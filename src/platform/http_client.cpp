#include "platform/http_client.h"
#include "platform/path.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace beachlife::platform {
namespace {
// Receives the body; throws to stop the transfer.
using Sink = std::function<void(const char* data, std::size_t size)>;

#ifndef _WIN32
// libcurl's stable easy interface (libcurl.so.4 / libcurl.4.dylib). The option
// numbers are part of its ABI (curl.h).
constexpr int curl_url = 10002, curl_write_function = 20011, curl_write_data = 10001, curl_user_agent = 10018,
              curl_http_header = 10023, curl_follow_location = 52, curl_max_redirects = 68, curl_connect_timeout = 78,
              curl_low_speed_limit = 19, curl_low_speed_time = 20, curl_no_progress = 43,
              curl_progress_function = 20219, curl_progress_data = 10057, curl_no_signal = 99,
              curl_accept_encoding = 10102, curl_error_buffer = 10010, curl_protocols = 10318,
              curl_redirect_protocols = 10319, curl_protocol_mask = 181, curl_redirect_protocol_mask = 182;
constexpr long curl_https_only = 2; // CURLPROTO_HTTPS
constexpr int curl_response_code = 0x200002;
constexpr int curl_aborted = 42, curl_write_error = 23, curl_timed_out = 28;

struct CurlApi {
    int (*global_init)(long) = nullptr;
    void* (*easy_init)() = nullptr;
    int (*easy_setopt)(void*, int, ...) = nullptr;
    int (*easy_perform)(void*) = nullptr;
    int (*easy_getinfo)(void*, int, ...) = nullptr;
    void (*easy_cleanup)(void*) = nullptr;
    const char* (*easy_strerror)(int) = nullptr;
    void* (*slist_append)(void*, const char*) = nullptr;
    void (*slist_free_all)(void*) = nullptr;
    bool ready = false;
    std::string error;
};

const CurlApi& curl() {
    static CurlApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        const char* const names[] = {
#ifdef __APPLE__
            "/usr/lib/libcurl.4.dylib", "libcurl.4.dylib",
#else
            "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so",
#endif
        };
        void* library = nullptr;
        for (const char* name : names)
            if ((library = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        if (!library) {
            api.error = "the system's libcurl was not found";
            return;
        }
        // Kept loaded for the rest of the run.
        bool complete = true;
        auto load = [&](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
            complete = complete && function;
        };
        load(api.global_init, "curl_global_init");
        load(api.easy_init, "curl_easy_init");
        load(api.easy_setopt, "curl_easy_setopt");
        load(api.easy_perform, "curl_easy_perform");
        load(api.easy_getinfo, "curl_easy_getinfo");
        load(api.easy_cleanup, "curl_easy_cleanup");
        load(api.easy_strerror, "curl_easy_strerror");
        load(api.slist_append, "curl_slist_append");
        load(api.slist_free_all, "curl_slist_free_all");
        if (!complete) api.error = "the system's libcurl is incomplete";
        else if (api.global_init(3 /* CURL_GLOBAL_DEFAULT */) != 0) api.error = "the system's libcurl failed to start";
        else api.ready = true;
    });
    return api;
}
#else
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string narrow(const wchar_t* text, int length) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}

std::string winhttp_message(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT: return "the connection timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "the server name could not be resolved (no internet connection?)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "could not connect to the server";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "the connection was reset";
    case ERROR_WINHTTP_SECURE_FAILURE: return "the secure connection failed";
    default: break;
    }
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE |
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        GetModuleHandleW(L"winhttp.dll"), error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string message = length ? narrow(buffer, static_cast<int>(length)) : std::string();
    if (buffer) LocalFree(buffer);
    while (!message.empty() && std::string_view("\r\n .").find(message.back()) != std::string_view::npos)
        message.pop_back();
    return message.empty() ? "network error " + std::to_string(error) : message;
}

struct InternetHandle {
    HINTERNET handle = nullptr;
    ~InternetHandle() { if (handle) WinHttpCloseHandle(handle); }
};
#endif
} // namespace

class HttpTransfer {
public:
    HttpTransfer(const HttpRequest& request, const HttpProgress& progress) : request_(request), progress_(progress) {}

    long run(const Sink& sink) {
        if (request_.url.rfind("https://", 0) != 0) throw HttpError("Only https addresses can be requested: " + request_.url);
        // A network failure before any data arrived is tried once more (a dropped
        // or stale connection); a partial answer is never repeated.
        const Sink counted = [&](const char* data, std::size_t size) {
            delivered_ = true;
            sink(data, size);
        };
        for (int attempt = 0;; ++attempt) {
            if (cancelled()) throw HttpError("Cancelled", true);
            try {
#ifdef _WIN32
                return run_winhttp(counted);
#else
                return run_curl(counted);
#endif
            } catch (const HttpError& error) {
                if (error.cancelled() || delivered_ || attempt > 0) throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

private:
    bool cancelled() const { return request_.cancel && request_.cancel->cancelled(); }

    void report(std::uint64_t received, std::uint64_t total) {
        if (progress_) progress_(received, total);
    }

#ifdef _WIN32
    // The request handle is shared with HttpCancel::cancel, which closes it to end
    // a blocking WinHTTP call at once; whoever takes it first closes it.
    void hold(HINTERNET handle) {
        if (!request_.cancel) return;
        const std::lock_guard lock(request_.cancel->mutex_);
        request_.cancel->handle_ = handle;
    }

    bool release() {
        if (!request_.cancel) return true;
        const std::lock_guard lock(request_.cancel->mutex_);
        const bool owned = request_.cancel->handle_ != nullptr;
        request_.cancel->handle_ = nullptr;
        return owned;
    }

    [[noreturn]] void fail(DWORD error) {
        if (cancelled() || error == ERROR_WINHTTP_OPERATION_CANCELLED) throw HttpError("Cancelled", true);
        throw HttpError(winhttp_message(error));
    }

    long run_winhttp(const Sink& sink) {
        const std::wstring url = wide(request_.url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof parts;
        parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
        if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
            throw HttpError("Invalid address: " + request_.url);
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

        const std::wstring agent = wide(request_.user_agent);
        InternetHandle session;
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
        // The system's proxy settings (Windows 8.1 and newer).
        session.handle = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
#endif
        if (!session.handle)
            session.handle = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session.handle) fail(GetLastError());
        const int connect = request_.connect_timeout*1000, stall = request_.stall_timeout*1000;
        WinHttpSetTimeouts(session.handle, connect, connect, stall, stall);
        InternetHandle connection;
        connection.handle = WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0);
        if (!connection.handle) fail(GetLastError());
        HINTERNET handle = WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!handle) fail(GetLastError());
        hold(handle);
        // A fresh connection per request: a kept-alive connection the server has
        // closed meanwhile fails the next request.
        DWORD features = WINHTTP_DISABLE_KEEP_ALIVE;
        WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof features);
        struct Close {
            HttpTransfer& transfer;
            HINTERNET handle;
            ~Close() { if (transfer.release()) WinHttpCloseHandle(handle); }
        } close{*this, handle};
        if (cancelled()) throw HttpError("Cancelled", true);
#if defined(WINHTTP_OPTION_DECOMPRESSION) && defined(WINHTTP_DECOMPRESSION_FLAG_ALL)
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL; // Windows 8.1 and newer; optional
        WinHttpSetOption(handle, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof decompression);
#endif
        std::wstring headers;
        for (const auto& header : request_.headers) headers += wide(header) + L"\r\n";
        if (!WinHttpSendRequest(handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                headers.empty() ? 0 : DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(handle, nullptr))
            fail(GetLastError());
        DWORD status = 0, size = sizeof status;
        if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
            fail(GetLastError());
        DWORD length = 0;
        size = sizeof length;
        const std::uint64_t total = WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX) ? length : 0;
        std::vector<char> buffer(1 << 16);
        std::uint64_t received = 0;
        report(0, total);
        for (;;) {
            DWORD read = 0;
            if (!WinHttpReadData(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) fail(GetLastError());
            if (read == 0) break;
            received += read;
            sink(buffer.data(), read);
            report(received, total);
            if (cancelled()) throw HttpError("Cancelled", true);
        }
        return static_cast<long>(status);
    }
#else
    struct CurlContext {
        HttpTransfer* transfer;
        const Sink* sink;
        std::exception_ptr failure;
    };

    static std::size_t write(char* data, std::size_t size, std::size_t count, void* context) {
        auto& state = *static_cast<CurlContext*>(context);
        try {
            (*state.sink)(data, size*count);
            return size*count;
        } catch (...) {
            state.failure = std::current_exception();
            return 0;
        }
    }

    static int progress(void* context, std::int64_t total, std::int64_t now, std::int64_t, std::int64_t) {
        auto& state = *static_cast<CurlContext*>(context);
        if (state.transfer->cancelled()) return 1;
        state.transfer->report(static_cast<std::uint64_t>(std::max<std::int64_t>(now, 0)),
                               static_cast<std::uint64_t>(std::max<std::int64_t>(total, 0)));
        return 0;
    }

    long run_curl(const Sink& sink) {
        const auto& api = curl();
        if (!api.ready) throw HttpError("Cannot connect: " + api.error);
        void* handle = api.easy_init();
        if (!handle) throw HttpError("Cannot connect: libcurl could not start a request");
        void* headers = nullptr;
        struct Cleanup {
            const CurlApi& api;
            void*& handle;
            void*& headers;
            ~Cleanup() {
                if (headers) api.slist_free_all(headers);
                api.easy_cleanup(handle);
            }
        } cleanup{api, handle, headers};
        for (const auto& header : request_.headers)
            if (void* list = api.slist_append(headers, header.c_str())) headers = list;
        char error[256] = ""; // CURL_ERROR_SIZE
        CurlContext context{this, &sink, nullptr};
        api.easy_setopt(handle, curl_url, request_.url.c_str());
        // HTTPS only, also for redirects; libcurl before 7.85 knows only the (since
        // deprecated) protocol masks.
        if (api.easy_setopt(handle, curl_protocols, "https") != 0)
            api.easy_setopt(handle, curl_protocol_mask, curl_https_only);
        if (api.easy_setopt(handle, curl_redirect_protocols, "https") != 0)
            api.easy_setopt(handle, curl_redirect_protocol_mask, curl_https_only);
        api.easy_setopt(handle, curl_follow_location, 1L);
        api.easy_setopt(handle, curl_max_redirects, 10L);
        api.easy_setopt(handle, curl_user_agent, request_.user_agent.c_str());
        if (headers) api.easy_setopt(handle, curl_http_header, headers);
        api.easy_setopt(handle, curl_accept_encoding, "");
        api.easy_setopt(handle, curl_no_signal, 1L);
        api.easy_setopt(handle, curl_connect_timeout, static_cast<long>(request_.connect_timeout));
        api.easy_setopt(handle, curl_low_speed_limit, 1L);
        api.easy_setopt(handle, curl_low_speed_time, static_cast<long>(request_.stall_timeout));
        api.easy_setopt(handle, curl_error_buffer, error);
        api.easy_setopt(handle, curl_write_function, &write);
        api.easy_setopt(handle, curl_write_data, &context);
        api.easy_setopt(handle, curl_no_progress, 0L);
        api.easy_setopt(handle, curl_progress_function, &progress);
        api.easy_setopt(handle, curl_progress_data, &context);
        report(0, 0);
        const int result = api.easy_perform(handle);
        if (context.failure) std::rethrow_exception(context.failure);
        if (result == curl_aborted || cancelled()) throw HttpError("Cancelled", true);
        if (result == curl_timed_out) throw HttpError("the connection timed out");
        if (result != 0 && result != curl_write_error)
            throw HttpError(*error ? std::string(error) : std::string(api.easy_strerror(result)));
        if (result != 0) throw HttpError("the download could not be stored");
        long status = 0;
        api.easy_getinfo(handle, curl_response_code, &status);
        return status;
    }
#endif

    const HttpRequest& request_;
    const HttpProgress& progress_;
    bool delivered_ = false;
};

void HttpCancel::cancel() {
    cancelled_ = true;
#ifdef _WIN32
    const std::lock_guard lock(mutex_);
    if (handle_) WinHttpCloseHandle(static_cast<HINTERNET>(handle_));
    handle_ = nullptr;
#endif
}

std::string http_unavailable() {
#ifdef _WIN32
    return {};
#else
    const auto& api = curl();
    return api.ready ? std::string() : api.error;
#endif
}

HttpResponse http_get(const HttpRequest& request, const HttpProgress& progress) {
    HttpResponse response;
    response.status = HttpTransfer(request, progress).run([&](const char* data, std::size_t size) {
        if (response.body.size()+size > request.max_size) throw HttpError("The answer is too large");
        response.body.append(data, size);
    });
    return response;
}

long http_download(const HttpRequest& request, const std::filesystem::path& file, const HttpProgress& progress) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream) throw HttpError("Cannot create " + utf8(file));
    long status = 0;
    try {
        status = HttpTransfer(request, progress).run([&](const char* data, std::size_t size) {
            if (!stream.write(data, static_cast<std::streamsize>(size))) throw HttpError("Cannot write " + utf8(file));
        });
        stream.close();
        if (!stream) throw HttpError("Cannot write " + utf8(file));
    } catch (...) {
        stream.close();
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        throw;
    }
    if (status != 200) {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
    }
    return status;
}

} // namespace beachlife::platform

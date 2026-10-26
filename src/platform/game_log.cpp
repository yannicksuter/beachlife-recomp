#include "platform/game_log.h"

#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace beachlife::platform
{
namespace
{
namespace fs = std::filesystem;

// How a session ended; a log with none of these lines crashed or was killed.
constexpr char ended[] = "Beach Life: the session ended.\n";
constexpr char exited[] = "Beach Life: the game exited.\n";
constexpr char reported[] = "Beach Life: the launcher reported this stop.\n";

bool active = false;
std::terminate_handler previous_terminate = nullptr;

#ifdef _WIN32
FILE* log_file = nullptr; // the reopened stderr, or a file of its own beside a console
bool own_file = false;
bool redirected = false; // stderr still writes to the last log, released before the next one
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
void (*previous_abort)(int) = nullptr;

void say(const char* text)
{
    if (log_file)
    {
        std::fputs(text, log_file);
        std::fflush(log_file);
    }
}

// module+offset, which a symbolizer resolves against the same package.
void say_where(const void* address)
{
    HMODULE module = nullptr;
    char text[MAX_PATH * 3 + 32];
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(address),
            &module))
    {
        std::snprintf(text, sizeof text, "%p", address);
        say(text);
        return;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* name = std::wcsrchr(path, L'\\');
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(
        CP_UTF8, 0, name ? name + 1 : path, -1, utf8, sizeof utf8, nullptr, nullptr);
    std::snprintf(text,
        sizeof text,
        "%s+0x%llx",
        utf8,
        static_cast<unsigned long long>(
            reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module)));
    say(text);
}

void say_stack()
{
    std::array<void*, 62> frames{};
    const USHORT count =
        RtlCaptureStackBackTrace(1, static_cast<ULONG>(frames.size()), frames.data(), nullptr);
    say("Call stack (newest first):\n");
    for (USHORT index = 0; index < count; ++index)
    {
        say("  ");
        say_where(frames[index]);
        say("\n");
    }
}

const char* describe(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        return "invalid memory access";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "division by zero";
    default:
        return "unhandled exception";
    }
}

LONG WINAPI crashed(EXCEPTION_POINTERS* exception)
{
    const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
    char text[128];
    std::snprintf(text,
        sizeof text,
        "\nBeach Life stopped: %s (exception 0x%08lx) at ",
        describe(record.ExceptionCode),
        static_cast<unsigned long>(record.ExceptionCode));
    say(text);
    say_where(record.ExceptionAddress);
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
    {
        const auto kind = record.ExceptionInformation[0];
        std::snprintf(text,
            sizeof text,
            ", %s address 0x%llx",
            kind == 0   ? "reading"
            : kind == 1 ? "writing"
                        : "executing",
            static_cast<unsigned long long>(record.ExceptionInformation[1]));
        say(text);
    }
    say("\n");
    say_stack();
    return previous_filter ? previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

void aborted(int)
{
    say("\nBeach Life stopped: abort\n");
    say_stack();
    std::signal(SIGABRT, SIG_DFL); // abort() goes on to end the process
}
#else
constexpr std::array<int, 6> fatal = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
int log_fd = -1;
int saved_stderr = -1;
bool to_terminal = false; // stderr stays on a terminal, which also gets the crash report
std::array<struct sigaction, fatal.size()> previous_actions{};
stack_t previous_stack{};
// The crash handler's own stack, so that a stack overflow can still be reported.
alignas(16) char signal_stack[1 << 16];

void put(int fd, const char* text)
{
    for (std::size_t left = std::strlen(text); left > 0;)
    {
        const ssize_t written = ::write(fd, text, left);
        if (written <= 0)
        {
            return;
        }
        text += written;
        left -= static_cast<std::size_t>(written);
    }
}

void say(const char* text)
{
    if (log_fd >= 0)
    {
        put(log_fd, text);
    }
    if (to_terminal)
    {
        put(STDERR_FILENO, text);
    }
}

// Formats without the heap, which a crash handler must not use.
void say_hex(std::uintptr_t value)
{
    char text[2 + 2 * sizeof value + 1];
    char* at = text + sizeof text - 1;
    *at = 0;
    do
    {
        *--at = "0123456789abcdef"[value & 15];
        value >>= 4;
    } while (value);
    *--at = 'x';
    *--at = '0';
    say(at);
}

const char* describe(int number)
{
    switch (number)
    {
    case SIGSEGV:
        return "invalid memory access (SIGSEGV)";
    case SIGBUS:
        return "invalid memory access (SIGBUS)";
    case SIGILL:
        return "illegal instruction (SIGILL)";
    case SIGFPE:
        return "arithmetic error (SIGFPE)";
    case SIGABRT:
        return "abort (SIGABRT)";
    default:
        return "trap (SIGTRAP)";
    }
}

void crashed(int number, siginfo_t* info, void*)
{
    say("\nBeach Life stopped: ");
    say(describe(number));
    // Codes 1 to 3 are the kernel's own memory faults (SEGV_MAPERR/ACCERR,
    // BUS_ADRALN/ADRERR/OBJERR); a signal sent by another process has no address.
    if ((number == SIGSEGV || number == SIGBUS) && info && info->si_code >= 1 && info->si_code <= 3)
    {
        say(" at address ");
        say_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    }
    say("\nCall stack (newest first):\n");
    std::array<void*, 64> frames{};
    const int count = backtrace(frames.data(), static_cast<int>(frames.size()));
    if (log_fd >= 0)
    {
        backtrace_symbols_fd(frames.data(), count, log_fd);
    }
    if (to_terminal)
    {
        backtrace_symbols_fd(frames.data(), count, STDERR_FILENO);
    }
    // SA_RESETHAND restored the default action: the system's crash handling follows.
    raise(number);
}
#endif

void mark_exit()
{
    if (active)
    {
        say(exited);
    }
}

[[noreturn]] void terminated()
{
    if (const auto current = std::current_exception())
    {
        try
        {
            std::rethrow_exception(current);
        }
        catch (const std::exception& error)
        {
            say("\nBeach Life: uncaught exception: ");
            say(error.what());
            say("\n");
        }
        catch (...)
        {
            say("\nBeach Life: uncaught exception of an unknown type\n");
        }
    }
    std::abort();
}
} // namespace

GameLog::GameLog(const fs::path& folder, const std::string& header)
{
    std::error_code error;
    fs::create_directories(folder, error);
    const fs::path file = folder / "game.log";
#ifdef _WIN32
    if (redirected)
    {
        // Release the previous log so that it can move aside.
        std::freopen("NUL", "w", stderr);
        redirected = false;
    }
    fs::rename(file, folder / "game-previous.log", error);
    if (_isatty(_fileno(stderr)))
    {
        log_file = _wfopen(file.c_str(), L"w");
        own_file = true;
    }
    else
    {
        log_file = _wfreopen(file.c_str(), L"w", stderr);
        redirected = log_file != nullptr;
        own_file = false;
    }
    if (!log_file)
    {
        return;
    }
    say(header.c_str());
    say("\n");
    previous_filter = SetUnhandledExceptionFilter(crashed);
    previous_abort = std::signal(SIGABRT, aborted);
#else
    fs::rename(file, folder / "game-previous.log", error);
    log_fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log_fd < 0)
    {
        return;
    }
    put(log_fd, header.c_str());
    put(log_fd, "\n");
    std::fflush(stderr);
    to_terminal = isatty(STDERR_FILENO);
    if (!to_terminal)
    {
        saved_stderr = dup(STDERR_FILENO);
        dup2(log_fd, STDERR_FILENO);
    }
    // backtrace loads its unwinder on first use; do that now, not in the handler.
    std::array<void*, 1> warm{};
    backtrace(warm.data(), 1);
    stack_t stack{};
    stack.ss_sp = signal_stack;
    stack.ss_size = sizeof signal_stack;
    sigaltstack(&stack, &previous_stack);
    struct sigaction action{};
    action.sa_sigaction = crashed;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t index = 0; index < fatal.size(); ++index)
    {
        sigaction(fatal[index], &action, &previous_actions[index]);
    }
#endif
    previous_terminate = std::set_terminate(terminated);
    static const bool exit_hook = std::atexit(mark_exit) == 0;
    (void)exit_hook;
    active = true;
}

GameLog::~GameLog()
{
    if (!active)
    {
        return;
    }
    say(ended);
    active = false;
    std::set_terminate(previous_terminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(previous_filter);
    std::signal(SIGABRT, previous_abort ? previous_abort : SIG_DFL);
    if (own_file)
    {
        std::fclose(log_file);
    }
    log_file = nullptr;
#else
    for (std::size_t index = 0; index < fatal.size(); ++index)
    {
        sigaction(fatal[index], &previous_actions[index], nullptr);
    }
    sigaltstack(&previous_stack, nullptr);
    std::fflush(stderr);
    if (saved_stderr >= 0)
    {
        dup2(saved_stderr, STDERR_FILENO);
        ::close(saved_stderr);
        saved_stderr = -1;
    }
    ::close(log_fd);
    log_fd = -1;
#endif
}

fs::path unreported_crash(const fs::path& folder)
{
    const fs::path file = folder / "game.log";
    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            return {};
        }
        text.assign(std::istreambuf_iterator<char>(in), {});
    }
    for (const char* line : {ended, exited, reported})
    {
        if (text.find(line) != std::string::npos)
        {
            return {};
        }
    }
    std::ofstream(file, std::ios::binary | std::ios::app) << reported;
    return file;
}

} // namespace beachlife::platform

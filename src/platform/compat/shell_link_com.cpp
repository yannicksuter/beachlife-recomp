#include "shell_link.h"

#include "ansi_text.h"
#include "data/game_path.h"
#include "kernel_files.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace beachlife::compat::shell
{
namespace
{
constexpr IID unknown_iid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
constexpr HRESULT pointer_error = static_cast<HRESULT>(0x80004003);
constexpr HRESULT invalid_argument = static_cast<HRESULT>(0x80070057);
constexpr HRESULT file_not_found = static_cast<HRESULT>(0x80030002);
bool same(REFGUID a, REFGUID b)
{
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

template <class F> HRESULT boundary(const char* api, F&& function) noexcept
{
    try
    {
        return function();
    }
    catch (const UnsupportedLink& error)
    {
        unsupported(api, error.what());
    }
    catch (const InvalidLink&)
    {
        return static_cast<HRESULT>(0x800300fb); // STG_E_INVALIDHEADER
    }
    catch (const std::bad_alloc&)
    {
        return static_cast<HRESULT>(0x8007000e);
    }
    catch (const std::invalid_argument&)
    {
        return invalid_argument;
    }
    catch (const std::exception& error)
    {
        unsupported(api, error.what());
    }
    catch (...)
    {
        unsupported(api, "unexpected host exception");
    }
}

bool relative(std::string_view path)
{
    return !path.empty() && path.front() != '/' && path.front() != '\\' &&
           path.find(':') == std::string_view::npos;
}

std::string relative_target(std::string_view link_path, std::string_view target)
{
    if (!relative(target))
    {
        throw UnsupportedLink("absolute ShellLink target has no mounted drive/share mapping");
    }
    const auto slash = link_path.find_last_of("/\\");
    std::string combined(
        slash == std::string_view::npos ? std::string_view{} : link_path.substr(0, slash + 1));
    combined += target;
    std::vector<std::string_view> parts;
    for (std::size_t offset = 0; offset < combined.size();)
    {
        const auto end = combined.find_first_of("/\\", offset);
        const auto part = std::string_view(combined).substr(
            offset, end == std::string::npos ? end : end - offset);
        offset = end == std::string::npos ? combined.size() : end + 1;
        if (part.empty() || part == ".")
        {
            continue;
        }
        if (part == "..")
        {
            if (parts.empty())
            {
                throw UnsupportedLink("relative ShellLink target escapes the mounted VFS");
            }
            parts.pop_back();
        }
        else
        {
            parts.push_back(part);
        }
    }
    std::string result;
    for (const auto part : parts)
    {
        if (!result.empty())
        {
            result += '/';
        }
        result += part;
    }
    if (result.empty())
    {
        return ".";
    }
    return data::normalize_game_path(result).display;
}

class ShellLink final : public IShellLinkA, public IPersistFile
{
  public:
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (same(iid, unknown_iid) || same(iid, IID_IShellLinkA))
        {
            *output = static_cast<IShellLinkA*>(this);
        }
        else if (same(iid, IID_IPersist) || same(iid, IID_IPersistFile))
        {
            *output = static_cast<IPersistFile*>(this);
        }
        else
        {
            return static_cast<HRESULT>(0x80004002);
        }
        AddRef();
        return S_OK;
    }
    ULONG AddRef() override
    {
        return ++references_;
    }
    ULONG Release() override
    {
        const auto count = --references_;
        if (!count)
        {
            delete this;
        }
        return count;
    }
    HRESULT GetClassID(CLSID* clsid) override
    {
        if (!clsid)
        {
            return pointer_error;
        }
        *clsid = CLSID_ShellLink;
        return S_OK;
    }
    HRESULT IsDirty() override
    {
        return 1;
    } // S_FALSE: read-only loaded object
    HRESULT Load(LPCWSTR path, DWORD mode) override
    {
        return boundary("IPersistFile::Load",
            [&]() -> HRESULT
            {
                loaded_ = false;
                target_ = {};
                resolved_.clear();
                source_.clear();
                if (!path)
                {
                    return pointer_error;
                }
                if (mode)
                {
                    unsupported("IPersistFile::Load", "only STGM_READ is implemented");
                }
                const int count = WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, nullptr, 0, nullptr, nullptr);
                if (!count)
                {
                    return invalid_argument;
                }
                std::string name(count, '\0');
                if (!WideCharToMultiByte(CP_UTF8,
                        WC_ERR_INVALID_CHARS,
                        path,
                        -1,
                        name.data(),
                        count,
                        nullptr,
                        nullptr))
                {
                    return invalid_argument;
                }
                source_ = kernel::logical_path(name.c_str());
                const auto bytes = kernel::read_file(name.c_str());
                if (!bytes)
                {
                    const auto error = GetLastError();
                    return error ? static_cast<HRESULT>(0x80070000u | (error & 0xffff))
                                 : file_not_found;
                }
                target_ = parse(*bytes);
                loaded_ = true;
                return S_OK;
            });
    }
    HRESULT Resolve(HWND, DWORD flags) override
    {
        return boundary("IShellLinkA::Resolve",
            [&]() -> HRESULT
            {
                if (flags != SLR_NO_UI)
                {
                    unsupported("IShellLinkA::Resolve", "only SLR_NO_UI resolution is implemented");
                }
                resolved_.clear();
                if (!loaded_)
                {
                    return E_FAIL;
                }
                std::vector<std::string> candidates;
                if (relative(target_.local_path))
                {
                    candidates.push_back(relative_target("", target_.local_path));
                }
                if (!target_.relative_path.empty())
                {
                    candidates.push_back(relative_target(source_, target_.relative_path));
                }
                if (candidates.empty())
                {
                    unsupported("IShellLinkA::Resolve",
                        "target needs a Windows drive/share or shell namespace");
                }
                for (auto& candidate : candidates)
                {
                    const auto attributes = GetFileAttributesA(candidate.c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES)
                    {
                        resolved_ = std::move(candidate);
                        target_.metadata.dwFileAttributes = attributes;
                        return S_OK;
                    }
                }
                return file_not_found;
            });
    }
    HRESULT GetPath(LPSTR path, int capacity, WIN32_FIND_DATAA* data, DWORD flags) override
    {
        if (!path)
        {
            return pointer_error;
        }
        if (capacity <= 0)
        {
            return invalid_argument;
        }
        path[0] = 0;
        if (data)
        {
            *data = {};
        }
        return boundary("IShellLinkA::GetPath",
            [&]() -> HRESULT
            {
                if (flags & ~SLGP_SHORTPATH)
                {
                    unsupported("IShellLinkA::GetPath", "UNC-priority or raw path flags");
                }
                if (!loaded_)
                {
                    return E_FAIL;
                }
                const std::string target = !resolved_.empty() ? resolved_
                                           : !target_.local_path.empty()
                                               ? target_.local_path
                                               : relative_target(source_, target_.relative_path);
                std::string ansi;
                if (!utf8_to_ansi(target, ansi))
                {
                    unsupported(
                        "IShellLinkA::GetPath", "target is not representable in Windows-1252");
                }
                std::replace(ansi.begin(), ansi.end(), '/', '\\');
                // This filesystem has no separate DOS aliases. As on Windows
                // volumes without 8.3 names, SHORTPATH returns the existing name.
                if (ansi.size() >= static_cast<std::size_t>(capacity))
                {
                    return static_cast<HRESULT>(0x8007007a); // insufficient buffer
                }
                const auto separator = ansi.find_last_of('\\');
                const auto name = std::string_view(ansi).substr(
                    separator == std::string::npos ? 0 : separator + 1);
                if (data && name.size() >= sizeof(data->cFileName))
                {
                    return static_cast<HRESULT>(0x8007007a);
                }
                std::memcpy(path, ansi.c_str(), ansi.size() + 1);
                if (data)
                {
                    *data = target_.metadata;
                    std::memcpy(data->cFileName, name.data(), name.size());
                    data->cFileName[name.size()] = 0;
                }
                return S_OK;
            });
    }
    HRESULT GetIDList(void**) override
    {
        unsupported("IShellLinkA::GetIDList", "shell namespace objects");
    }
    HRESULT SetIDList(const void*) override
    {
        unsupported("IShellLinkA::SetIDList", "creating shortcuts");
    }
    HRESULT GetDescription(LPSTR, int) override
    {
        unsupported("IShellLinkA::GetDescription", "shortcut metadata queries");
    }
    HRESULT SetDescription(LPCSTR) override
    {
        unsupported("IShellLinkA::SetDescription", "creating shortcuts");
    }
    HRESULT GetWorkingDirectory(LPSTR, int) override
    {
        unsupported("IShellLinkA::GetWorkingDirectory", "shortcut metadata queries");
    }
    HRESULT SetWorkingDirectory(LPCSTR) override
    {
        unsupported("IShellLinkA::SetWorkingDirectory", "creating shortcuts");
    }
    HRESULT GetArguments(LPSTR, int) override
    {
        unsupported("IShellLinkA::GetArguments", "shortcut metadata queries");
    }
    HRESULT SetArguments(LPCSTR) override
    {
        unsupported("IShellLinkA::SetArguments", "creating shortcuts");
    }
    HRESULT GetHotkey(WORD*) override
    {
        unsupported("IShellLinkA::GetHotkey", "shortcut metadata queries");
    }
    HRESULT SetHotkey(WORD) override
    {
        unsupported("IShellLinkA::SetHotkey", "creating shortcuts");
    }
    HRESULT GetShowCmd(int*) override
    {
        unsupported("IShellLinkA::GetShowCmd", "shortcut metadata queries");
    }
    HRESULT SetShowCmd(int) override
    {
        unsupported("IShellLinkA::SetShowCmd", "creating shortcuts");
    }
    HRESULT GetIconLocation(LPSTR, int, int*) override
    {
        unsupported("IShellLinkA::GetIconLocation", "shortcut metadata queries");
    }
    HRESULT SetIconLocation(LPCSTR, int) override
    {
        unsupported("IShellLinkA::SetIconLocation", "creating shortcuts");
    }
    HRESULT SetRelativePath(LPCSTR, DWORD) override
    {
        unsupported("IShellLinkA::SetRelativePath", "creating shortcuts");
    }
    HRESULT SetPath(LPCSTR) override
    {
        unsupported("IShellLinkA::SetPath", "creating shortcuts");
    }
    HRESULT Save(LPCWSTR, BOOL) override
    {
        unsupported("IPersistFile::Save", "creating shortcuts");
    }
    HRESULT SaveCompleted(LPCWSTR) override
    {
        unsupported("IPersistFile::SaveCompleted", "creating shortcuts");
    }
    HRESULT GetCurFile(LPWSTR*) override
    {
        unsupported("IPersistFile::GetCurFile", "COM task-allocator ownership");
    }

  private:
    std::atomic<ULONG> references_{1};
    bool loaded_ = false;
    Target target_;
    std::string source_, resolved_;
};
} // namespace

HRESULT create(REFIID iid, void** output)
{
    return boundary("CoCreateInstance(ShellLink)",
        [&]() -> HRESULT
        {
            auto* link = new ShellLink;
            const auto result = link->QueryInterface(iid, output);
            link->Release();
            return result;
        });
}
} // namespace beachlife::compat::shell

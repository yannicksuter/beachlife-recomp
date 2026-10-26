#pragma once

#include "windows.h"

inline constexpr CLSID CLSID_ShellLink = {0x00021401, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
inline constexpr IID IID_IShellLinkA = {0x000214ee, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
inline constexpr IID IID_IPersist = {0x0000010c, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
inline constexpr IID IID_IPersistFile = {0x0000010b, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
inline constexpr DWORD SLR_NO_UI = 1;
inline constexpr DWORD SLGP_SHORTPATH = 1;

struct FILETIME
{
    DWORD dwLowDateTime, dwHighDateTime;
};
struct WIN32_FIND_DATAA
{
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    char cFileName[260], cAlternateFileName[14];
};
static_assert(sizeof(WIN32_FIND_DATAA) == 320);

// Preserve SDK vtable order, including methods not used by the retail caller.
struct IShellLinkA : IUnknown
{
    virtual HRESULT GetPath(LPSTR path, int capacity, WIN32_FIND_DATAA* data, DWORD flags) = 0;
    virtual HRESULT GetIDList(void** list) = 0;
    virtual HRESULT SetIDList(const void* list) = 0;
    virtual HRESULT GetDescription(LPSTR text, int capacity) = 0;
    virtual HRESULT SetDescription(LPCSTR text) = 0;
    virtual HRESULT GetWorkingDirectory(LPSTR path, int capacity) = 0;
    virtual HRESULT SetWorkingDirectory(LPCSTR path) = 0;
    virtual HRESULT GetArguments(LPSTR text, int capacity) = 0;
    virtual HRESULT SetArguments(LPCSTR text) = 0;
    virtual HRESULT GetHotkey(WORD* key) = 0;
    virtual HRESULT SetHotkey(WORD key) = 0;
    virtual HRESULT GetShowCmd(int* command) = 0;
    virtual HRESULT SetShowCmd(int command) = 0;
    virtual HRESULT GetIconLocation(LPSTR path, int capacity, int* index) = 0;
    virtual HRESULT SetIconLocation(LPCSTR path, int index) = 0;
    virtual HRESULT SetRelativePath(LPCSTR path, DWORD reserved) = 0;
    virtual HRESULT Resolve(HWND window, DWORD flags) = 0;
    virtual HRESULT SetPath(LPCSTR path) = 0;
};
struct IPersist : IUnknown
{
    virtual HRESULT GetClassID(CLSID* clsid) = 0;
};
struct IPersistFile : IPersist
{
    virtual HRESULT IsDirty() = 0;
    virtual HRESULT Load(LPCWSTR path, DWORD mode) = 0;
    virtual HRESULT Save(LPCWSTR path, BOOL remember) = 0;
    virtual HRESULT SaveCompleted(LPCWSTR path) = 0;
    virtual HRESULT GetCurFile(LPWSTR* path) = 0;
};

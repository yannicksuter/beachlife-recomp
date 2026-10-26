// The subset of the Windows API that Beach Life imports, for compiling the
// decompiled game unchanged on the host. Integer types keep their 32-bit
// Windows widths so retail structures keep their layout; handles are opaque
// pointers. Functions are implemented on SDL3 in src/platform/compat; a
// function declared here but not implemented yet becomes a boot-build trap.
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef short SHORT;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef unsigned int UINT;
typedef int INT;
typedef char CHAR;
// Retail strings and on-disk text use UTF-16, independently of host wchar_t.
typedef uint16_t WCHAR;
typedef WORD ATOM;
typedef DWORD COLORREF;
typedef int32_t HRESULT;
typedef size_t SIZE_T;
typedef intptr_t LPARAM;
typedef uintptr_t WPARAM;
typedef intptr_t LRESULT;
typedef int HFILE;
typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef WCHAR* LPWSTR;
typedef const WCHAR* LPCWSTR;
typedef BOOL* LPBOOL;
typedef LONG* LPLONG;
typedef DWORD* LPDWORD;

typedef void* HANDLE;
typedef struct HWND__* HWND;
typedef struct HINSTANCE__* HINSTANCE;
typedef HINSTANCE HMODULE;
typedef struct HICON__* HICON;
typedef HICON HCURSOR;
typedef struct HBRUSH__* HBRUSH;
typedef struct HDC__* HDC;
typedef struct HMENU__* HMENU;
typedef void* HGDIOBJ;
typedef struct HFONT__* HFONT;
typedef struct HBITMAP__* HBITMAP;

// Compat and native SDK calls coexist in the same process. Never interpose
// on SDL, the C++ runtime or host code using the real Windows APIs.
#define BEACHLIFE_COMPAT_SYMBOL(name) __asm__("beachlife_compat_" #name)

#define WINAPI
#define CALLBACK
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005)
#define MAKEINTRESOURCEA(i) ((LPSTR)(uintptr_t)(WORD)(i))
#define LOWORD(l) ((WORD)((uintptr_t)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((uintptr_t)(l) >> 16) & 0xffff))
#define RGB(r, g, b)                                                                               \
    ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define ZeroMemory(destination, length) memset((destination), 0, (length))

// Constants the retail code uses, with their Windows values.
#define CP_ACP 0
#define CP_UTF8 65001
#define CP_WINDOWS_1252 1252
#define MB_PRECOMPOSED 0x00000001
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_ERR_INVALID_CHARS 0x00000080
#define WC_NO_BEST_FIT_CHARS 0x00000400
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_TOO_MANY_OPEN_FILES 4L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_DATA 13L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_WRITE_FAULT 29L
#define ERROR_READ_FAULT 30L
#define ERROR_GEN_FAILURE 31L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_INVALID_NAME 123L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_NOT_OWNER 288L
#define ERROR_NO_UNICODE_TRANSLATION 1113L
#define ERROR_ALREADY_EXISTS 183L
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD) - 1)
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define OF_READ 0x0000
#define OF_WRITE 0x0001
#define OF_READWRITE 0x0002
#define OF_SHARE_COMPAT 0x0000
#define OF_SHARE_EXCLUSIVE 0x0010
#define OF_SHARE_DENY_WRITE 0x0020
#define OF_SHARE_DENY_READ 0x0030
#define OF_SHARE_DENY_NONE 0x0040
#define OF_PARSE 0x0100
#define OF_DELETE 0x0200
#define OF_CREATE 0x1000
#define OF_EXIST 0x4000
#define OF_REOPEN 0x8000
#define HFILE_ERROR ((HFILE) - 1)
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define FILE_MAP_COPY 0x0001
#define FILE_MAP_WRITE 0x0002
#define FILE_MAP_READ 0x0004
#define FILE_MAP_ALL_ACCESS 0x000f001f
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY 0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK 0x000000ff
#define STARTF_USESHOWWINDOW 0x00000001
#define CW_USEDEFAULT ((int)0x80000000)
#define WM_QUIT 0x0012
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_ACTIVATEAPP 0x001c
#define WM_SYSCOMMAND 0x0112
#define SC_SIZE 0xf000
#define SC_MOVE 0xf010
#define SC_MAXIMIZE 0xf030
#define SC_KEYMENU 0xf100
#define SC_MONITORPOWER 0xf170
#define PM_REMOVE 0x0001
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define WS_POPUP 0x80000000L
#define WS_EX_TOPMOST 0x00000008L
#define SM_CXSIZEFRAME 32
#define SM_CYSIZEFRAME 33
#define SM_CYCAPTION 4
#define BLACK_BRUSH 4
#define BI_RGB 0L
#define DIB_RGB_COLORS 0
#define MM_TEXT 1
#define TA_LEFT 0
#define TA_TOP 0
#define ETO_OPAQUE 0x0002
#define FW_NORMAL 400
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define VARIABLE_PITCH 2
#define LOGPIXELSY 90

typedef struct tagRECT
{
    LONG left, top, right, bottom;
} RECT, *LPRECT;
typedef const RECT* LPCRECT;

typedef struct tagPOINT
{
    LONG x, y;
} POINT, *LPPOINT;

typedef struct tagSIZE
{
    LONG cx, cy;
} SIZE, *LPSIZE;

typedef struct tagMSG
{
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG;

typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASSEXA
{
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
    HICON hIconSm;
} WNDCLASSEXA;

typedef struct _STARTUPINFOA
{
    DWORD cb;
    LPSTR lpReserved, lpDesktop, lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow, cbReserved2;
    BYTE* lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;

typedef struct _OFSTRUCT
{
    BYTE cBytes, fFixedDisk;
    WORD nErrCode, Reserved1, Reserved2;
    CHAR szPathName[128];
} OFSTRUCT, *LPOFSTRUCT;

typedef struct _SECURITY_ATTRIBUTES
{
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct tagBITMAPINFOHEADER
{
    DWORD biSize;
    LONG biWidth, biHeight;
    WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagRGBQUAD
{
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO;

typedef struct tagKERNINGPAIR
{
    WORD wFirst, wSecond;
    int iKernAmount;
} KERNINGPAIR, *LPKERNINGPAIR;

typedef struct _GUID
{
    DWORD Data1;
    WORD Data2, Data3;
    BYTE Data4[8];
} GUID, IID, CLSID;
#ifdef __cplusplus
#define REFGUID const GUID&
#define REFIID const IID&
#define REFCLSID const CLSID&
#else
#define REFGUID const GUID*
#define REFIID const IID*
#define REFCLSID const CLSID*
#endif

#ifdef __cplusplus
// COM interfaces are plain C++ abstract classes on the host.
struct IUnknown
{
    virtual HRESULT QueryInterface(REFIID iid, void** object) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};
typedef IUnknown* LPUNKNOWN;
#endif

// The game is an ANSI build: generic names select the A functions.
#define DeleteFile DeleteFileA
#define GetStartupInfo GetStartupInfoA
#define GetModuleHandle GetModuleHandleA
#define GetPrivateProfileString GetPrivateProfileStringA
#define WritePrivateProfileString WritePrivateProfileStringA
#define GetCurrentDirectory GetCurrentDirectoryA
#define CreateDirectory CreateDirectoryA
#define GetFileAttributes GetFileAttributesA
#define CreateMutex CreateMutexA
#define CreateFileMapping CreateFileMappingA
#define FormatMessage FormatMessageA
#define PeekMessage PeekMessageA
#define DispatchMessage DispatchMessageA
#define DefWindowProc DefWindowProcA
#define RegisterClassEx RegisterClassExA
#define UnregisterClass UnregisterClassA
#define CreateWindowEx CreateWindowExA
#define GetWindowLong GetWindowLongA
#define SystemParametersInfo SystemParametersInfoA
#define LoadImage LoadImageA
#define LoadIcon LoadIconA
#define MessageBox MessageBoxA
#define AddFontResource AddFontResourceA
#define RemoveFontResource RemoveFontResourceA
#define CreateFont CreateFontA
#define GetTextExtentPoint32 GetTextExtentPoint32A
#define ExtTextOut ExtTextOutA
#define GetKerningPairs GetKerningPairsA
#define WNDCLASSEX WNDCLASSEXA
#define STARTUPINFO STARTUPINFOA

#ifdef __cplusplus
extern "C"
{
#endif

    // KERNEL32
    BOOL WINAPI DeleteFileA(LPCSTR path) BEACHLIFE_COMPAT_SYMBOL(DeleteFileA);
    void WINAPI GetStartupInfoA(LPSTARTUPINFOA info) BEACHLIFE_COMPAT_SYMBOL(GetStartupInfoA);
    HMODULE WINAPI GetModuleHandleA(LPCSTR name) BEACHLIFE_COMPAT_SYMBOL(GetModuleHandleA);
    DWORD WINAPI GetPrivateProfileStringA(
        LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR result, DWORD size, LPCSTR file)
        BEACHLIFE_COMPAT_SYMBOL(GetPrivateProfileStringA);
    BOOL WINAPI WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file)
        BEACHLIFE_COMPAT_SYMBOL(WritePrivateProfileStringA);
    DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer)
        BEACHLIFE_COMPAT_SYMBOL(GetCurrentDirectoryA);
    DWORD WINAPI GetTickCount(void) BEACHLIFE_COMPAT_SYMBOL(GetTickCount);
    BOOL WINAPI CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES security)
        BEACHLIFE_COMPAT_SYMBOL(CreateDirectoryA);
    DWORD WINAPI GetFileAttributesA(LPCSTR path) BEACHLIFE_COMPAT_SYMBOL(GetFileAttributesA);
    HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES security, BOOL owner, LPCSTR name)
        BEACHLIFE_COMPAT_SYMBOL(CreateMutexA);
    BOOL WINAPI ReleaseMutex(HANDLE mutex) BEACHLIFE_COMPAT_SYMBOL(ReleaseMutex);
    int WINAPI WideCharToMultiByte(UINT code_page,
        DWORD flags,
        LPCWSTR wide,
        int wide_length,
        LPSTR narrow,
        int narrow_size,
        LPCSTR fallback,
        LPBOOL used_fallback) BEACHLIFE_COMPAT_SYMBOL(WideCharToMultiByte);
    int WINAPI MultiByteToWideChar(
        UINT code_page, DWORD flags, LPCSTR narrow, int narrow_length, LPWSTR wide, int wide_size)
        BEACHLIFE_COMPAT_SYMBOL(MultiByteToWideChar);
    BOOL WINAPI UnmapViewOfFile(LPCVOID view) BEACHLIFE_COMPAT_SYMBOL(UnmapViewOfFile);
    LPVOID WINAPI MapViewOfFile(
        HANDLE mapping, DWORD access, DWORD offset_high, DWORD offset_low, SIZE_T bytes)
        BEACHLIFE_COMPAT_SYMBOL(MapViewOfFile);
    HANDLE WINAPI CreateFileMappingA(HANDLE file,
        LPSECURITY_ATTRIBUTES security,
        DWORD protect,
        DWORD size_high,
        DWORD size_low,
        LPCSTR name) BEACHLIFE_COMPAT_SYMBOL(CreateFileMappingA);
    BOOL WINAPI CloseHandle(HANDLE handle) BEACHLIFE_COMPAT_SYMBOL(CloseHandle);
    HFILE WINAPI OpenFile(LPCSTR path, LPOFSTRUCT info, UINT style)
        BEACHLIFE_COMPAT_SYMBOL(OpenFile);
    void WINAPI Sleep(DWORD milliseconds) BEACHLIFE_COMPAT_SYMBOL(Sleep);
    DWORD WINAPI GetLastError(void) BEACHLIFE_COMPAT_SYMBOL(GetLastError);
    void WINAPI SetLastError(DWORD error) BEACHLIFE_COMPAT_SYMBOL(SetLastError);
    HANDLE WINAPI LocalFree(HANDLE memory) BEACHLIFE_COMPAT_SYMBOL(LocalFree);
    LONG WINAPI InterlockedExchange(LONG* target, LONG value)
        BEACHLIFE_COMPAT_SYMBOL(InterlockedExchange);
    int WINAPI MulDiv(int number, int numerator, int denominator) BEACHLIFE_COMPAT_SYMBOL(MulDiv);
    DWORD WINAPI FormatMessageA(DWORD flags,
        LPCVOID source,
        DWORD message,
        DWORD language,
        LPSTR buffer,
        DWORD size,
        va_list* arguments) BEACHLIFE_COMPAT_SYMBOL(FormatMessageA);

    // USER32
    BOOL WINAPI PeekMessageA(LPMSG message, HWND window, UINT first, UINT last, UINT remove)
        BEACHLIFE_COMPAT_SYMBOL(PeekMessageA);
    BOOL WINAPI TranslateMessage(const MSG* message) BEACHLIFE_COMPAT_SYMBOL(TranslateMessage);
    LRESULT WINAPI DispatchMessageA(const MSG* message) BEACHLIFE_COMPAT_SYMBOL(DispatchMessageA);
    void WINAPI PostQuitMessage(int exit_code) BEACHLIFE_COMPAT_SYMBOL(PostQuitMessage);
    LRESULT WINAPI DefWindowProcA(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        BEACHLIFE_COMPAT_SYMBOL(DefWindowProcA);
    ATOM WINAPI RegisterClassExA(const WNDCLASSEXA* window_class)
        BEACHLIFE_COMPAT_SYMBOL(RegisterClassExA);
    BOOL WINAPI UnregisterClassA(LPCSTR name, HINSTANCE instance)
        BEACHLIFE_COMPAT_SYMBOL(UnregisterClassA);
    HWND WINAPI CreateWindowExA(DWORD extended_style,
        LPCSTR class_name,
        LPCSTR title,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        HWND parent,
        HMENU menu,
        HINSTANCE instance,
        LPVOID parameter) BEACHLIFE_COMPAT_SYMBOL(CreateWindowExA);
    BOOL WINAPI DestroyWindow(HWND window) BEACHLIFE_COMPAT_SYMBOL(DestroyWindow);
    BOOL WINAPI ShowWindow(HWND window, int command) BEACHLIFE_COMPAT_SYMBOL(ShowWindow);
    BOOL WINAPI UpdateWindow(HWND window) BEACHLIFE_COMPAT_SYMBOL(UpdateWindow);
    BOOL WINAPI SetWindowPos(
        HWND window, HWND after, int x, int y, int width, int height, UINT flags)
        BEACHLIFE_COMPAT_SYMBOL(SetWindowPos);
    BOOL WINAPI AdjustWindowRectEx(LPRECT rectangle, DWORD style, BOOL menu, DWORD extended_style)
        BEACHLIFE_COMPAT_SYMBOL(AdjustWindowRectEx);
    BOOL WINAPI GetClientRect(HWND window, LPRECT rectangle) BEACHLIFE_COMPAT_SYMBOL(GetClientRect);
    BOOL WINAPI GetWindowRect(HWND window, LPRECT rectangle) BEACHLIFE_COMPAT_SYMBOL(GetWindowRect);
    BOOL WINAPI ClientToScreen(HWND window, LPPOINT point) BEACHLIFE_COMPAT_SYMBOL(ClientToScreen);
    HMENU WINAPI GetMenu(HWND window) BEACHLIFE_COMPAT_SYMBOL(GetMenu);
    LONG WINAPI GetWindowLongA(HWND window, int index) BEACHLIFE_COMPAT_SYMBOL(GetWindowLongA);
    HDC WINAPI GetDC(HWND window) BEACHLIFE_COMPAT_SYMBOL(GetDC);
    int WINAPI ReleaseDC(HWND window, HDC context) BEACHLIFE_COMPAT_SYMBOL(ReleaseDC);
    int WINAPI GetSystemMetrics(int index) BEACHLIFE_COMPAT_SYMBOL(GetSystemMetrics);
    BOOL WINAPI SystemParametersInfoA(UINT action, UINT parameter, PVOID value, UINT flags)
        BEACHLIFE_COMPAT_SYMBOL(SystemParametersInfoA);
    SHORT WINAPI GetAsyncKeyState(int key) BEACHLIFE_COMPAT_SYMBOL(GetAsyncKeyState);
    HANDLE WINAPI LoadImageA(
        HINSTANCE instance, LPCSTR name, UINT type, int width, int height, UINT flags)
        BEACHLIFE_COMPAT_SYMBOL(LoadImageA);
    HICON WINAPI LoadIconA(HINSTANCE instance, LPCSTR name) BEACHLIFE_COMPAT_SYMBOL(LoadIconA);
    HCURSOR WINAPI SetCursor(HCURSOR cursor) BEACHLIFE_COMPAT_SYMBOL(SetCursor);
    BOOL WINAPI SetCursorPos(int x, int y) BEACHLIFE_COMPAT_SYMBOL(SetCursorPos);
    BOOL WINAPI GetCursorPos(LPPOINT point) BEACHLIFE_COMPAT_SYMBOL(GetCursorPos);
    BOOL WINAPI ClipCursor(const RECT* rectangle) BEACHLIFE_COMPAT_SYMBOL(ClipCursor);
    int WINAPI ShowCursor(BOOL show) BEACHLIFE_COMPAT_SYMBOL(ShowCursor);
    BOOL WINAPI SetRect(LPRECT rectangle, int left, int top, int right, int bottom)
        BEACHLIFE_COMPAT_SYMBOL(SetRect);
    int WINAPI MessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type)
        BEACHLIFE_COMPAT_SYMBOL(MessageBoxA);

    // GDI32
    HBITMAP WINAPI CreateDIBSection(
        HDC context, const BITMAPINFO* info, UINT usage, void** bits, HANDLE section, DWORD offset)
        BEACHLIFE_COMPAT_SYMBOL(CreateDIBSection);
    int WINAPI AddFontResourceA(LPCSTR file) BEACHLIFE_COMPAT_SYMBOL(AddFontResourceA);
    BOOL WINAPI RemoveFontResourceA(LPCSTR file) BEACHLIFE_COMPAT_SYMBOL(RemoveFontResourceA);
    HGDIOBJ WINAPI GetStockObject(int index) BEACHLIFE_COMPAT_SYMBOL(GetStockObject);
    HDC WINAPI CreateCompatibleDC(HDC context) BEACHLIFE_COMPAT_SYMBOL(CreateCompatibleDC);
    int WINAPI StretchDIBits(HDC context,
        int x,
        int y,
        int width,
        int height,
        int source_x,
        int source_y,
        int source_width,
        int source_height,
        const void* bits,
        const BITMAPINFO* info,
        UINT usage,
        DWORD operation) BEACHLIFE_COMPAT_SYMBOL(StretchDIBits);
    int WINAPI SetMapMode(HDC context, int mode) BEACHLIFE_COMPAT_SYMBOL(SetMapMode);
    int WINAPI GetDeviceCaps(HDC context, int index) BEACHLIFE_COMPAT_SYMBOL(GetDeviceCaps);
    HFONT WINAPI CreateFontA(int height,
        int width,
        int escapement,
        int orientation,
        int weight,
        DWORD italic,
        DWORD underline,
        DWORD strike_out,
        DWORD char_set,
        DWORD output_precision,
        DWORD clip_precision,
        DWORD quality,
        DWORD pitch_and_family,
        LPCSTR face) BEACHLIFE_COMPAT_SYMBOL(CreateFontA);
    HGDIOBJ WINAPI SelectObject(HDC context, HGDIOBJ object) BEACHLIFE_COMPAT_SYMBOL(SelectObject);
    COLORREF WINAPI SetTextColor(HDC context, COLORREF colour)
        BEACHLIFE_COMPAT_SYMBOL(SetTextColor);
    COLORREF WINAPI SetBkColor(HDC context, COLORREF colour) BEACHLIFE_COMPAT_SYMBOL(SetBkColor);
    UINT WINAPI SetTextAlign(HDC context, UINT align) BEACHLIFE_COMPAT_SYMBOL(SetTextAlign);
    BOOL WINAPI GetTextExtentPoint32A(HDC context, LPCSTR text, int length, LPSIZE size)
        BEACHLIFE_COMPAT_SYMBOL(GetTextExtentPoint32A);
    BOOL WINAPI ExtTextOutA(HDC context,
        int x,
        int y,
        UINT options,
        const RECT* rectangle,
        LPCSTR text,
        UINT length,
        const INT* spacing) BEACHLIFE_COMPAT_SYMBOL(ExtTextOutA);
    DWORD WINAPI GetKerningPairsA(HDC context, DWORD count, LPKERNINGPAIR pairs)
        BEACHLIFE_COMPAT_SYMBOL(GetKerningPairsA);
    BOOL WINAPI DeleteObject(HGDIOBJ object) BEACHLIFE_COMPAT_SYMBOL(DeleteObject);
    BOOL WINAPI DeleteDC(HDC context) BEACHLIFE_COMPAT_SYMBOL(DeleteDC);

// OLE32
#ifdef __cplusplus
    HRESULT WINAPI CoCreateInstance(
        REFCLSID class_id, LPUNKNOWN outer, DWORD context, REFIID interface_id, LPVOID* object)
        BEACHLIFE_COMPAT_SYMBOL(CoCreateInstance);
#endif
    HRESULT WINAPI CoInitialize(LPVOID reserved) BEACHLIFE_COMPAT_SYMBOL(CoInitialize);
    void WINAPI CoUninitialize(void) BEACHLIFE_COMPAT_SYMBOL(CoUninitialize);

#ifdef __cplusplus
}
#endif

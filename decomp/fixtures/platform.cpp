#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

typedef char PointerSizeIsFour[sizeof(void*) == 4 ? 1 : -1];
typedef char WindowClassSizeIs48[sizeof(WNDCLASSEXA) == 48 ? 1 : -1];
typedef char MessageSizeIs28[sizeof(MSG) == 28 ? 1 : -1];

// Synthetic compile/link fixture only. This executable must never be run.
static WNDCLASSEXA generated_class;
static MSG generated_message;
static volatile DWORD observed;

extern "C" void fixture_entry()
{
    HINSTANCE instance = 0;
    HANDLE mutex = CreateMutexA(0, FALSE, "generated-mutex");
    observed = GetLastError();
    generated_class.cbSize = sizeof(generated_class);
    generated_class.hInstance = instance;
    generated_class.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
    generated_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    generated_class.lpszClassName = "generated-class";
    observed = RegisterClassExA(&generated_class);
    observed = GetSystemMetrics(SM_CXSCREEN);
    HWND window = CreateWindowExA(0, generated_class.lpszClassName, "generated-title", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, 0, 0, instance, 0);
    observed = ShowWindow(window, SW_SHOWNORMAL);
    observed = UpdateWindow(window);
    observed = CoInitialize(0);
    observed = PeekMessageA(&generated_message, 0, 0, 0, PM_REMOVE);
    observed = TranslateMessage(&generated_message);
    observed = DispatchMessageA(&generated_message);
    CoUninitialize();
    observed = UnregisterClassA(generated_class.lpszClassName, instance);
    observed = ReleaseMutex(mutex);
    observed = CloseHandle(mutex);
}

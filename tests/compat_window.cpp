#include "platform/compat/user32_host.h"
#include "platform/video.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
void require(bool condition, const char* detail)
{
    if (!condition)
    {
        std::fprintf(stderr, "Window messages: %s (%s)\n", detail, SDL_GetError());
        std::abort();
    }
}

class TestVideo final : public beachlife::platform::Video
{
  public:
    SDL_Window* handle = nullptr;
    beachlife::platform::VideoInfo metadata;
    SDL_Window* window() const override
    {
        return handle;
    }
    const beachlife::platform::VideoInfo& info() const override
    {
        return metadata;
    }
    void new_overlay_frame() override
    {
        std::abort();
    }
    void present(const beachlife::platform::Frame&, ImDrawData*) override
    {
        std::abort();
    }
    void save_next(const std::filesystem::path&) override
    {
        std::abort();
    }
};

std::vector<UINT> dispatched;
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM, LPARAM)
{
    dispatched.push_back(message);
    if (message == WM_CLOSE)
    {
        require(DestroyWindow(window), "destroy the logical window from its callback");
        PostQuitMessage(23);
    }
    return 17;
}

void prepare(void* context)
{
    ++*static_cast<unsigned int*>(context);
}

void check_session(TestVideo& video, bool application_quit)
{
    const auto handle = reinterpret_cast<HWND>(&video);
    const auto id = SDL_GetWindowID(video.handle);
    unsigned int polls = 0;
    beachlife::compat::WindowSession session(handle, prepare, &polls, true);
    const auto instance = GetModuleHandleA(nullptr);
    WNDCLASSEXA definition{};
    definition.cbSize = sizeof(definition);
    definition.hInstance = instance;
    definition.lpfnWndProc = procedure;
    definition.lpszClassName = "Beach Life";
    definition.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(104));
    require(definition.hIcon && definition.hIcon == LoadIconA(instance, MAKEINTRESOURCEA(104)),
        "borrow the application's shared icon");
    require(RegisterClassExA(&definition) != 0, "register a fresh class");
    require(RegisterClassExA(&definition) == 0 && GetLastError() == 1410,
        "reject duplicate registration");
    require(CreateWindowExA(WS_EX_TOPMOST,
                "Beach Life",
                "Beach Life",
                WS_POPUP,
                0,
                0,
                800,
                600,
                nullptr,
                nullptr,
                instance,
                nullptr) == handle,
        "reuse Play's window");
    ShowWindow(handle, 1);
    require(SDL_GetWindowFlags(video.handle) & SDL_WINDOW_HIDDEN, "preserve hidden automation");
    int width = 0, height = 0;
    SDL_GetWindowSize(video.handle, &width, &height);
    require(width == 64 && height == 48, "preserve host geometry");
    require(UpdateWindow(handle), "update the GPU window without a GDI region");
    require(
        DefWindowProcA(handle, 0x104, 0xa4, 0) == 0 && DefWindowProcA(handle, 0x106, 'x', 0) == 0,
        "accept Alt and system characters when the host has no menu");

    MSG message{};
    require(PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE) && message.message == WM_ACTIVATEAPP &&
                message.hwnd == handle && polls == 1,
        "deliver initial activation and run the frame hook");
    require(DispatchMessageA(&message) == 17, "dispatch the registered procedure");
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE))
    {
    }
    dispatched.clear();

    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = id;
    require(SDL_PushEvent(&event), "push focus loss");
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    require(SDL_PushEvent(&event), "push focus gain");
    require(PeekMessageA(&message, handle, WM_ACTIVATEAPP, WM_ACTIVATEAPP, 0) && !message.wParam,
        "peek retains the first message");
    require(PeekMessageA(&message, handle, WM_ACTIVATEAPP, WM_ACTIVATEAPP, PM_REMOVE) &&
                !message.wParam,
        "consume only the first focus message");
    DispatchMessageA(&message);
    require(
        PeekMessageA(&message, handle, WM_ACTIVATEAPP, WM_ACTIVATEAPP, PM_REMOVE) && message.wParam,
        "retain the second focus message in order");
    DispatchMessageA(&message);

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = id;
    event.key.scancode = SDL_SCANCODE_A;
    event.key.down = true;
    require(SDL_PushEvent(&event), "push a keyboard event");
    require(PeekMessageA(&message, handle, 0x100, 0x100, PM_REMOVE) && message.wParam == 'A' &&
                TranslateMessage(&message),
        "translate a key through the message queue");
    DispatchMessageA(&message);
    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = id;
    event.text.text = "a";
    require(SDL_PushEvent(&event), "push layout-translated text");
    require(PeekMessageA(&message, handle, 0x102, 0x102, PM_REMOVE) && message.wParam == 'a',
        "deliver translated character data");
    DispatchMessageA(&message);

    event = {};
    event.type = application_quit ? SDL_EVENT_QUIT : SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    if (!application_quit)
    {
        event.window.windowID = id;
    }
    require(SDL_PushEvent(&event), "push a close request");
    require(PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE) && message.message == WM_CLOSE,
        "send host quit through the game's close handler");
    DispatchMessageA(&message);
    require(dispatched.back() == 2, "notify destruction synchronously");
    require(PeekMessageA(&message, handle, 0x100, 0x100, PM_REMOVE) && message.message == WM_QUIT &&
                message.wParam == 23 && !message.hwnd,
        "deliver PostQuitMessage on the next poll even through a filter");
    const auto count = dispatched.size();
    require(DispatchMessageA(&message) == 0 && dispatched.size() == count,
        "do not dispatch thread quit messages");
    require(SDL_GetWindowFromID(id) == video.handle, "retain Video for the final game render");
    require(!UnregisterClassA("app", instance) && GetLastError() == 1411,
        "preserve retail's mismatched unregister failure");
    // Leave the class registered: WindowSession must clear it before the next Play.
}
} // namespace

int main()
{
    require(CoInitialize(nullptr) == S_OK && CoInitialize(nullptr) == 1,
        "track per-thread COM initialization");
    CoUninitialize();
    CoUninitialize();
    require(CoInitialize(nullptr) == S_OK, "balance COM teardown");
    CoUninitialize();
    for (bool application_quit : {false, true})
    {
        require(SDL_Init(SDL_INIT_VIDEO), "initialize headless video");
        TestVideo video;
        video.handle = SDL_CreateWindow("Message checks", 64, 48, SDL_WINDOW_HIDDEN);
        require(video.handle, "create the host window");
        check_session(video, application_quit);
        SDL_DestroyWindow(video.handle);
        SDL_Quit();
    }
    std::puts("Window registration, message ordering and lifetime checks passed");
}

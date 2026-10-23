#define DIRECTINPUT_VERSION 0x0800
#include "dinput.h"

#include "platform/compat/input_map.h"
#include "platform/video.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
void require(bool condition, const char* detail)
{
    if (!condition)
    {
        std::fprintf(stderr, "DirectInput: %s (%s)\n", detail, SDL_GetError());
        std::abort();
    }
}

class TestVideo final : public beachlife::platform::Video
{
  public:
    SDL_Window* handle = nullptr;
    beachlife::platform::VideoInfo metadata;
    bool stretched = false;
    bool stretch() const override
    {
        return stretched;
    }

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

void push(SDL_Event event)
{
    require(SDL_PushEvent(&event), "push generated event");
}

void focus(SDL_WindowID window, bool gained)
{
    SDL_Event event{};
    event.type = gained ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = window;
    push(event);
}

void key(SDL_WindowID window, SDL_Scancode code, bool down, bool repeat = false)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = window;
    event.key.scancode = code;
    event.key.key = SDLK_A; // Deliberately unrelated to the physical key.
    event.key.down = down;
    event.key.repeat = repeat;
    push(event);
}

void motion(SDL_WindowID window, float x, float y)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window;
    event.motion.x = 16 + x;
    event.motion.y = 16 + y;
    event.motion.xrel = x;
    event.motion.yrel = y;
    push(event);
}

void wheel(SDL_WindowID window, float amount, bool flipped = false)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = window;
    event.wheel.y = amount;
    event.wheel.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
    push(event);
}

void button(SDL_WindowID window, Uint8 index, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = window;
    event.button.button = index;
    event.button.down = down;
    push(event);
}

void buffer_size(IDirectInputDeviceA& device, DWORD size)
{
    DIPROPDWORD property{{sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}, size};
    require(device.SetProperty(DIPROP_BUFFERSIZE, &property.diph) == DI_OK, "set buffer size");
}

struct RetailEvent
{
    DWORD offset, data, time, sequence, application;
};
static_assert(sizeof(RetailEvent) == 20);
static_assert(sizeof(DIMOUSESTATE2) == 20);
static_assert(sizeof(LONG) == 4 && sizeof(DWORD) == 4);

void keyboard_conversion(IDirectInputDeviceA& device, SDL_WindowID window)
{
    key(window, SDL_SCANCODE_Q, true);
    key(window, SDL_SCANCODE_Q, true, true);
    key(window, SDL_SCANCODE_RCTRL, true);
    key(window, SDL_SCANCODE_Q, false);
    std::array<BYTE, 256> state{};
    require(device.GetDeviceState(state.size(), state.data()) == DI_OK, "read keyboard");
    require(!state[DIK_Q] && state[DIK_RCONTROL] == 0x80 && !state[DIK_LCONTROL] && !state[DIK_A],
        "convert physical keys and extended scan codes, ignoring layout");

    struct
    {
        std::array<RetailEvent, 4> events;
        DWORD guard;
    } data;
    std::memset(&data, 0xa5, sizeof(data));
    DWORD count = 4;
    require(
        device.GetDeviceData(
            20, reinterpret_cast<DIDEVICEOBJECTDATA*>(data.events.data()), &count, DIGDD_PEEK) ==
                DI_OK &&
            count == 3,
        "peek packed keyboard records without autorepeat");
    require(data.events[0].offset == DIK_Q && data.events[0].data == 0x80 &&
                data.events[1].offset == DIK_RCONTROL && data.events[2].offset == DIK_Q &&
                !data.events[2].data && !data.events[2].application && data.guard == 0xa5a5a5a5,
        "write 20-byte records without a native stride or out-of-bounds write");
    require(data.events[0].sequence < data.events[1].sequence &&
                data.events[1].sequence < data.events[2].sequence,
        "order buffered transitions");

    std::array<DWORD, 12> dx3{};
    unsigned long native_count = 3;
    require(device.GetDeviceData(
                16, reinterpret_cast<DIDEVICEOBJECTDATA*>(dx3.data()), &native_count, 0) == DI_OK &&
                native_count == 3,
        "convert host-sized count and DX3 record stride");
    require(dx3[0] == DIK_Q && dx3[4] == DIK_RCONTROL && dx3[8] == DIK_Q,
        "preserve DX3 object offsets");
    count = 4;
    require(device.GetDeviceData(20, nullptr, &count, DIGDD_PEEK) == DI_OK && !count,
        "consume previously peeked records");
    require(SDL_HasEvent(SDL_EVENT_KEY_DOWN), "leave SDL events queued for the host window");
}

void mouse_conversion(IDirectInputDeviceA& device, SDL_WindowID window)
{
    motion(window, 1.5f, -2.25f);
    motion(window, 0.5f, -0.75f);
    wheel(window, 0.25f);
    wheel(window, 0.5f, true);
    button(window, SDL_BUTTON_RIGHT, true);
    button(window, SDL_BUTTON_MIDDLE, true);
    button(window, SDL_BUTTON_X1, true);
    button(window, SDL_BUTTON_RIGHT, false);
    std::array<RetailEvent, 12> records{};
    DWORD count = records.size();
    require(device.GetDeviceData(
                20, reinterpret_cast<DIDEVICEOBJECTDATA*>(records.data()), &count, 0) == DI_OK &&
                count == 10,
        "read mouse motion, wheel, and button transitions");
    require(records[0].offset == DIMOFS_X && records[0].data == 1 &&
                records[1].offset == DIMOFS_Y && static_cast<LONG>(records[1].data) == -2 &&
                records[2].data == 1 && static_cast<LONG>(records[3].data) == -1,
        "accumulate fractional relative motion and preserve negative axes");
    require(records[4].offset == DIMOFS_Z && records[4].data == 30 &&
                static_cast<LONG>(records[5].data) == -60,
        "convert wheel ticks to 120 units with flipped direction");
    require(records[6].offset == DIMOFS_BUTTON1 && records[6].data == 0x80 &&
                records[7].offset == DIMOFS_BUTTON2 && records[8].offset == DIMOFS_BUTTON3 &&
                records[9].offset == DIMOFS_BUTTON1 && records[9].data == 0,
        "map SDL right/middle buttons to DirectInput order");
    require(records[0].sequence == records[1].sequence,
        "give objects from one motion event the same sequence");
    DIMOUSESTATE2 state{};
    require(device.GetDeviceState(sizeof(state), &state) == DI_OK && state.lX == 2 &&
                state.lY == -3 && state.lZ == -30 && !state.rgbButtons[1] &&
                state.rgbButtons[2] == 0x80 && state.rgbButtons[3] == 0x80,
        "keep immediate mouse state independent from buffered reads");
    require(device.GetDeviceState(sizeof(state), &state) == DI_OK && !state.lX && !state.lY &&
                !state.lZ && state.rgbButtons[2] == 0x80,
        "reset relative axes after an immediate read and keep buttons held");
}

void mapped_mouse(IDirectInputDeviceA& device, TestVideo& video)
{
    using beachlife::compat::input::Mapping;
    using beachlife::compat::input::mapping;
    const HWND handle = reinterpret_cast<HWND>(&video);
    const SDL_WindowID window = SDL_GetWindowID(video.handle);
    require(SDL_SetWindowSize(video.handle, 1920, 1080), "resize generated host window");
    video.set_game_mode(800, 600, true);
    Mapping map;
    require(mapping(handle, map), "query presentation mapping before the first frame");
    require(map.presentation.destination.x == 240 && map.presentation.destination.y == 0 &&
                map.presentation.destination.w == 1440 && map.presentation.destination.h == 1080,
        "use the GPU's pillarbox viewport");
    RECT client{};
    POINT origin{}, bottom{800, 600};
    require(GetClientRect(handle, &client) && client.right == 800 && client.bottom == 600 &&
                ClientToScreen(handle, &origin) && origin.x == 0 && origin.y == 0 &&
                ClientToScreen(handle, &bottom) && bottom.x == 800 && bottom.y == 600,
        "emulated fullscreen exposes a zero-based logical Windows screen");
    const auto center = map.from_window(960, 540);
    require(center.x == 400 && center.y == 300, "visible button center maps to its game pixel");
    const auto first = map.from_window(-100, -100);
    const auto last = map.from_window(1920, 1080);
    require(first.x == 0 && first.y == 0 && last.x == 799 && last.y == 599,
        "clamp letterboxes and the exclusive client edges");

    // A generated SDL event reaches both immediate and buffered DirectInput.
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window;
    event.motion.x = 960;
    event.motion.y = 540;
    event.motion.xrel = 18;
    event.motion.yrel = -9;
    push(event);
    DIMOUSESTATE2 state{};
    require(
        device.GetDeviceState(sizeof(state), &state) == DI_OK && state.lX == 10 && state.lY == -5,
        "scale immediate DirectInput axes to logical pixels");
    std::array<DIDEVICEOBJECTDATA, 8> events{};
    DWORD count = events.size();
    require(device.GetDeviceData(sizeof(events[0]), events.data(), &count, 0) == DI_OK &&
                count == 2 && events[0].dwData == 10 && static_cast<LONG>(events[1].dwData) == -5,
        "scale buffered DirectInput axes through the same mapping");

    event.motion.xrel = 0.5625f; // 0.3125 logical pixels per generated event
    event.motion.yrel = 0;
    for (int i = 0; i < 3; ++i)
    {
        push(event);
    }
    require(device.GetDeviceState(sizeof(state), &state) == DI_OK && !state.lX,
        "retain subpixel motion through an immediate read");
    push(event);
    require(device.GetDeviceState(sizeof(state), &state) == DI_OK && state.lX == 1,
        "accumulate scaled fractions without losing small movements");
    count = events.size();
    require(device.GetDeviceData(sizeof(events[0]), events.data(), &count, 0) == DI_OK &&
                count == 1 && events[0].dwData == 1,
        "buffer the same accumulated logical movement");

    const RECT clip{100, 50, 200, 150};
    map.clip = clip;
    const auto physical = map.window_clip();
    require(physical.x == 420 && physical.y == 90 && physical.w == 180 && physical.h == 180,
        "map the clip to the presented host rectangle");
    const auto clipped = map.from_window(1920, 1080);
    require(clipped.x == 199 && clipped.y == 149, "clip has exclusive right and bottom bounds");
    const auto clipped_motion = map.delta(610, 180, 40, 0);
    require(std::abs(clipped_motion.x - (199 - (570 - 240) / 1.8)) < 1e-8,
        "clamp relative DirectInput motion at the same clip edge");
    const bool native_clip = ClipCursor(&clip);
    require(native_clip || std::strcmp(SDL_GetCurrentVideoDriver(), "dummy") == 0,
        "apply native clipping when the SDL driver supports it");
    require(mapping(handle, map), "keep mapping valid after a rejected native clip");
    const int expected_x = native_clip ? 199 : 799;
    const int expected_y = native_clip ? 50 : 0;
    require(SDL_ShowWindow(video.handle), "show the generated window for mouse focus");
    require(SetCursorPos(900, -50), "warp using game screen coordinates");
    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    const auto expected = map.presentation.to_window(expected_x + 0.5, expected_y + 0.5);
    require(std::abs(mx - expected.x) < 0.01 && std::abs(my - expected.y) < 0.01,
        "clamp the warp and map into the requested logical pixel");
    POINT cursor{};
    require(GetCursorPos(&cursor) && cursor.x == expected_x && cursor.y == expected_y,
        "GetCursorPos and SetCursorPos round trip through one transform");
    const RECT empty{20, 20, 20, 30};
    require(!ClipCursor(&empty), "reject empty clip rectangles");
    require(
        ClipCursor(nullptr) && !SDL_GetWindowMouseRect(video.handle), "release the cursor clip");

    video.stretched = true;
    require(mapping(handle, map) && map.presentation.destination.w == 1920 &&
                map.presentation.destination.x == 0,
        "stretch uses independent axis scales");
    auto position = map.from_window(480, 270);
    require(position.x == 200 && position.y == 150, "map a stretched button position");
    video.stretched = false;
    require(SDL_SetWindowSize(video.handle, 1000, 1000), "resize to a letterboxed square");
    video.set_game_mode(640, 480, false);
    require(SDL_SetWindowPosition(video.handle, -300, 70) && mapping(handle, map),
        "move the windowed logical client");
    require(map.presentation.destination.y == 125 && map.presentation.destination.h == 750 &&
                map.origin.x == -300 && map.origin.y == 70,
        "refresh mode, bars, and window origin without a frame");
    origin = {320, 240};
    require(ClientToScreen(handle, &origin) && origin.x == 20 && origin.y == 310 &&
                SetCursorPos(origin.x, origin.y) && GetCursorPos(&cursor) && cursor.x == origin.x &&
                cursor.y == origin.y,
        "windowed screen coordinates retain the desktop origin in logical units");

    // Generated drawable/window dimensions exercise density without a monitor.
    map.presentation = {800, 600, 1000, 700, 2000, 1400, {66, 0, 1867, 1400}, true};
    map.clip = {0, 0, 800, 600};
    position = map.presentation.to_window(400.5, 300.5);
    const auto logical = map.from_window(position.x, position.y);
    require(std::abs(logical.x - 400.5) < 1e-8 && std::abs(logical.y - 300.5) < 1e-8,
        "preserve a logical pixel under high DPI and nonintegral viewport rounding");
    const auto odd = beachlife::platform::frame_rect(800, 600, 2000, 1400, false);
    require(odd.x == 66 && odd.w == 1867, "share the presenter's exact odd viewport rounding");
    video.set_game_mode(0, 0, false);
    require(SDL_SetWindowSize(video.handle, 32, 32), "restore input fixture dimensions");
}

void overflow_and_focus(IDirectInputDeviceA& device, SDL_WindowID window)
{
    require(device.Unacquire() == DI_OK, "unacquire before resizing buffer");
    buffer_size(device, 4); // DirectInput's ring reserves one slot.
    require(device.Acquire() == DI_OK, "reacquire after resizing");
    for (int index = 0; index < 5; ++index)
    {
        motion(window, static_cast<float>(index + 1), 0);
    }
    std::array<DIDEVICEOBJECTDATA, 4> records{};
    DWORD count = records.size();
    require(device.GetDeviceData(sizeof(DIDEVICEOBJECTDATA), records.data(), &count, DIGDD_PEEK) ==
                    DI_BUFFEROVERFLOW &&
                count == 3,
        "signal overflow while preserving the oldest buffered events");
    require(records[0].dwData == 1 && records[1].dwData == 2 && records[2].dwData == 3,
        "keep event order through buffer overflow");
    count = records.size();
    require(device.GetDeviceData(sizeof(DIDEVICEOBJECTDATA), records.data(), &count, 0) ==
                    DI_BUFFEROVERFLOW &&
                count == 3,
        "peek retains the overflow indication");
    count = records.size();
    require(device.GetDeviceData(sizeof(DIDEVICEOBJECTDATA), nullptr, &count, 0) == DI_OK && !count,
        "clear overflow after a consuming read");
    motion(window, 1, 0);
    focus(window, false);
    DIMOUSESTATE2 state{};
    require(device.GetDeviceState(sizeof(state), &state) == DIERR_INPUTLOST,
        "report input lost on focus loss");
    count = records.size();
    require(device.GetDeviceData(sizeof(DIDEVICEOBJECTDATA), records.data(), &count, 0) ==
                    DIERR_INPUTLOST &&
                !count,
        "clear the output count when input is lost");
    require(device.Acquire() == DIERR_OTHERAPPHASPRIO, "reject background reacquisition promptly");
    motion(window, 20, 0);
    focus(window, true);
    require(device.GetDeviceState(sizeof(state), &state) == DIERR_INPUTLOST,
        "focus gain still requires acquisition");
    require(device.Acquire() == DI_OK && device.GetDeviceState(sizeof(state), &state) == DI_OK &&
                !state.lX && !state.lY && !state.lZ,
        "discard stale motion when reacquiring");
    count = records.size();
    require(
        device.GetDeviceData(sizeof(DIDEVICEOBJECTDATA), nullptr, &count, DIGDD_PEEK) == DI_OK &&
            !count,
        "discard stale buffered events when reacquiring");
}
} // namespace

int main()
{
    IDirectInput8A* input = nullptr;
    require(DirectInput8Create(
                nullptr, 0x0800, IID_IDirectInput8A, reinterpret_cast<void**>(&input), nullptr) ==
                    DIERR_NOTINITIALIZED &&
                !input,
        "require initialized SDL video");
    require(SDL_Init(SDL_INIT_VIDEO), "initialize SDL video");
    TestVideo video;
    video.handle = SDL_CreateWindow("DirectInput conversion checks", 32, 32, SDL_WINDOW_HIDDEN);
    require(video.handle, "create SDL test window");
    const SDL_WindowID window = SDL_GetWindowID(video.handle);
    require(DirectInput8Create(
                nullptr, 0x0800, IID_IDirectInput8A, reinterpret_cast<void**>(&input), nullptr) ==
                DI_OK,
        "create DirectInput8");
    void* queried = nullptr;
    require(input->QueryInterface(IID_IDirectInput8A, &queried) == DI_OK && queried == input &&
                input->Release() == 1,
        "preserve COM identity and reference count");
    IDirectInputDeviceA* keyboard = nullptr;
    IDirectInputDeviceA* mouse = nullptr;
    require(input->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr) == DI_OK &&
                input->CreateDevice(GUID_SysMouse, &mouse, nullptr) == DI_OK,
        "create system keyboard and mouse");
    require(keyboard->SetDataFormat(&c_dfDIKeyboard) == DI_OK &&
                mouse->SetDataFormat(&c_dfDIMouse2) == DI_OK,
        "select stock device data formats");
    const HWND handle = reinterpret_cast<HWND>(&video);
    require(keyboard->SetCooperativeLevel(
                handle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY) == DI_OK &&
                mouse->SetCooperativeLevel(handle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE) == DI_OK,
        "use the game's foreground cooperative levels");
    buffer_size(*keyboard, 4);
    buffer_size(*mouse, 60);
    focus(window, true);
    require(keyboard->Acquire() == DI_OK && mouse->Acquire() == DI_OK,
        "acquire both foreground devices");
    require(keyboard->SetDataFormat(&c_dfDIKeyboard) == DIERR_ACQUIRED,
        "reject format changes on an acquired device");
    DIDEVICEOBJECTINSTANCEA object{};
    object.dwSize = sizeof(object);
    require(mouse->GetObjectInfo(&object, DIMOFS_Z, DIPH_BYOFFSET) == DI_OK &&
                object.dwOfs == DIMOFS_Z &&
                !std::memcmp(&object.guidType, &GUID_ZAxis, sizeof(GUID)),
        "report the system mouse wheel axis");
    keyboard_conversion(*keyboard, window);
    mouse_conversion(*mouse, window);
    mapped_mouse(*mouse, video);
    overflow_and_focus(*mouse, window);
    require(keyboard->Acquire() == DI_OK && keyboard->Unacquire() == DI_OK &&
                !SDL_GetWindowKeyboardGrab(video.handle),
        "restore the keyboard grab after focus loss and reacquisition");
    SDL_DestroyWindow(video.handle);
    DIMOUSESTATE2 state{};
    require(mouse->GetDeviceState(sizeof(state), &state) == DIERR_INPUTLOST,
        "lose acquisition when the host window is destroyed");
    require(keyboard->Release() == 0 && mouse->Release() == 0 && input->Release() == 0,
        "release watchers and COM objects after window destruction");
    SDL_Quit();
    std::puts("DirectInput conversion and buffered-input checks passed");
}

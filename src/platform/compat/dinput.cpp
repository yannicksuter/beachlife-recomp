#define DIRECTINPUT_VERSION 0x0800
#include "dinput.h"

#include "compat_host.h"
#include "input_map.h"
#include "platform/video.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

extern "C"
{
    const GUID GUID_SysMouse = {
        0x6f1d2b60, 0xd5a0, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID GUID_SysKeyboard = {
        0x6f1d2b61, 0xd5a0, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID GUID_XAxis = {
        0xa36d02e0, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID GUID_YAxis = {
        0xa36d02e1, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID GUID_ZAxis = {
        0xa36d02e2, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID GUID_Button = {
        0xa36d02f0, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID GUID_Key = {0x55728220, 0xd33c, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID IID_IDirectInput8A = {
        0xbf798030, 0x483a, 0x4da2, {0xaa, 0x99, 0x5d, 0x64, 0xed, 0x36, 0x97, 0}};
    const GUID IID_IDirectInputDeviceA = {
        0x5944e680, 0xc92e, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0}};
    const GUID beachlife_dinput_prop_buffersize = {1, 0, 0, {}};
    const GUID beachlife_dinput_prop_axismode = {2, 0, 0, {}};
}

namespace
{
using beachlife::compat::unsupported;

bool same_guid(REFGUID first, REFGUID second)
{
    return std::memcmp(&first, &second, sizeof(GUID)) == 0;
}

constexpr GUID unknown_iid = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

// DirectInput uses PC set-1 scan codes, including the extended-key high bit;
// SDL uses USB usages. Keycodes would incorrectly depend on keyboard layout.
constexpr auto keyboard_codes()
{
    std::array<BYTE, SDL_SCANCODE_COUNT> codes{};
    constexpr BYTE letters[] = {DIK_A,
        DIK_B,
        DIK_C,
        DIK_D,
        DIK_E,
        DIK_F,
        DIK_G,
        DIK_H,
        DIK_I,
        DIK_J,
        DIK_K,
        DIK_L,
        DIK_M,
        DIK_N,
        DIK_O,
        DIK_P,
        DIK_Q,
        DIK_R,
        DIK_S,
        DIK_T,
        DIK_U,
        DIK_V,
        DIK_W,
        DIK_X,
        DIK_Y,
        DIK_Z};
    for (std::size_t index = 0; index < std::size(letters); ++index)
    {
        codes[SDL_SCANCODE_A + index] = letters[index];
    }
    for (int index = 0; index < 9; ++index)
    {
        codes[SDL_SCANCODE_1 + index] = DIK_1 + index;
    }
    for (int index = 0; index < 10; ++index)
    {
        codes[SDL_SCANCODE_F1 + index] = DIK_F1 + index;
    }
    constexpr std::pair<SDL_Scancode, BYTE> others[] = {{SDL_SCANCODE_0, DIK_0},
        {SDL_SCANCODE_RETURN, DIK_RETURN},
        {SDL_SCANCODE_ESCAPE, DIK_ESCAPE},
        {SDL_SCANCODE_BACKSPACE, DIK_BACK},
        {SDL_SCANCODE_TAB, DIK_TAB},
        {SDL_SCANCODE_SPACE, DIK_SPACE},
        {SDL_SCANCODE_MINUS, DIK_MINUS},
        {SDL_SCANCODE_EQUALS, DIK_EQUALS},
        {SDL_SCANCODE_LEFTBRACKET, DIK_LBRACKET},
        {SDL_SCANCODE_RIGHTBRACKET, DIK_RBRACKET},
        {SDL_SCANCODE_BACKSLASH, DIK_BACKSLASH},
        {SDL_SCANCODE_NONUSHASH, DIK_BACKSLASH},
        {SDL_SCANCODE_SEMICOLON, DIK_SEMICOLON},
        {SDL_SCANCODE_APOSTROPHE, DIK_APOSTROPHE},
        {SDL_SCANCODE_GRAVE, DIK_GRAVE},
        {SDL_SCANCODE_COMMA, DIK_COMMA},
        {SDL_SCANCODE_PERIOD, DIK_PERIOD},
        {SDL_SCANCODE_SLASH, DIK_SLASH},
        {SDL_SCANCODE_CAPSLOCK, DIK_CAPITAL},
        {SDL_SCANCODE_F11, DIK_F11},
        {SDL_SCANCODE_F12, DIK_F12},
        {SDL_SCANCODE_F13, DIK_F13},
        {SDL_SCANCODE_F14, DIK_F14},
        {SDL_SCANCODE_F15, DIK_F15},
        {SDL_SCANCODE_PRINTSCREEN, DIK_SYSRQ},
        {SDL_SCANCODE_SCROLLLOCK, DIK_SCROLL},
        {SDL_SCANCODE_PAUSE, DIK_PAUSE},
        {SDL_SCANCODE_INSERT, DIK_INSERT},
        {SDL_SCANCODE_HOME, DIK_HOME},
        {SDL_SCANCODE_PAGEUP, DIK_PRIOR},
        {SDL_SCANCODE_DELETE, DIK_DELETE},
        {SDL_SCANCODE_END, DIK_END},
        {SDL_SCANCODE_PAGEDOWN, DIK_NEXT},
        {SDL_SCANCODE_RIGHT, DIK_RIGHT},
        {SDL_SCANCODE_LEFT, DIK_LEFT},
        {SDL_SCANCODE_DOWN, DIK_DOWN},
        {SDL_SCANCODE_UP, DIK_UP},
        {SDL_SCANCODE_NUMLOCKCLEAR, DIK_NUMLOCK},
        {SDL_SCANCODE_KP_DIVIDE, DIK_DIVIDE},
        {SDL_SCANCODE_KP_MULTIPLY, DIK_MULTIPLY},
        {SDL_SCANCODE_KP_MINUS, DIK_SUBTRACT},
        {SDL_SCANCODE_KP_PLUS, DIK_ADD},
        {SDL_SCANCODE_KP_ENTER, DIK_NUMPADENTER},
        {SDL_SCANCODE_KP_1, DIK_NUMPAD1},
        {SDL_SCANCODE_KP_2, DIK_NUMPAD2},
        {SDL_SCANCODE_KP_3, DIK_NUMPAD3},
        {SDL_SCANCODE_KP_4, DIK_NUMPAD4},
        {SDL_SCANCODE_KP_5, DIK_NUMPAD5},
        {SDL_SCANCODE_KP_6, DIK_NUMPAD6},
        {SDL_SCANCODE_KP_7, DIK_NUMPAD7},
        {SDL_SCANCODE_KP_8, DIK_NUMPAD8},
        {SDL_SCANCODE_KP_9, DIK_NUMPAD9},
        {SDL_SCANCODE_KP_0, DIK_NUMPAD0},
        {SDL_SCANCODE_KP_PERIOD, DIK_DECIMAL},
        {SDL_SCANCODE_KP_EQUALS, DIK_NUMPADEQUALS},
        {SDL_SCANCODE_KP_COMMA, DIK_NUMPADCOMMA},
        {SDL_SCANCODE_NONUSBACKSLASH, DIK_OEM_102},
        {SDL_SCANCODE_APPLICATION, DIK_APPS},
        {SDL_SCANCODE_POWER, DIK_POWER},
        {SDL_SCANCODE_SLEEP, DIK_SLEEP},
        {SDL_SCANCODE_INTERNATIONAL3, DIK_YEN},
        {SDL_SCANCODE_INTERNATIONAL4, DIK_CONVERT},
        {SDL_SCANCODE_INTERNATIONAL5, DIK_NOCONVERT},
        {SDL_SCANCODE_LANG3, DIK_KANA},
        {SDL_SCANCODE_LCTRL, DIK_LCONTROL},
        {SDL_SCANCODE_RCTRL, DIK_RCONTROL},
        {SDL_SCANCODE_LSHIFT, DIK_LSHIFT},
        {SDL_SCANCODE_RSHIFT, DIK_RSHIFT},
        {SDL_SCANCODE_LALT, DIK_LMENU},
        {SDL_SCANCODE_RALT, DIK_RMENU},
        {SDL_SCANCODE_LGUI, DIK_LWIN},
        {SDL_SCANCODE_RGUI, DIK_RWIN}};
    for (const auto& [scancode, code] : others)
    {
        codes[scancode] = code;
    }
    return codes;
}
constexpr auto scan_codes = keyboard_codes();
std::atomic<DWORD> next_sequence{0};

int mouse_button(Uint8 button)
{
    // SDL numbers left/middle/right; DirectInput numbers left/right/middle.
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    default:
        return button >= SDL_BUTTON_X1 && button <= 8 ? button - 1 : -1;
    }
}

bool same_format(const DIDATAFORMAT& first, const DIDATAFORMAT& second)
{
    if (first.dwFlags != second.dwFlags || first.dwDataSize != second.dwDataSize ||
        first.dwNumObjs != second.dwNumObjs)
    {
        return false;
    }
    for (DWORD index = 0; index < first.dwNumObjs; ++index)
    {
        const auto& a = first.rgodf[index];
        const auto& b = second.rgodf[index];
        if (!a.pguid || !same_guid(*a.pguid, *b.pguid) || a.dwOfs != b.dwOfs ||
            a.dwType != b.dwType || a.dwFlags != b.dwFlags)
        {
            return false;
        }
    }
    return true;
}

bool dword_property(const DIPROPHEADER* header)
{
    return header && header->dwSize == sizeof(DIPROPDWORD) &&
           header->dwHeaderSize == sizeof(DIPROPHEADER) && header->dwHow == DIPH_DEVICE &&
           header->dwObj == 0;
}

unsigned property_number(REFGUID property)
{
    if (&property == &DIPROP_BUFFERSIZE)
    {
        return 1;
    }
    if (&property == &DIPROP_AXISMODE)
    {
        return 2;
    }
    return 0;
}

std::array<DIOBJECTDATAFORMAT, 256> keyboard_objects = []
{
    std::array<DIOBJECTDATAFORMAT, 256> objects{};
    for (DWORD index = 0; index < objects.size(); ++index)
    {
        objects[index] = {&GUID_Key, index, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(index), 0};
    }
    return objects;
}();

std::array<DIOBJECTDATAFORMAT, 11> mouse_objects = []
{
    std::array<DIOBJECTDATAFORMAT, 11> objects{};
    constexpr const GUID* axes[] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis};
    for (DWORD index = 0; index < 3; ++index)
    {
        objects[index] = {axes[index], index * 4, DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(index), 0};
    }
    for (DWORD index = 0; index < 8; ++index)
    {
        objects[index + 3] = {
            &GUID_Button, DIMOFS_BUTTON0 + index, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(index), 0};
    }
    return objects;
}();

class Device final : public IDirectInputDeviceA
{
  public:
    explicit Device(bool keyboard)
        : keyboard_(keyboard)
    {
    }

    bool watch()
    {
        watching_ = SDL_AddEventWatch(event_watch, this);
        return watching_;
    }

    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return DIERR_INVALIDPARAM;
        }
        *output = nullptr;
        if (!same_guid(iid, unknown_iid) && !same_guid(iid, IID_IDirectInputDeviceA))
        {
            return DIERR_NOINTERFACE;
        }
        *output = static_cast<IDirectInputDeviceA*>(this);
        AddRef();
        return DI_OK;
    }

    ULONG AddRef() override
    {
        return ++references_;
    }

    ULONG Release() override
    {
        const ULONG remaining = --references_;
        if (!remaining)
        {
            delete this;
        }
        return remaining;
    }

    HRESULT GetCapabilities(LPDIDEVCAPS) override
    {
        unsupported("IDirectInputDevice::GetCapabilities", "not used by the retail input calls");
    }

    HRESULT EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD) override
    {
        unsupported("IDirectInputDevice::EnumObjects", "not used by the retail input calls");
    }

    HRESULT GetProperty(REFGUID property, LPDIPROPHEADER header) override
    {
        if (!dword_property(header))
        {
            return DIERR_INVALIDPARAM;
        }
        std::lock_guard lock(mutex_);
        auto& value = reinterpret_cast<DIPROPDWORD*>(header)->dwData;
        switch (property_number(property))
        {
        case 1:
            value = static_cast<DWORD>(buffer_.size());
            return DI_OK;
        case 2:
            if (!keyboard_)
            {
                value = DIPROPAXISMODE_REL;
                return DI_OK;
            }
            break;
        }
        unsupported("IDirectInputDevice::GetProperty", "only buffer size and relative mouse axes");
    }

    HRESULT SetProperty(REFGUID property, LPCDIPROPHEADER header) override
    {
        if (!dword_property(header))
        {
            return DIERR_INVALIDPARAM;
        }
        std::lock_guard lock(mutex_);
        if (acquired_)
        {
            return DIERR_ACQUIRED;
        }
        const DWORD value = reinterpret_cast<const DIPROPDWORD*>(header)->dwData;
        switch (property_number(property))
        {
        case 1:
            if (value > buffer_.max_size())
            {
                return DIERR_OUTOFMEMORY;
            }
            try
            {
                buffer_.resize(value);
            }
            catch (const std::bad_alloc&)
            {
                return DIERR_OUTOFMEMORY;
            }
            clear_buffer();
            return DI_OK;
        case 2:
            if (!keyboard_ && value == DIPROPAXISMODE_REL)
            {
                axes_.fill(0);
                fractions_.fill(0);
                return DI_OK;
            }
            break;
        }
        unsupported("IDirectInputDevice::SetProperty", "only buffer size and relative mouse axes");
    }

    HRESULT Acquire() override
    {
        SDL_PumpEvents(); // Never hold the device mutex while SDL invokes the watch.
        std::lock_guard lock(mutex_);
        if (!format_size_ || !window_id_)
        {
            return DIERR_NOTINITIALIZED;
        }
        SDL_Window* window = SDL_GetWindowFromID(window_id_);
        if (!window || !focused_)
        {
            return DIERR_OTHERAPPHASPRIO;
        }
        if (acquired_)
        {
            return DI_NOEFFECT;
        }
        if (no_windows_key_ && !grabbed_)
        {
            previous_grab_ = SDL_GetWindowKeyboardGrab(window);
            if (!SDL_SetWindowKeyboardGrab(window, true))
            {
                return DIERR_GENERIC;
            }
            grabbed_ = true;
        }
        clear_state();
        if (keyboard_)
        {
            int count = 0;
            const bool* keys = SDL_GetKeyboardState(&count);
            for (int index = 0; index < count && index < SDL_SCANCODE_COUNT; ++index)
            {
                if (scan_codes[index] && keys[index])
                {
                    keys_[scan_codes[index]] = 0x80;
                }
            }
        }
        else
        {
            const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
            for (Uint8 button = 1; button <= 8; ++button)
            {
                buttons_[mouse_button(button)] = (buttons & SDL_BUTTON_MASK(button)) ? 0x80 : 0;
            }
        }
        acquired_ = true;
        lost_ = false;
        return DI_OK;
    }

    HRESULT Unacquire() override
    {
        std::lock_guard lock(mutex_);
        const bool was_acquired = acquired_;
        acquired_ = false;
        lost_ = false;
        clear_state();
        release_grab();
        return was_acquired ? DI_OK : DI_NOEFFECT;
    }

    HRESULT GetDeviceState(DWORD size, LPVOID state) override
    {
        if (!state)
        {
            return DIERR_INVALIDPARAM;
        }
        SDL_PumpEvents();
        std::lock_guard lock(mutex_);
        if (!format_size_ || size != format_size_)
        {
            return DIERR_INVALIDPARAM;
        }
        const HRESULT status = read_status();
        if (FAILED(status))
        {
            return status;
        }
        if (keyboard_)
        {
            std::memcpy(state, keys_.data(), keys_.size());
        }
        else
        {
            DIMOUSESTATE2 mouse{};
            mouse.lX = axes_[0];
            mouse.lY = axes_[1];
            mouse.lZ = axes_[2];
            std::memcpy(mouse.rgbButtons, buttons_.data(), buttons_.size());
            std::memcpy(state, &mouse, format_size_);
            axes_.fill(0);
        }
        return DI_OK;
    }

    HRESULT GetDeviceData(
        DWORD size, LPDIDEVICEOBJECTDATA events, LPDWORD count, DWORD flags) override
    {
        // The game's fixed x86 record is 20 bytes, while the host SDK layout
        // has a pointer-sized uAppData. DX3 callers have just the four DWORDs.
        if (!count)
        {
            return DIERR_INVALIDPARAM;
        }
        const DWORD requested = *count;
        *count = 0;
        if ((size != 16 && size != 20 && size != sizeof(DIDEVICEOBJECTDATA)) ||
            (flags & ~DIGDD_PEEK))
        {
            return DIERR_INVALIDPARAM;
        }
        SDL_PumpEvents();
        std::lock_guard lock(mutex_);
        const HRESULT status = read_status();
        if (FAILED(status))
        {
            return status;
        }
        if (buffer_.empty())
        {
            return DIERR_NOTBUFFERED;
        }
        const std::size_t amount = std::min<std::size_t>(requested, buffered_);
        if (events)
        {
            auto* output = reinterpret_cast<BYTE*>(events);
            for (std::size_t index = 0; index < amount; ++index)
            {
                const auto& event = buffer_[(first_ + index) % buffer_.size()];
                // Copy a prefix, never use the host structure's array stride.
                std::memcpy(output + index * size, &event, size);
            }
        }
        *count = static_cast<DWORD>(amount);
        const HRESULT result = overflow_ ? DI_BUFFEROVERFLOW : DI_OK;
        if (!(flags & DIGDD_PEEK))
        {
            first_ = (first_ + amount) % buffer_.size();
            buffered_ -= amount;
            overflow_ = false;
        }
        return result;
    }

    HRESULT SetDataFormat(LPCDIDATAFORMAT format) override
    {
        if (!format || format->dwSize != sizeof(DIDATAFORMAT) ||
            format->dwObjSize != sizeof(DIOBJECTDATAFORMAT) || !format->rgodf)
        {
            return DIERR_INVALIDPARAM;
        }
        std::lock_guard lock(mutex_);
        if (acquired_)
        {
            return DIERR_ACQUIRED;
        }
        const bool supported =
            keyboard_ ? same_format(*format, c_dfDIKeyboard)
                      : (same_format(*format, c_dfDIMouse) || same_format(*format, c_dfDIMouse2));
        if (!supported)
        {
            unsupported(
                "IDirectInputDevice::SetDataFormat", "only stock keyboard and mouse formats");
        }
        format_size_ = format->dwDataSize;
        clear_state();
        return DI_OK;
    }

    HRESULT SetEventNotification(HANDLE) override
    {
        unsupported("IDirectInputDevice::SetEventNotification", "Win32 event notification");
    }

    HRESULT SetCooperativeLevel(HWND handle, DWORD flags) override
    {
        constexpr DWORD known = DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE | DISCL_FOREGROUND |
                                DISCL_BACKGROUND | DISCL_NOWINKEY;
        const DWORD sharing = flags & (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE);
        const DWORD focus = flags & (DISCL_FOREGROUND | DISCL_BACKGROUND);
        if ((flags & ~known) || !sharing || sharing == (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE) ||
            !focus || focus == (DISCL_FOREGROUND | DISCL_BACKGROUND) || !handle)
        {
            return DIERR_INVALIDPARAM;
        }
        if (sharing == DISCL_EXCLUSIVE || focus == DISCL_BACKGROUND)
        {
            unsupported("IDirectInputDevice::SetCooperativeLevel",
                "only foreground, nonexclusive devices are used by the game");
        }
        SDL_Window* window = beachlife::compat::video(handle)->window();
        if (!window)
        {
            return DIERR_INVALIDPARAM;
        }
        std::lock_guard lock(mutex_);
        if (acquired_)
        {
            return DIERR_ACQUIRED;
        }
        release_grab();
        window_id_ = SDL_GetWindowID(window);
        handle_ = handle;
        beachlife::compat::input::bind(handle);
        // BEACHLIFE_BOOT_ASSUME_FOCUS=1 is for scripted test runs only: compositors
        // such as Wayland never give focus to a window a script started.
        focused_ = SDL_GetKeyboardFocus() == window || SDL_getenv("BEACHLIFE_BOOT_ASSUME_FOCUS");
        no_windows_key_ = keyboard_ && (flags & DISCL_NOWINKEY) != 0;
        return DI_OK;
    }

    HRESULT GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA output, DWORD identifier, DWORD how) override
    {
        constexpr DWORD old_size = offsetof(DIDEVICEOBJECTINSTANCEA, dwFFMaxForce);
        if (!output || (output->dwSize != sizeof(*output) && output->dwSize != old_size))
        {
            return DIERR_INVALIDPARAM;
        }
        std::lock_guard lock(mutex_);
        DWORD offset = identifier;
        if (how == DIPH_BYID)
        {
            const DWORD type = identifier & 0xff;
            const DWORD instance = DIDFT_GETINSTANCE(identifier);
            if (type & DIDFT_BUTTON)
            {
                offset = keyboard_ ? instance : DIMOFS_BUTTON0 + instance;
            }
            else if (!keyboard_ && (type & DIDFT_AXIS) && instance < 3)
            {
                offset = instance * 4;
            }
            else
            {
                return DIERR_OBJECTNOTFOUND;
            }
        }
        else if (how != DIPH_BYOFFSET)
        {
            unsupported("IDirectInputDevice::GetObjectInfo", "only object offsets and IDs");
        }
        DIDEVICEOBJECTINSTANCEA object{};
        object.dwSize = output->dwSize;
        object.dwOfs = offset;
        if (keyboard_ && offset < 256)
        {
            object.guidType = GUID_Key;
            object.dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(offset);
            std::snprintf(object.tszName, sizeof(object.tszName), "Key %u", offset);
        }
        else if (!keyboard_ && offset <= DIMOFS_Z && offset % 4 == 0)
        {
            const DWORD axis = offset / 4;
            object.guidType = *mouse_objects[axis].pguid;
            object.dwType = DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(axis);
            object.dwFlags = 0x100; // DIDOI_ASPECTPOSITION
            object.wUsagePage = 1;
            object.wUsage = 0x30 + axis;
            std::snprintf(object.tszName, sizeof(object.tszName), "%c axis", 'X' + axis);
        }
        else if (!keyboard_ && offset >= DIMOFS_BUTTON0 && offset < DIMOFS_BUTTON0 + 8)
        {
            const DWORD button = offset - DIMOFS_BUTTON0;
            object.guidType = GUID_Button;
            object.dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(button);
            object.wUsagePage = 9;
            object.wUsage = button + 1;
            std::snprintf(object.tszName, sizeof(object.tszName), "Button %u", button);
        }
        else
        {
            return DIERR_OBJECTNOTFOUND;
        }
        std::memcpy(output, &object, object.dwSize);
        return DI_OK;
    }

    HRESULT GetDeviceInfo(LPDIDEVICEINSTANCEA) override
    {
        unsupported("IDirectInputDevice::GetDeviceInfo", "not used by the retail input calls");
    }

    HRESULT RunControlPanel(HWND, DWORD) override
    {
        unsupported("IDirectInputDevice::RunControlPanel", "Windows input control panel");
    }

    HRESULT Initialize(HINSTANCE, DWORD, REFGUID) override
    {
        return DIERR_ALREADYINITIALIZED;
    }

  private:
    ~Device()
    {
        // SDL serializes removal with callbacks. Do not hold mutex_ here.
        if (watching_)
        {
            SDL_RemoveEventWatch(event_watch, this);
        }
        release_grab();
    }

    void release_grab()
    {
        if (grabbed_)
        {
            if (SDL_Window* window = SDL_GetWindowFromID(window_id_))
            {
                if (!SDL_SetWindowKeyboardGrab(window, previous_grab_))
                {
                    unsupported("IDirectInputDevice::Unacquire", SDL_GetError());
                }
            }
            grabbed_ = false;
        }
    }

    HRESULT read_status()
    {
        if (acquired_ && !SDL_GetWindowFromID(window_id_))
        {
            acquired_ = false;
            lost_ = true;
            clear_state();
        }
        return acquired_ ? DI_OK : (lost_ ? DIERR_INPUTLOST : DIERR_NOTACQUIRED);
    }

    static bool SDLCALL event_watch(void* context, SDL_Event* event) noexcept
    {
        auto& self = *static_cast<Device*>(context);
        std::lock_guard lock(self.mutex_);
        if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED &&
            event->window.windowID == self.window_id_)
        {
            self.focused_ = true;
        }
        else if ((event->type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                     event->type == SDL_EVENT_WINDOW_DESTROYED) &&
                 event->window.windowID == self.window_id_)
        {
            self.focused_ = SDL_getenv("BEACHLIFE_BOOT_ASSUME_FOCUS") != nullptr;
            if (self.acquired_)
            {
                self.acquired_ = false;
                self.lost_ = true;
                self.clear_state();
            }
        }
        else if (self.acquired_ && self.keyboard_ &&
                 (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
                 event->key.windowID == self.window_id_ &&
                 event->key.scancode > SDL_SCANCODE_UNKNOWN &&
                 event->key.scancode < SDL_SCANCODE_COUNT)
        {
            const BYTE code = scan_codes[event->key.scancode];
            if (code && !event->key.repeat)
            {
                const BYTE value = event->type == SDL_EVENT_KEY_DOWN ? 0x80 : 0;
                if (self.keys_[code] != value)
                {
                    self.keys_[code] = value;
                    self.queue(code, value, event->common.timestamp, next_sequence++);
                }
            }
        }
        else if (self.acquired_ && !self.keyboard_)
        {
            const DWORD sequence = next_sequence++;
            const Uint64 timestamp = event->common.timestamp;
            if (event->type == SDL_EVENT_MOUSE_MOTION && event->motion.windowID == self.window_id_)
            {
                beachlife::compat::input::Mapping map;
                if (beachlife::compat::input::mapping(self.handle_, map))
                {
                    const auto delta = map.delta(
                        event->motion.x, event->motion.y, event->motion.xrel, event->motion.yrel);
                    self.axis(0, delta.x, timestamp, sequence);
                    self.axis(1, delta.y, timestamp, sequence);
                }
            }
            else if (event->type == SDL_EVENT_MOUSE_WHEEL &&
                     event->wheel.windowID == self.window_id_)
            {
                const double direction = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
                self.axis(2, direction * event->wheel.y * 120.0, timestamp, sequence);
            }
            else if ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                         event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                     event->button.windowID == self.window_id_)
            {
                const int button = mouse_button(event->button.button);
                if (button >= 0 && button < static_cast<int>(self.format_size_ - DIMOFS_BUTTON0))
                {
                    const BYTE value = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 0x80 : 0;
                    if (self.buttons_[button] != value)
                    {
                        self.buttons_[button] = value;
                        self.queue(DIMOFS_BUTTON0 + button, value, timestamp, sequence);
                    }
                }
            }
        }
        return true;
    }

    void queue(DWORD offset, DWORD value, Uint64 timestamp, DWORD sequence)
    {
        if (buffer_.empty())
        {
            return;
        }
        // As in DirectInput, one slot distinguishes the ring's full and empty
        // states. On overflow, retain unread events and drop incoming data.
        if (buffered_ + 1 >= buffer_.size())
        {
            overflow_ = true;
            return;
        }
        buffer_[(first_ + buffered_) % buffer_.size()] = {
            offset, value, static_cast<DWORD>(timestamp / 1000000), sequence, 0};
        ++buffered_;
    }

    void axis(std::size_t index, double change, Uint64 timestamp, DWORD sequence)
    {
        if (!std::isfinite(change))
        {
            return;
        }
        double& fraction = fractions_[index];
        fraction += change;
        const LONG whole = static_cast<LONG>(std::clamp(fraction,
            static_cast<double>(std::numeric_limits<LONG>::min()),
            static_cast<double>(std::numeric_limits<LONG>::max())));
        fraction -= whole;
        if (whole)
        {
            // Sum with Windows' 32-bit wrapping, avoiding signed C++ overflow.
            axes_[index] =
                static_cast<LONG>(static_cast<DWORD>(axes_[index]) + static_cast<DWORD>(whole));
            queue(static_cast<DWORD>(index * 4), static_cast<DWORD>(whole), timestamp, sequence);
        }
    }

    void clear_buffer()
    {
        first_ = 0;
        buffered_ = 0;
        overflow_ = false;
    }

    void clear_state()
    {
        keys_.fill(0);
        buttons_.fill(0);
        axes_.fill(0);
        fractions_.fill(0);
        clear_buffer();
    }

    std::atomic<ULONG> references_{1};
    std::mutex mutex_;
    const bool keyboard_;
    std::array<BYTE, 256> keys_{};
    std::array<BYTE, 8> buttons_{};
    std::array<LONG, 3> axes_{};
    std::array<double, 3> fractions_{};
    std::vector<DIDEVICEOBJECTDATA> buffer_;
    std::size_t first_ = 0, buffered_ = 0;
    bool overflow_ = false;
    SDL_WindowID window_id_ = 0;
    HWND handle_ = nullptr;
    DWORD format_size_ = 0;
    bool watching_ = false, focused_ = false, acquired_ = false, lost_ = false;
    bool no_windows_key_ = false, grabbed_ = false, previous_grab_ = false;
};

class DirectInput final : public IDirectInput8A
{
  public:
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return DIERR_INVALIDPARAM;
        }
        *output = nullptr;
        if (!same_guid(iid, unknown_iid) && !same_guid(iid, IID_IDirectInput8A))
        {
            return DIERR_NOINTERFACE;
        }
        *output = static_cast<IDirectInput8A*>(this);
        AddRef();
        return DI_OK;
    }

    ULONG AddRef() override
    {
        return ++references_;
    }

    ULONG Release() override
    {
        const ULONG remaining = --references_;
        if (!remaining)
        {
            delete this;
        }
        return remaining;
    }

    HRESULT CreateDevice(REFGUID guid, IDirectInputDeviceA** output, LPUNKNOWN outer) override
    {
        if (!output)
        {
            return DIERR_INVALIDPARAM;
        }
        *output = nullptr;
        if (outer)
        {
            return DIERR_NOAGGREGATION;
        }
        const bool keyboard = same_guid(guid, GUID_SysKeyboard);
        if (!keyboard && !same_guid(guid, GUID_SysMouse))
        {
            unsupported("IDirectInput8::CreateDevice", "only the system keyboard and mouse");
        }
        auto* device = new (std::nothrow) Device(keyboard);
        if (!device)
        {
            return DIERR_OUTOFMEMORY;
        }
        if (!device->watch())
        {
            device->Release();
            return DIERR_GENERIC;
        }
        *output = device;
        return DI_OK;
    }

    HRESULT EnumDevices(DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD) override
    {
        unsupported("IDirectInput8::EnumDevices", "not used by the retail input calls");
    }

    HRESULT GetDeviceStatus(REFGUID) override
    {
        unsupported("IDirectInput8::GetDeviceStatus", "not used by the retail input calls");
    }

    HRESULT RunControlPanel(HWND, DWORD) override
    {
        unsupported("IDirectInput8::RunControlPanel", "Windows input control panel");
    }

    HRESULT Initialize(HINSTANCE, DWORD) override
    {
        return DIERR_ALREADYINITIALIZED;
    }

    HRESULT FindDevice(REFGUID, LPCSTR, LPGUID) override
    {
        unsupported("IDirectInput8::FindDevice", "not used by the retail input calls");
    }

    HRESULT EnumDevicesBySemantics(LPCSTR, void*, void*, LPVOID, DWORD) override
    {
        unsupported(
            "IDirectInput8::EnumDevicesBySemantics", "action mapping is not used by the game");
    }

    HRESULT ConfigureDevices(void*, void*, DWORD, LPVOID) override
    {
        unsupported("IDirectInput8::ConfigureDevices", "Windows input configuration dialog");
    }

  private:
    std::atomic<ULONG> references_{1};
};
} // namespace

extern "C"
{
    const DIDATAFORMAT c_dfDIKeyboard = {sizeof(DIDATAFORMAT),
        sizeof(DIOBJECTDATAFORMAT),
        DIDF_RELAXIS,
        256,
        256,
        keyboard_objects.data()};
    const DIDATAFORMAT c_dfDIMouse = {sizeof(DIDATAFORMAT),
        sizeof(DIOBJECTDATAFORMAT),
        DIDF_RELAXIS,
        sizeof(DIMOUSESTATE),
        7,
        mouse_objects.data()};
    const DIDATAFORMAT c_dfDIMouse2 = {sizeof(DIDATAFORMAT),
        sizeof(DIOBJECTDATAFORMAT),
        DIDF_RELAXIS,
        sizeof(DIMOUSESTATE2),
        11,
        mouse_objects.data()};

    HRESULT WINAPI DirectInput8Create(
        HINSTANCE, DWORD version, REFIID iid, LPVOID* output, LPUNKNOWN outer)
    {
        if (!output)
        {
            return DIERR_INVALIDPARAM;
        }
        *output = nullptr;
        if (outer)
        {
            return DIERR_NOAGGREGATION;
        }
        if (version != DIRECTINPUT8_VERSION)
        {
            return version < DIRECTINPUT8_VERSION ? DIERR_OLDDIRECTINPUTVERSION
                                                  : DIERR_BETADIRECTINPUTVERSION;
        }
        if (!same_guid(iid, IID_IDirectInput8A))
        {
            return DIERR_NOINTERFACE;
        }
        if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
        {
            return DIERR_NOTINITIALIZED;
        }
        *output = new (std::nothrow) DirectInput;
        return *output ? DI_OK : DIERR_OUTOFMEMORY;
    }
}

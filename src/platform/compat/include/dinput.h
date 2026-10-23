// DirectInput subset used by the retail keyboard and mouse callers. The
// default exposes the old device prefix, as in the game's pinned SDK; its
// startup_direct_input8.h supplies IDirectInput8A. Host users can request the
// same DI8 interface here by defining DIRECTINPUT_VERSION to 0x0800 first.
#pragma once

#include "windows.h"

#include <limits.h>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0500
#endif
#define DIRECTINPUT8_VERSION 0x0800

// COM declaration spellings used by the game's DI8 declaration.
#ifndef STDMETHOD
#define STDMETHOD(method) virtual HRESULT method
#define STDMETHOD_(type, method) virtual type method
#define PURE = 0
#endif
typedef GUID* LPGUID;

// Window frame metrics used by the game's buffered mouse reader.
#ifndef SM_CXFIXEDFRAME
#define SM_CXFIXEDFRAME 7
#define SM_CYFIXEDFRAME 8
#endif

#define DI_OK ((HRESULT)0)
#define DI_NOEFFECT ((HRESULT)1)
#define DI_BUFFEROVERFLOW ((HRESULT)1)
#define DIERR_OLDDIRECTINPUTVERSION ((HRESULT)0x8007047e)
#define DIERR_BETADIRECTINPUTVERSION ((HRESULT)0x80070481)
#define DIERR_NOTFOUND ((HRESULT)0x80070002)
#define DIERR_OBJECTNOTFOUND DIERR_NOTFOUND
#define DIERR_INVALIDPARAM ((HRESULT)0x80070057)
#define DIERR_NOINTERFACE ((HRESULT)0x80004002)
#define DIERR_GENERIC E_FAIL
#define DIERR_OUTOFMEMORY ((HRESULT)0x8007000e)
#define DIERR_UNSUPPORTED ((HRESULT)0x80004001)
#define DIERR_NOTINITIALIZED ((HRESULT)0x80070015)
#define DIERR_ALREADYINITIALIZED ((HRESULT)0x800704df)
#define DIERR_NOAGGREGATION ((HRESULT)0x80040110)
#define DIERR_OTHERAPPHASPRIO ((HRESULT)0x80070005)
#define DIERR_INPUTLOST ((HRESULT)0x8007001e)
#define DIERR_ACQUIRED ((HRESULT)0x800700aa)
#define DIERR_NOTACQUIRED ((HRESULT)0x8007000c)
#define DIERR_NOTBUFFERED ((HRESULT)0x80040207)

#define DISCL_EXCLUSIVE 0x00000001
#define DISCL_NONEXCLUSIVE 0x00000002
#define DISCL_FOREGROUND 0x00000004
#define DISCL_BACKGROUND 0x00000008
#define DISCL_NOWINKEY 0x00000010
#define DIDF_ABSAXIS 0x00000001
#define DIDF_RELAXIS 0x00000002
#define DIDFT_RELAXIS 0x00000001
#define DIDFT_ABSAXIS 0x00000002
#define DIDFT_AXIS 0x00000003
#define DIDFT_PSHBUTTON 0x00000004
#define DIDFT_TGLBUTTON 0x00000008
#define DIDFT_BUTTON 0x0000000c
#define DIDFT_OPTIONAL 0x80000000
#define DIDFT_MAKEINSTANCE(n) ((DWORD)(n) << 8)
#define DIDFT_GETINSTANCE(n) (((DWORD)(n) >> 8) & 0xffff)
#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DIPH_BYID 2
#define DIPH_BYUSAGE 3
#define DIGDD_PEEK 0x00000001
#define DIPROPAXISMODE_ABS 0
#define DIPROPAXISMODE_REL 1
// Aligned host tokens replace the SDK's misaligned integer-reference tokens.
// These retain the caller syntax without binding an invalid C++ GUID reference.
#define DIPROP_BUFFERSIZE beachlife_dinput_prop_buffersize
#define DIPROP_AXISMODE beachlife_dinput_prop_axismode

#define DIK_ESCAPE 0x01
#define DIK_1 0x02
#define DIK_2 0x03
#define DIK_3 0x04
#define DIK_4 0x05
#define DIK_5 0x06
#define DIK_6 0x07
#define DIK_7 0x08
#define DIK_8 0x09
#define DIK_9 0x0a
#define DIK_0 0x0b
#define DIK_MINUS 0x0c
#define DIK_EQUALS 0x0d
#define DIK_BACK 0x0e
#define DIK_TAB 0x0f
#define DIK_Q 0x10
#define DIK_W 0x11
#define DIK_E 0x12
#define DIK_R 0x13
#define DIK_T 0x14
#define DIK_Y 0x15
#define DIK_U 0x16
#define DIK_I 0x17
#define DIK_O 0x18
#define DIK_P 0x19
#define DIK_LBRACKET 0x1a
#define DIK_RBRACKET 0x1b
#define DIK_RETURN 0x1c
#define DIK_LCONTROL 0x1d
#define DIK_A 0x1e
#define DIK_S 0x1f
#define DIK_D 0x20
#define DIK_F 0x21
#define DIK_G 0x22
#define DIK_H 0x23
#define DIK_J 0x24
#define DIK_K 0x25
#define DIK_L 0x26
#define DIK_SEMICOLON 0x27
#define DIK_APOSTROPHE 0x28
#define DIK_GRAVE 0x29
#define DIK_LSHIFT 0x2a
#define DIK_BACKSLASH 0x2b
#define DIK_Z 0x2c
#define DIK_X 0x2d
#define DIK_C 0x2e
#define DIK_V 0x2f
#define DIK_B 0x30
#define DIK_N 0x31
#define DIK_M 0x32
#define DIK_COMMA 0x33
#define DIK_PERIOD 0x34
#define DIK_SLASH 0x35
#define DIK_RSHIFT 0x36
#define DIK_MULTIPLY 0x37
#define DIK_LMENU 0x38
#define DIK_SPACE 0x39
#define DIK_CAPITAL 0x3a
#define DIK_F1 0x3b
#define DIK_F2 0x3c
#define DIK_F3 0x3d
#define DIK_F4 0x3e
#define DIK_F5 0x3f
#define DIK_F6 0x40
#define DIK_F7 0x41
#define DIK_F8 0x42
#define DIK_F9 0x43
#define DIK_F10 0x44
#define DIK_NUMLOCK 0x45
#define DIK_SCROLL 0x46
#define DIK_NUMPAD7 0x47
#define DIK_NUMPAD8 0x48
#define DIK_NUMPAD9 0x49
#define DIK_SUBTRACT 0x4a
#define DIK_NUMPAD4 0x4b
#define DIK_NUMPAD5 0x4c
#define DIK_NUMPAD6 0x4d
#define DIK_ADD 0x4e
#define DIK_NUMPAD1 0x4f
#define DIK_NUMPAD2 0x50
#define DIK_NUMPAD3 0x51
#define DIK_NUMPAD0 0x52
#define DIK_DECIMAL 0x53
#define DIK_OEM_102 0x56
#define DIK_F11 0x57
#define DIK_F12 0x58
#define DIK_F13 0x64
#define DIK_F14 0x65
#define DIK_F15 0x66
#define DIK_KANA 0x70
#define DIK_CONVERT 0x79
#define DIK_NOCONVERT 0x7b
#define DIK_YEN 0x7d
#define DIK_NUMPADEQUALS 0x8d
#define DIK_NUMPADENTER 0x9c
#define DIK_RCONTROL 0x9d
#define DIK_NUMPADCOMMA 0xb3
#define DIK_DIVIDE 0xb5
#define DIK_SYSRQ 0xb7
#define DIK_RMENU 0xb8
#define DIK_PAUSE 0xc5
#define DIK_HOME 0xc7
#define DIK_UP 0xc8
#define DIK_PRIOR 0xc9
#define DIK_LEFT 0xcb
#define DIK_RIGHT 0xcd
#define DIK_END 0xcf
#define DIK_DOWN 0xd0
#define DIK_NEXT 0xd1
#define DIK_INSERT 0xd2
#define DIK_DELETE 0xd3
#define DIK_LWIN 0xdb
#define DIK_RWIN 0xdc
#define DIK_APPS 0xdd
#define DIK_POWER 0xde
#define DIK_SLEEP 0xdf
#define DIK_BACKSPACE DIK_BACK
#define DIK_LALT DIK_LMENU
#define DIK_RALT DIK_RMENU
#define DIK_CAPSLOCK DIK_CAPITAL
#define DIK_PGUP DIK_PRIOR
#define DIK_PGDN DIK_NEXT

typedef struct DIPROPHEADER
{
    DWORD dwSize, dwHeaderSize, dwObj, dwHow;
} DIPROPHEADER, *LPDIPROPHEADER;
typedef const DIPROPHEADER* LPCDIPROPHEADER;
typedef struct DIPROPDWORD
{
    DIPROPHEADER diph;
    DWORD dwData;
} DIPROPDWORD;

typedef struct DIOBJECTDATAFORMAT
{
    const GUID* pguid;
    DWORD dwOfs, dwType, dwFlags;
} DIOBJECTDATAFORMAT;
typedef struct DIDATAFORMAT
{
    DWORD dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs;
    DIOBJECTDATAFORMAT* rgodf;
} DIDATAFORMAT;
typedef const DIDATAFORMAT* LPCDIDATAFORMAT;

typedef struct DIDEVICEOBJECTDATA
{
    DWORD dwOfs, dwData, dwTimeStamp, dwSequence;
    uintptr_t uAppData;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;

typedef struct DIMOUSESTATE
{
    LONG lX, lY, lZ;
    BYTE rgbButtons[4];
} DIMOUSESTATE;
typedef struct DIMOUSESTATE2
{
    LONG lX, lY, lZ;
    BYTE rgbButtons[8];
} DIMOUSESTATE2;
#define DIMOFS_X 0
#define DIMOFS_Y 4
#define DIMOFS_Z 8
#define DIMOFS_BUTTON0 12
#define DIMOFS_BUTTON1 13
#define DIMOFS_BUTTON2 14
#define DIMOFS_BUTTON3 15
#define DIMOFS_BUTTON4 16
#define DIMOFS_BUTTON5 17
#define DIMOFS_BUTTON6 18
#define DIMOFS_BUTTON7 19

typedef struct DIDEVICEOBJECTINSTANCEA
{
    DWORD dwSize;
    GUID guidType;
    DWORD dwOfs, dwType, dwFlags;
    CHAR tszName[MAX_PATH];
    DWORD dwFFMaxForce, dwFFForceResolution;
    WORD wCollectionNumber, wDesignatorIndex, wUsagePage, wUsage;
    DWORD dwDimension;
    WORD wExponent, wReportId;
} DIDEVICEOBJECTINSTANCEA, *LPDIDEVICEOBJECTINSTANCEA;
typedef const DIDEVICEOBJECTINSTANCEA* LPCDIDEVICEOBJECTINSTANCEA;
typedef struct DIDEVICEINSTANCEA
{
    DWORD dwSize;
    GUID guidInstance, guidProduct;
    DWORD dwDevType;
    CHAR tszInstanceName[MAX_PATH], tszProductName[MAX_PATH];
    GUID guidFFDriver;
    WORD wUsagePage, wUsage;
} DIDEVICEINSTANCEA, *LPDIDEVICEINSTANCEA;
typedef const DIDEVICEINSTANCEA* LPCDIDEVICEINSTANCEA;
typedef struct DIDEVCAPS
{
    DWORD dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs;
    DWORD dwFFSamplePeriod, dwFFMinTimeResolution, dwFirmwareRevision;
    DWORD dwHardwareRevision, dwFFDriverVersion;
} DIDEVCAPS, *LPDIDEVCAPS;
typedef BOOL(CALLBACK* LPDIENUMDEVICESCALLBACKA)(LPCDIDEVICEINSTANCEA, LPVOID);
typedef BOOL(CALLBACK* LPDIENUMDEVICEOBJECTSCALLBACKA)(LPCDIDEVICEOBJECTINSTANCEA, LPVOID);

#ifdef __cplusplus
extern "C"
{
    extern const GUID GUID_SysKeyboard, GUID_SysMouse;
    extern const GUID GUID_Key, GUID_Button, GUID_XAxis, GUID_YAxis, GUID_ZAxis;
    extern const GUID IID_IDirectInput8A, IID_IDirectInputDeviceA;
    extern const GUID beachlife_dinput_prop_buffersize, beachlife_dinput_prop_axismode;
    extern const DIDATAFORMAT c_dfDIKeyboard, c_dfDIMouse, c_dfDIMouse2;
    HRESULT WINAPI DirectInput8Create(
        HINSTANCE instance, DWORD version, REFIID interface_id, LPVOID* output, LPUNKNOWN outer);
}

struct IDirectInputDeviceA : IUnknown
{
    virtual HRESULT GetCapabilities(LPDIDEVCAPS capabilities) = 0;
    virtual HRESULT EnumObjects(
        LPDIENUMDEVICEOBJECTSCALLBACKA callback, LPVOID context, DWORD flags) = 0;
    virtual HRESULT GetProperty(REFGUID property, LPDIPROPHEADER header) = 0;
    virtual HRESULT SetProperty(REFGUID property, LPCDIPROPHEADER header) = 0;
    virtual HRESULT Acquire() = 0;
    virtual HRESULT Unacquire() = 0;
    virtual HRESULT GetDeviceState(DWORD size, LPVOID state) = 0;
    virtual HRESULT GetDeviceData(
        DWORD size, LPDIDEVICEOBJECTDATA events, LPDWORD count, DWORD flags) = 0;
    virtual HRESULT SetDataFormat(LPCDIDATAFORMAT format) = 0;
    virtual HRESULT SetEventNotification(HANDLE event) = 0;
    virtual HRESULT SetCooperativeLevel(HWND window, DWORD flags) = 0;
    virtual HRESULT GetObjectInfo(
        LPDIDEVICEOBJECTINSTANCEA object, DWORD identifier, DWORD how) = 0;
    virtual HRESULT GetDeviceInfo(LPDIDEVICEINSTANCEA device) = 0;
    virtual HRESULT RunControlPanel(HWND owner, DWORD flags) = 0;
    virtual HRESULT Initialize(HINSTANCE instance, DWORD version, REFGUID device) = 0;

#if ULONG_MAX > UINT32_MAX
    // The reconstructed mouse count is an unsigned long. Preserve that
    // host-sized field while keeping the actual Windows API's DWORD count.
    HRESULT GetDeviceData(
        DWORD size, LPDIDEVICEOBJECTDATA events, unsigned long* count, DWORD flags)
    {
        if (!count || *count > UINT32_MAX)
        {
            return DIERR_INVALIDPARAM;
        }
        DWORD narrow_count = static_cast<DWORD>(*count);
        const HRESULT result = GetDeviceData(size, events, &narrow_count, flags);
        *count = narrow_count;
        return result;
    }
#endif
};
typedef IDirectInputDeviceA* LPDIRECTINPUTDEVICEA;

#if DIRECTINPUT_VERSION >= 0x0800
// Match the game's startup_direct_input8.h, including its shared device prefix
// and opaque DI8-only parameters. Only that prefix is used by the retail code.
struct IDirectInput8A : public IUnknown
{
    STDMETHOD(CreateDevice)(REFGUID guid, IDirectInputDeviceA** device, LPUNKNOWN outer) PURE;
    STDMETHOD(EnumDevices)(
        DWORD device_type, LPDIENUMDEVICESCALLBACKA callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(GetDeviceStatus)(REFGUID instance) PURE;
    STDMETHOD(RunControlPanel)(HWND owner, DWORD flags) PURE;
    STDMETHOD(Initialize)(HINSTANCE instance, DWORD version) PURE;
    STDMETHOD(FindDevice)(REFGUID guid, LPCSTR name, LPGUID instance) PURE;
    STDMETHOD(EnumDevicesBySemantics)(
        LPCSTR user, void* action_format, void* callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(ConfigureDevices)(void* callback, void* parameters, DWORD flags, LPVOID data) PURE;
};
#else
struct IDirectInput8A;
#endif
typedef IDirectInput8A* LPDIRECTINPUT8A;
#endif

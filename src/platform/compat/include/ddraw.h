// DirectDraw interface subset used by the retail startup movie. COM method
// order follows the SDK; integer fields retain Windows widths on the host.
#pragma once
#include "windows.h"
#ifndef STDMETHOD
#define STDMETHOD(method) virtual HRESULT method
#define STDMETHOD_(type, method) virtual type method
#define PURE = 0
#endif
#define DD_OK S_OK
#define DDERR_UNSUPPORTED ((HRESULT)0x80004001)
#define DDERR_INVALIDPARAMS ((HRESULT)0x80070057)
#define DDERR_OUTOFMEMORY ((HRESULT)0x8007000e)
#define DDERR_INVALIDRECT ((HRESULT)0x88760096)
#define DDERR_SURFACEBUSY ((HRESULT)0x887601ae)
#define DDERR_NOTLOCKED ((HRESULT)0x88760248)
#define DDERR_NOTFOUND ((HRESULT)0x887600ff)
#define DDSCL_NORMAL 0x00000008
#define DDSCL_FULLSCREEN 0x00000001
#define DDSCL_EXCLUSIVE 0x00000010
#define DDSCL_FPUSETUP 0x00000800
#define DDSD_CAPS 0x00000001
#define DDSD_HEIGHT 0x00000002
#define DDSD_WIDTH 0x00000004
#define DDSD_PITCH 0x00000008
#define DDSD_BACKBUFFERCOUNT 0x00000020
#define DDSD_LPSURFACE 0x00000800
#define DDSD_PIXELFORMAT 0x00001000
#define DDSCAPS_BACKBUFFER 0x00000004
#define DDSCAPS_COMPLEX 0x00000008
#define DDSCAPS_FLIP 0x00000010
#define DDSCAPS_FRONTBUFFER 0x00000020
#define DDSCAPS_OFFSCREENPLAIN 0x00000040
#define DDSCAPS_PRIMARYSURFACE 0x00000200
#define DDSCAPS_SYSTEMMEMORY 0x00000800
#define DDSCAPS_VIDEOMEMORY 0x00004000
#define DDPF_RGB 0x00000040
#define DDBLT_WAIT 0x01000000
#define DDFLIP_WAIT 0x00000001
#define DDLOCK_WAIT 0x00000001
#define DDLOCK_READONLY 0x00000010
#define DDLOCK_WRITEONLY 0x00000020
#define DDLOCK_NOSYSLOCK 0x00000800

struct IDirectDraw;
struct IDirectDraw4;
struct IDirectDrawSurface;
struct IDirectDrawSurface4;
struct IDirectDrawPalette;
struct IDirectDrawClipper;
struct DDCAPS;
struct DDBLTFX;
struct DDBLTBATCH;
struct PALETTEENTRY;
using LPDDCAPS = DDCAPS*;
using LPPALETTEENTRY = PALETTEENTRY*;
using LPDIRECTDRAW = IDirectDraw*;
using LPDIRECTDRAW4 = IDirectDraw4*;
using LPDIRECTDRAWSURFACE = IDirectDrawSurface*;
using LPDIRECTDRAWSURFACE4 = IDirectDrawSurface4*;
using LPDIRECTDRAWCLIPPER = IDirectDrawClipper*;
using LPDIRECTDRAWPALETTE = IDirectDrawPalette*;
struct DDCOLORKEY
{
    DWORD dwColorSpaceLowValue, dwColorSpaceHighValue;
};
struct DDSCAPS
{
    DWORD dwCaps;
};
struct DDSCAPS2
{
    DWORD dwCaps, dwCaps2, dwCaps3, dwCaps4;
};
struct DDPIXELFORMAT
{
    DWORD dwSize, dwFlags, dwFourCC, dwRGBBitCount;
    DWORD dwRBitMask, dwGBitMask, dwBBitMask, dwRGBAlphaBitMask;
};
struct DDSURFACEDESC
{
    DWORD dwSize, dwFlags, dwHeight, dwWidth;
    union
    {
        LONG lPitch;
        DWORD dwLinearSize;
    };
    DWORD dwBackBufferCount;
    union
    {
        DWORD dwMipMapCount;
        DWORD dwZBufferBitDepth;
        DWORD dwRefreshRate;
    };
    DWORD dwAlphaBitDepth, dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay, ddckCKDestBlt, ddckCKSrcOverlay, ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;
};
using LPDDSURFACEDESC = DDSURFACEDESC*;
struct DDSURFACEDESC2
{
    DWORD dwSize, dwFlags, dwHeight, dwWidth;
    union
    {
        LONG lPitch;
        DWORD dwLinearSize;
    };
    DWORD dwBackBufferCount;
    union
    {
        DWORD dwMipMapCount;
        DWORD dwZBufferBitDepth;
        DWORD dwRefreshRate;
    };
    DWORD dwAlphaBitDepth, dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay, ddckCKDestBlt, ddckCKSrcOverlay, ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS2 ddsCaps;
    DWORD dwTextureStage;
};
using LPDDSURFACEDESC2 = DDSURFACEDESC2*;
using LPDDPIXELFORMAT = DDPIXELFORMAT*;
using LPDDSCAPS = DDSCAPS*;
using LPDDSCAPS2 = DDSCAPS2*;

struct IDirectDrawClipper : IUnknown
{
    STDMETHOD(GetClipList)(RECT*, void*, DWORD*) PURE;
    STDMETHOD(GetHWnd)(HWND*) PURE;
    STDMETHOD(Initialize)(IDirectDraw*, DWORD) PURE;
    STDMETHOD(IsClipListChanged)(BOOL*) PURE;
    STDMETHOD(SetClipList)(void*, DWORD) PURE;
    STDMETHOD(SetHWnd)(DWORD, HWND) PURE;
};
struct IDirectDraw : IUnknown
{
    STDMETHOD(Compact)() PURE;
    STDMETHOD(CreateClipper)(DWORD, IDirectDrawClipper**, IUnknown*) PURE;
    STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, IDirectDrawPalette**, IUnknown*) PURE;
    STDMETHOD(CreateSurface)(DDSURFACEDESC*, IDirectDrawSurface**, IUnknown*) PURE;
    STDMETHOD(DuplicateSurface)(IDirectDrawSurface*, IDirectDrawSurface**) PURE;
    STDMETHOD(EnumDisplayModes)(DWORD, DDSURFACEDESC*, LPVOID, void*) PURE;
    STDMETHOD(EnumSurfaces)(DWORD, DDSURFACEDESC*, LPVOID, void*) PURE;
    STDMETHOD(FlipToGDISurface)() PURE;
    STDMETHOD(GetCaps)(LPDDCAPS, LPDDCAPS) PURE;
    STDMETHOD(GetDisplayMode)(DDSURFACEDESC*) PURE;
    STDMETHOD(GetFourCCCodes)(DWORD*, DWORD*) PURE;
    STDMETHOD(GetGDISurface)(IDirectDrawSurface**) PURE;
    STDMETHOD(GetMonitorFrequency)(DWORD*) PURE;
    STDMETHOD(GetScanLine)(DWORD*) PURE;
    STDMETHOD(GetVerticalBlankStatus)(BOOL*) PURE;
    STDMETHOD(Initialize)(GUID*) PURE;
    STDMETHOD(RestoreDisplayMode)() PURE;
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) PURE;
    STDMETHOD(SetDisplayMode)(DWORD, DWORD, DWORD) PURE;
};
struct IDirectDrawSurface : IUnknown
{
    STDMETHOD(AddAttachedSurface)(IDirectDrawSurface*) PURE;
    STDMETHOD(AddOverlayDirtyRect)(RECT*) PURE;
    STDMETHOD(Blt)(RECT*, IDirectDrawSurface*, RECT*, DWORD, DDBLTFX*) PURE;
    STDMETHOD(BltBatch)(DDBLTBATCH*, DWORD, DWORD) PURE;
    STDMETHOD(BltFast)(DWORD, DWORD, IDirectDrawSurface*, RECT*, DWORD) PURE;
    STDMETHOD(DeleteAttachedSurface)(DWORD, IDirectDrawSurface*) PURE;
    STDMETHOD(EnumAttachedSurfaces)(void*, void*) PURE;
    STDMETHOD(EnumOverlayZOrders)(DWORD, void*, void*) PURE;
    STDMETHOD(Flip)(IDirectDrawSurface*, DWORD) PURE;
    STDMETHOD(GetAttachedSurface)(DDSCAPS*, IDirectDrawSurface**) PURE;
    STDMETHOD(GetBltStatus)(DWORD) PURE;
    STDMETHOD(GetCaps)(DDSCAPS*) PURE;
    STDMETHOD(GetClipper)(IDirectDrawClipper**) PURE;
    STDMETHOD(GetColorKey)(DWORD, DDCOLORKEY*) PURE;
    STDMETHOD(GetDC)(HDC*) PURE;
    STDMETHOD(GetFlipStatus)(DWORD) PURE;
    STDMETHOD(GetOverlayPosition)(LONG*, LONG*) PURE;
    STDMETHOD(GetPalette)(IDirectDrawPalette**) PURE;
    STDMETHOD(GetPixelFormat)(DDPIXELFORMAT*) PURE;
    STDMETHOD(GetSurfaceDesc)(DDSURFACEDESC*) PURE;
    STDMETHOD(Initialize)(IDirectDraw*, DDSURFACEDESC*) PURE;
    STDMETHOD(IsLost)() PURE;
    STDMETHOD(Lock)(RECT*, DDSURFACEDESC*, DWORD, HANDLE) PURE;
    STDMETHOD(ReleaseDC)(HDC) PURE;
    STDMETHOD(Restore)() PURE;
    STDMETHOD(SetClipper)(IDirectDrawClipper*) PURE;
    STDMETHOD(SetColorKey)(DWORD, DDCOLORKEY*) PURE;
    STDMETHOD(SetOverlayPosition)(LONG, LONG) PURE;
    STDMETHOD(SetPalette)(IDirectDrawPalette*) PURE;
    STDMETHOD(Unlock)(LPVOID) PURE;
};
struct IDirectDraw4 : IUnknown
{
    STDMETHOD(Compact)() PURE;
    STDMETHOD(CreateClipper)(DWORD, IDirectDrawClipper**, IUnknown*) PURE;
    STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, IDirectDrawPalette**, IUnknown*) PURE;
    STDMETHOD(CreateSurface)(DDSURFACEDESC2*, IDirectDrawSurface4**, IUnknown*) PURE;
    STDMETHOD(DuplicateSurface)(IDirectDrawSurface4*, IDirectDrawSurface4**) PURE;
    STDMETHOD(EnumDisplayModes)(DWORD, DDSURFACEDESC2*, LPVOID, void*) PURE;
    STDMETHOD(EnumSurfaces)(DWORD, DDSURFACEDESC2*, LPVOID, void*) PURE;
    STDMETHOD(FlipToGDISurface)() PURE;
    STDMETHOD(GetCaps)(LPDDCAPS, LPDDCAPS) PURE;
    STDMETHOD(GetDisplayMode)(DDSURFACEDESC2*) PURE;
    STDMETHOD(GetFourCCCodes)(DWORD*, DWORD*) PURE;
    STDMETHOD(GetGDISurface)(IDirectDrawSurface4**) PURE;
    STDMETHOD(GetMonitorFrequency)(DWORD*) PURE;
    STDMETHOD(GetScanLine)(DWORD*) PURE;
    STDMETHOD(GetVerticalBlankStatus)(BOOL*) PURE;
    STDMETHOD(Initialize)(GUID*) PURE;
    STDMETHOD(RestoreDisplayMode)() PURE;
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) PURE;
    STDMETHOD(SetDisplayMode)(DWORD, DWORD, DWORD, DWORD, DWORD) PURE;
};
struct IDirectDrawSurface4 : IUnknown
{
    STDMETHOD(AddAttachedSurface)(IDirectDrawSurface4*) PURE;
    STDMETHOD(AddOverlayDirtyRect)(RECT*) PURE;
    STDMETHOD(Blt)(RECT*, IDirectDrawSurface4*, RECT*, DWORD, DDBLTFX*) PURE;
    STDMETHOD(BltBatch)(DDBLTBATCH*, DWORD, DWORD) PURE;
    STDMETHOD(BltFast)(DWORD, DWORD, IDirectDrawSurface4*, RECT*, DWORD) PURE;
    STDMETHOD(DeleteAttachedSurface)(DWORD, IDirectDrawSurface4*) PURE;
    STDMETHOD(EnumAttachedSurfaces)(void*, void*) PURE;
    STDMETHOD(EnumOverlayZOrders)(DWORD, void*, void*) PURE;
    STDMETHOD(Flip)(IDirectDrawSurface4*, DWORD) PURE;
    STDMETHOD(GetAttachedSurface)(DDSCAPS2*, IDirectDrawSurface4**) PURE;
    STDMETHOD(GetBltStatus)(DWORD) PURE;
    STDMETHOD(GetCaps)(DDSCAPS2*) PURE;
    STDMETHOD(GetClipper)(IDirectDrawClipper**) PURE;
    STDMETHOD(GetColorKey)(DWORD, DDCOLORKEY*) PURE;
    STDMETHOD(GetDC)(HDC*) PURE;
    STDMETHOD(GetFlipStatus)(DWORD) PURE;
    STDMETHOD(GetOverlayPosition)(LONG*, LONG*) PURE;
    STDMETHOD(GetPalette)(IDirectDrawPalette**) PURE;
    STDMETHOD(GetPixelFormat)(DDPIXELFORMAT*) PURE;
    STDMETHOD(GetSurfaceDesc)(DDSURFACEDESC2*) PURE;
    STDMETHOD(Initialize)(IDirectDraw*, DDSURFACEDESC2*) PURE;
    STDMETHOD(IsLost)() PURE;
    STDMETHOD(Lock)(RECT*, DDSURFACEDESC2*, DWORD, HANDLE) PURE;
    STDMETHOD(ReleaseDC)(HDC) PURE;
    STDMETHOD(Restore)() PURE;
    STDMETHOD(SetClipper)(IDirectDrawClipper*) PURE;
    STDMETHOD(SetColorKey)(DWORD, DDCOLORKEY*) PURE;
    STDMETHOD(SetOverlayPosition)(LONG, LONG) PURE;
    STDMETHOD(SetPalette)(IDirectDrawPalette*) PURE;
    STDMETHOD(Unlock)(RECT*) PURE;
};
// Let the decompiled SDK fallback see that these interfaces already exist.
#define IDirectDraw4_SetDisplayMode(p, a, b, c, d, e) (p)->SetDisplayMode(a, b, c, d, e)
#define IDirectDrawSurface4_Flip(p, a, b) (p)->Flip(a, b)
extern "C"
{
    extern const GUID IID_IDirectDraw BEACHLIFE_COMPAT_SYMBOL(IID_IDirectDraw), IID_IDirectDraw4
        BEACHLIFE_COMPAT_SYMBOL(IID_IDirectDraw4);
    extern const GUID IID_IDirectDrawSurface BEACHLIFE_COMPAT_SYMBOL(IID_IDirectDrawSurface),
        IID_IDirectDrawSurface4 BEACHLIFE_COMPAT_SYMBOL(IID_IDirectDrawSurface4),
        IID_IDirectDrawClipper BEACHLIFE_COMPAT_SYMBOL(IID_IDirectDrawClipper);
    HRESULT WINAPI DirectDrawCreate(GUID*, IDirectDraw**, IUnknown*)
        BEACHLIFE_COMPAT_SYMBOL(DirectDrawCreate);
    HRESULT WINAPI DirectDrawCreateEx(GUID*, void**, REFIID, IUnknown*)
        BEACHLIFE_COMPAT_SYMBOL(DirectDrawCreateEx);
}

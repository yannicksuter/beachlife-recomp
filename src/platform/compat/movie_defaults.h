// Internal defaults preserve each declared COM slot and stop on unused APIs.
#pragma once
#include "amstream.h"
#include "compat_host.h"
#include "ddstream.h"
namespace beachlife::compat::movie
{
struct IDirectDrawDefaults : IDirectDraw
{
    HRESULT Compact() override
    {
        unsupported("IDirectDraw::Compact", "outside the startup movie API subset");
    }
    HRESULT CreateClipper(DWORD, IDirectDrawClipper**, IUnknown*) override
    {
        unsupported("IDirectDraw::CreateClipper", "outside the startup movie API subset");
    }
    HRESULT CreatePalette(DWORD, LPPALETTEENTRY, IDirectDrawPalette**, IUnknown*) override
    {
        unsupported("IDirectDraw::CreatePalette", "outside the startup movie API subset");
    }
    HRESULT CreateSurface(DDSURFACEDESC*, IDirectDrawSurface**, IUnknown*) override
    {
        unsupported("IDirectDraw::CreateSurface", "outside the startup movie API subset");
    }
    HRESULT DuplicateSurface(IDirectDrawSurface*, IDirectDrawSurface**) override
    {
        unsupported("IDirectDraw::DuplicateSurface", "outside the startup movie API subset");
    }
    HRESULT EnumDisplayModes(DWORD, DDSURFACEDESC*, LPVOID, void*) override
    {
        unsupported("IDirectDraw::EnumDisplayModes", "outside the startup movie API subset");
    }
    HRESULT EnumSurfaces(DWORD, DDSURFACEDESC*, LPVOID, void*) override
    {
        unsupported("IDirectDraw::EnumSurfaces", "outside the startup movie API subset");
    }
    HRESULT FlipToGDISurface() override
    {
        unsupported("IDirectDraw::FlipToGDISurface", "outside the startup movie API subset");
    }
    HRESULT GetCaps(LPDDCAPS, LPDDCAPS) override
    {
        unsupported("IDirectDraw::GetCaps", "outside the startup movie API subset");
    }
    HRESULT GetDisplayMode(DDSURFACEDESC*) override
    {
        unsupported("IDirectDraw::GetDisplayMode", "outside the startup movie API subset");
    }
    HRESULT GetFourCCCodes(DWORD*, DWORD*) override
    {
        unsupported("IDirectDraw::GetFourCCCodes", "outside the startup movie API subset");
    }
    HRESULT GetGDISurface(IDirectDrawSurface**) override
    {
        unsupported("IDirectDraw::GetGDISurface", "outside the startup movie API subset");
    }
    HRESULT GetMonitorFrequency(DWORD*) override
    {
        unsupported("IDirectDraw::GetMonitorFrequency", "outside the startup movie API subset");
    }
    HRESULT GetScanLine(DWORD*) override
    {
        unsupported("IDirectDraw::GetScanLine", "outside the startup movie API subset");
    }
    HRESULT GetVerticalBlankStatus(BOOL*) override
    {
        unsupported("IDirectDraw::GetVerticalBlankStatus", "outside the startup movie API subset");
    }
    HRESULT Initialize(GUID*) override
    {
        unsupported("IDirectDraw::Initialize", "outside the startup movie API subset");
    }
    HRESULT RestoreDisplayMode() override
    {
        unsupported("IDirectDraw::RestoreDisplayMode", "outside the startup movie API subset");
    }
    HRESULT SetCooperativeLevel(HWND, DWORD) override
    {
        unsupported("IDirectDraw::SetCooperativeLevel", "outside the startup movie API subset");
    }
    HRESULT SetDisplayMode(DWORD, DWORD, DWORD) override
    {
        unsupported("IDirectDraw::SetDisplayMode", "outside the startup movie API subset");
    }
};
struct IDirectDraw4Defaults : IDirectDraw4
{
    HRESULT Compact() override
    {
        unsupported("IDirectDraw4::Compact", "outside the startup movie API subset");
    }
    HRESULT CreateClipper(DWORD, IDirectDrawClipper**, IUnknown*) override
    {
        unsupported("IDirectDraw4::CreateClipper", "outside the startup movie API subset");
    }
    HRESULT CreatePalette(DWORD, LPPALETTEENTRY, IDirectDrawPalette**, IUnknown*) override
    {
        unsupported("IDirectDraw4::CreatePalette", "outside the startup movie API subset");
    }
    HRESULT CreateSurface(DDSURFACEDESC2*, IDirectDrawSurface4**, IUnknown*) override
    {
        unsupported("IDirectDraw4::CreateSurface", "outside the startup movie API subset");
    }
    HRESULT DuplicateSurface(IDirectDrawSurface4*, IDirectDrawSurface4**) override
    {
        unsupported("IDirectDraw4::DuplicateSurface", "outside the startup movie API subset");
    }
    HRESULT EnumDisplayModes(DWORD, DDSURFACEDESC2*, LPVOID, void*) override
    {
        unsupported("IDirectDraw4::EnumDisplayModes", "outside the startup movie API subset");
    }
    HRESULT EnumSurfaces(DWORD, DDSURFACEDESC2*, LPVOID, void*) override
    {
        unsupported("IDirectDraw4::EnumSurfaces", "outside the startup movie API subset");
    }
    HRESULT FlipToGDISurface() override
    {
        unsupported("IDirectDraw4::FlipToGDISurface", "outside the startup movie API subset");
    }
    HRESULT GetCaps(LPDDCAPS, LPDDCAPS) override
    {
        unsupported("IDirectDraw4::GetCaps", "outside the startup movie API subset");
    }
    HRESULT GetDisplayMode(DDSURFACEDESC2*) override
    {
        unsupported("IDirectDraw4::GetDisplayMode", "outside the startup movie API subset");
    }
    HRESULT GetFourCCCodes(DWORD*, DWORD*) override
    {
        unsupported("IDirectDraw4::GetFourCCCodes", "outside the startup movie API subset");
    }
    HRESULT GetGDISurface(IDirectDrawSurface4**) override
    {
        unsupported("IDirectDraw4::GetGDISurface", "outside the startup movie API subset");
    }
    HRESULT GetMonitorFrequency(DWORD*) override
    {
        unsupported("IDirectDraw4::GetMonitorFrequency", "outside the startup movie API subset");
    }
    HRESULT GetScanLine(DWORD*) override
    {
        unsupported("IDirectDraw4::GetScanLine", "outside the startup movie API subset");
    }
    HRESULT GetVerticalBlankStatus(BOOL*) override
    {
        unsupported("IDirectDraw4::GetVerticalBlankStatus", "outside the startup movie API subset");
    }
    HRESULT Initialize(GUID*) override
    {
        unsupported("IDirectDraw4::Initialize", "outside the startup movie API subset");
    }
    HRESULT RestoreDisplayMode() override
    {
        unsupported("IDirectDraw4::RestoreDisplayMode", "outside the startup movie API subset");
    }
    HRESULT SetCooperativeLevel(HWND, DWORD) override
    {
        unsupported("IDirectDraw4::SetCooperativeLevel", "outside the startup movie API subset");
    }
    HRESULT SetDisplayMode(DWORD, DWORD, DWORD, DWORD, DWORD) override
    {
        unsupported("IDirectDraw4::SetDisplayMode", "outside the startup movie API subset");
    }
};
struct IDirectDrawSurfaceDefaults : IDirectDrawSurface
{
    HRESULT AddAttachedSurface(IDirectDrawSurface*) override
    {
        unsupported(
            "IDirectDrawSurface::AddAttachedSurface", "outside the startup movie API subset");
    }
    HRESULT AddOverlayDirtyRect(RECT*) override
    {
        unsupported(
            "IDirectDrawSurface::AddOverlayDirtyRect", "outside the startup movie API subset");
    }
    HRESULT Blt(RECT*, IDirectDrawSurface*, RECT*, DWORD, DDBLTFX*) override
    {
        unsupported("IDirectDrawSurface::Blt", "outside the startup movie API subset");
    }
    HRESULT BltBatch(DDBLTBATCH*, DWORD, DWORD) override
    {
        unsupported("IDirectDrawSurface::BltBatch", "outside the startup movie API subset");
    }
    HRESULT BltFast(DWORD, DWORD, IDirectDrawSurface*, RECT*, DWORD) override
    {
        unsupported("IDirectDrawSurface::BltFast", "outside the startup movie API subset");
    }
    HRESULT DeleteAttachedSurface(DWORD, IDirectDrawSurface*) override
    {
        unsupported(
            "IDirectDrawSurface::DeleteAttachedSurface", "outside the startup movie API subset");
    }
    HRESULT EnumAttachedSurfaces(void*, void*) override
    {
        unsupported(
            "IDirectDrawSurface::EnumAttachedSurfaces", "outside the startup movie API subset");
    }
    HRESULT EnumOverlayZOrders(DWORD, void*, void*) override
    {
        unsupported(
            "IDirectDrawSurface::EnumOverlayZOrders", "outside the startup movie API subset");
    }
    HRESULT Flip(IDirectDrawSurface*, DWORD) override
    {
        unsupported("IDirectDrawSurface::Flip", "outside the startup movie API subset");
    }
    HRESULT GetAttachedSurface(DDSCAPS*, IDirectDrawSurface**) override
    {
        unsupported(
            "IDirectDrawSurface::GetAttachedSurface", "outside the startup movie API subset");
    }
    HRESULT GetBltStatus(DWORD) override
    {
        unsupported("IDirectDrawSurface::GetBltStatus", "outside the startup movie API subset");
    }
    HRESULT GetCaps(DDSCAPS*) override
    {
        unsupported("IDirectDrawSurface::GetCaps", "outside the startup movie API subset");
    }
    HRESULT GetClipper(IDirectDrawClipper**) override
    {
        unsupported("IDirectDrawSurface::GetClipper", "outside the startup movie API subset");
    }
    HRESULT GetColorKey(DWORD, DDCOLORKEY*) override
    {
        unsupported("IDirectDrawSurface::GetColorKey", "outside the startup movie API subset");
    }
    HRESULT GetDC(HDC*) override
    {
        unsupported("IDirectDrawSurface::GetDC", "outside the startup movie API subset");
    }
    HRESULT GetFlipStatus(DWORD) override
    {
        unsupported("IDirectDrawSurface::GetFlipStatus", "outside the startup movie API subset");
    }
    HRESULT GetOverlayPosition(LONG*, LONG*) override
    {
        unsupported(
            "IDirectDrawSurface::GetOverlayPosition", "outside the startup movie API subset");
    }
    HRESULT GetPalette(IDirectDrawPalette**) override
    {
        unsupported("IDirectDrawSurface::GetPalette", "outside the startup movie API subset");
    }
    HRESULT GetPixelFormat(DDPIXELFORMAT*) override
    {
        unsupported("IDirectDrawSurface::GetPixelFormat", "outside the startup movie API subset");
    }
    HRESULT GetSurfaceDesc(DDSURFACEDESC*) override
    {
        unsupported("IDirectDrawSurface::GetSurfaceDesc", "outside the startup movie API subset");
    }
    HRESULT Initialize(IDirectDraw*, DDSURFACEDESC*) override
    {
        unsupported("IDirectDrawSurface::Initialize", "outside the startup movie API subset");
    }
    HRESULT IsLost() override
    {
        unsupported("IDirectDrawSurface::IsLost", "outside the startup movie API subset");
    }
    HRESULT Lock(RECT*, DDSURFACEDESC*, DWORD, HANDLE) override
    {
        unsupported("IDirectDrawSurface::Lock", "outside the startup movie API subset");
    }
    HRESULT ReleaseDC(HDC) override
    {
        unsupported("IDirectDrawSurface::ReleaseDC", "outside the startup movie API subset");
    }
    HRESULT Restore() override
    {
        unsupported("IDirectDrawSurface::Restore", "outside the startup movie API subset");
    }
    HRESULT SetClipper(IDirectDrawClipper*) override
    {
        unsupported("IDirectDrawSurface::SetClipper", "outside the startup movie API subset");
    }
    HRESULT SetColorKey(DWORD, DDCOLORKEY*) override
    {
        unsupported("IDirectDrawSurface::SetColorKey", "outside the startup movie API subset");
    }
    HRESULT SetOverlayPosition(LONG, LONG) override
    {
        unsupported(
            "IDirectDrawSurface::SetOverlayPosition", "outside the startup movie API subset");
    }
    HRESULT SetPalette(IDirectDrawPalette*) override
    {
        unsupported("IDirectDrawSurface::SetPalette", "outside the startup movie API subset");
    }
    HRESULT Unlock(LPVOID) override
    {
        unsupported("IDirectDrawSurface::Unlock", "outside the startup movie API subset");
    }
};
struct IDirectDrawSurface4Defaults : IDirectDrawSurface4
{
    HRESULT AddAttachedSurface(IDirectDrawSurface4*) override
    {
        unsupported(
            "IDirectDrawSurface4::AddAttachedSurface", "outside the startup movie API subset");
    }
    HRESULT AddOverlayDirtyRect(RECT*) override
    {
        unsupported(
            "IDirectDrawSurface4::AddOverlayDirtyRect", "outside the startup movie API subset");
    }
    HRESULT Blt(RECT*, IDirectDrawSurface4*, RECT*, DWORD, DDBLTFX*) override
    {
        unsupported("IDirectDrawSurface4::Blt", "outside the startup movie API subset");
    }
    HRESULT BltBatch(DDBLTBATCH*, DWORD, DWORD) override
    {
        unsupported("IDirectDrawSurface4::BltBatch", "outside the startup movie API subset");
    }
    HRESULT BltFast(DWORD, DWORD, IDirectDrawSurface4*, RECT*, DWORD) override
    {
        unsupported("IDirectDrawSurface4::BltFast", "outside the startup movie API subset");
    }
    HRESULT DeleteAttachedSurface(DWORD, IDirectDrawSurface4*) override
    {
        unsupported(
            "IDirectDrawSurface4::DeleteAttachedSurface", "outside the startup movie API subset");
    }
    HRESULT EnumAttachedSurfaces(void*, void*) override
    {
        unsupported(
            "IDirectDrawSurface4::EnumAttachedSurfaces", "outside the startup movie API subset");
    }
    HRESULT EnumOverlayZOrders(DWORD, void*, void*) override
    {
        unsupported(
            "IDirectDrawSurface4::EnumOverlayZOrders", "outside the startup movie API subset");
    }
    HRESULT Flip(IDirectDrawSurface4*, DWORD) override
    {
        unsupported("IDirectDrawSurface4::Flip", "outside the startup movie API subset");
    }
    HRESULT GetAttachedSurface(DDSCAPS2*, IDirectDrawSurface4**) override
    {
        unsupported(
            "IDirectDrawSurface4::GetAttachedSurface", "outside the startup movie API subset");
    }
    HRESULT GetBltStatus(DWORD) override
    {
        unsupported("IDirectDrawSurface4::GetBltStatus", "outside the startup movie API subset");
    }
    HRESULT GetCaps(DDSCAPS2*) override
    {
        unsupported("IDirectDrawSurface4::GetCaps", "outside the startup movie API subset");
    }
    HRESULT GetClipper(IDirectDrawClipper**) override
    {
        unsupported("IDirectDrawSurface4::GetClipper", "outside the startup movie API subset");
    }
    HRESULT GetColorKey(DWORD, DDCOLORKEY*) override
    {
        unsupported("IDirectDrawSurface4::GetColorKey", "outside the startup movie API subset");
    }
    HRESULT GetDC(HDC*) override
    {
        unsupported("IDirectDrawSurface4::GetDC", "outside the startup movie API subset");
    }
    HRESULT GetFlipStatus(DWORD) override
    {
        unsupported("IDirectDrawSurface4::GetFlipStatus", "outside the startup movie API subset");
    }
    HRESULT GetOverlayPosition(LONG*, LONG*) override
    {
        unsupported(
            "IDirectDrawSurface4::GetOverlayPosition", "outside the startup movie API subset");
    }
    HRESULT GetPalette(IDirectDrawPalette**) override
    {
        unsupported("IDirectDrawSurface4::GetPalette", "outside the startup movie API subset");
    }
    HRESULT GetPixelFormat(DDPIXELFORMAT*) override
    {
        unsupported("IDirectDrawSurface4::GetPixelFormat", "outside the startup movie API subset");
    }
    HRESULT GetSurfaceDesc(DDSURFACEDESC2*) override
    {
        unsupported("IDirectDrawSurface4::GetSurfaceDesc", "outside the startup movie API subset");
    }
    HRESULT Initialize(IDirectDraw*, DDSURFACEDESC2*) override
    {
        unsupported("IDirectDrawSurface4::Initialize", "outside the startup movie API subset");
    }
    HRESULT IsLost() override
    {
        unsupported("IDirectDrawSurface4::IsLost", "outside the startup movie API subset");
    }
    HRESULT Lock(RECT*, DDSURFACEDESC2*, DWORD, HANDLE) override
    {
        unsupported("IDirectDrawSurface4::Lock", "outside the startup movie API subset");
    }
    HRESULT ReleaseDC(HDC) override
    {
        unsupported("IDirectDrawSurface4::ReleaseDC", "outside the startup movie API subset");
    }
    HRESULT Restore() override
    {
        unsupported("IDirectDrawSurface4::Restore", "outside the startup movie API subset");
    }
    HRESULT SetClipper(IDirectDrawClipper*) override
    {
        unsupported("IDirectDrawSurface4::SetClipper", "outside the startup movie API subset");
    }
    HRESULT SetColorKey(DWORD, DDCOLORKEY*) override
    {
        unsupported("IDirectDrawSurface4::SetColorKey", "outside the startup movie API subset");
    }
    HRESULT SetOverlayPosition(LONG, LONG) override
    {
        unsupported(
            "IDirectDrawSurface4::SetOverlayPosition", "outside the startup movie API subset");
    }
    HRESULT SetPalette(IDirectDrawPalette*) override
    {
        unsupported("IDirectDrawSurface4::SetPalette", "outside the startup movie API subset");
    }
    HRESULT Unlock(RECT*) override
    {
        unsupported("IDirectDrawSurface4::Unlock", "outside the startup movie API subset");
    }
};
struct IDirectDrawClipperDefaults : IDirectDrawClipper
{
    HRESULT GetClipList(RECT*, void*, DWORD*) override
    {
        unsupported("IDirectDrawClipper::GetClipList", "outside the startup movie API subset");
    }
    HRESULT GetHWnd(HWND*) override
    {
        unsupported("IDirectDrawClipper::GetHWnd", "outside the startup movie API subset");
    }
    HRESULT Initialize(IDirectDraw*, DWORD) override
    {
        unsupported("IDirectDrawClipper::Initialize", "outside the startup movie API subset");
    }
    HRESULT IsClipListChanged(BOOL*) override
    {
        unsupported(
            "IDirectDrawClipper::IsClipListChanged", "outside the startup movie API subset");
    }
    HRESULT SetClipList(void*, DWORD) override
    {
        unsupported("IDirectDrawClipper::SetClipList", "outside the startup movie API subset");
    }
    HRESULT SetHWnd(DWORD, HWND) override
    {
        unsupported("IDirectDrawClipper::SetHWnd", "outside the startup movie API subset");
    }
};
struct IAMMultiMediaStreamDefaults : IAMMultiMediaStream
{
    HRESULT GetInformation(DWORD*, STREAM_TYPE*) override
    {
        unsupported("IAMMultiMediaStream::GetInformation", "outside the startup movie API subset");
    }
    HRESULT GetMediaStream(REFMSPID, IMediaStream**) override
    {
        unsupported("IAMMultiMediaStream::GetMediaStream", "outside the startup movie API subset");
    }
    HRESULT EnumMediaStreams(LONG, IMediaStream**) override
    {
        unsupported(
            "IAMMultiMediaStream::EnumMediaStreams", "outside the startup movie API subset");
    }
    HRESULT GetState(STREAM_STATE*) override
    {
        unsupported("IAMMultiMediaStream::GetState", "outside the startup movie API subset");
    }
    HRESULT SetState(STREAM_STATE) override
    {
        unsupported("IAMMultiMediaStream::SetState", "outside the startup movie API subset");
    }
    HRESULT GetTime(STREAM_TIME*) override
    {
        unsupported("IAMMultiMediaStream::GetTime", "outside the startup movie API subset");
    }
    HRESULT GetDuration(STREAM_TIME*) override
    {
        unsupported("IAMMultiMediaStream::GetDuration", "outside the startup movie API subset");
    }
    HRESULT Seek(STREAM_TIME) override
    {
        unsupported("IAMMultiMediaStream::Seek", "outside the startup movie API subset");
    }
    HRESULT GetEndOfStreamEventHandle(HANDLE*) override
    {
        unsupported("IAMMultiMediaStream::GetEndOfStreamEventHandle",
            "outside the startup movie API subset");
    }
    HRESULT Initialize(STREAM_TYPE, DWORD, IGraphBuilder*) override
    {
        unsupported("IAMMultiMediaStream::Initialize", "outside the startup movie API subset");
    }
    HRESULT GetFilterGraph(IGraphBuilder**) override
    {
        unsupported("IAMMultiMediaStream::GetFilterGraph", "outside the startup movie API subset");
    }
    HRESULT GetFilter(IMediaStreamFilter**) override
    {
        unsupported("IAMMultiMediaStream::GetFilter", "outside the startup movie API subset");
    }
    HRESULT AddMediaStream(IUnknown*, const MSPID*, DWORD, IMediaStream**) override
    {
        unsupported("IAMMultiMediaStream::AddMediaStream", "outside the startup movie API subset");
    }
    HRESULT OpenFile(LPCWSTR, DWORD) override
    {
        unsupported("IAMMultiMediaStream::OpenFile", "outside the startup movie API subset");
    }
    HRESULT OpenMoniker(IBindCtx*, IMoniker*, DWORD) override
    {
        unsupported("IAMMultiMediaStream::OpenMoniker", "outside the startup movie API subset");
    }
    HRESULT Render(DWORD) override
    {
        unsupported("IAMMultiMediaStream::Render", "outside the startup movie API subset");
    }
};
struct IDirectDrawMediaStreamDefaults : IDirectDrawMediaStream
{
    HRESULT GetMultiMediaStream(IMultiMediaStream**) override
    {
        unsupported(
            "IDirectDrawMediaStream::GetMultiMediaStream", "outside the startup movie API subset");
    }
    HRESULT GetInformation(MSPID*, STREAM_TYPE*) override
    {
        unsupported(
            "IDirectDrawMediaStream::GetInformation", "outside the startup movie API subset");
    }
    HRESULT SetSameFormat(IMediaStream*, DWORD) override
    {
        unsupported(
            "IDirectDrawMediaStream::SetSameFormat", "outside the startup movie API subset");
    }
    HRESULT AllocateSample(DWORD, IStreamSample**) override
    {
        unsupported(
            "IDirectDrawMediaStream::AllocateSample", "outside the startup movie API subset");
    }
    HRESULT CreateSharedSample(IStreamSample*, DWORD, IStreamSample**) override
    {
        unsupported(
            "IDirectDrawMediaStream::CreateSharedSample", "outside the startup movie API subset");
    }
    HRESULT SendEndOfStream(DWORD) override
    {
        unsupported(
            "IDirectDrawMediaStream::SendEndOfStream", "outside the startup movie API subset");
    }
    HRESULT GetFormat(DDSURFACEDESC*, IDirectDrawPalette**, DDSURFACEDESC*, DWORD*) override
    {
        unsupported("IDirectDrawMediaStream::GetFormat", "outside the startup movie API subset");
    }
    HRESULT SetFormat(const DDSURFACEDESC*, IDirectDrawPalette*) override
    {
        unsupported("IDirectDrawMediaStream::SetFormat", "outside the startup movie API subset");
    }
    HRESULT GetDirectDraw(IDirectDraw**) override
    {
        unsupported(
            "IDirectDrawMediaStream::GetDirectDraw", "outside the startup movie API subset");
    }
    HRESULT SetDirectDraw(IDirectDraw*) override
    {
        unsupported(
            "IDirectDrawMediaStream::SetDirectDraw", "outside the startup movie API subset");
    }
    HRESULT CreateSample(
        IDirectDrawSurface*, const RECT*, DWORD, IDirectDrawStreamSample**) override
    {
        unsupported("IDirectDrawMediaStream::CreateSample", "outside the startup movie API subset");
    }
    HRESULT GetTimePerFrame(STREAM_TIME*) override
    {
        unsupported(
            "IDirectDrawMediaStream::GetTimePerFrame", "outside the startup movie API subset");
    }
};
struct IDirectDrawStreamSampleDefaults : IDirectDrawStreamSample
{
    HRESULT GetMediaStream(IMediaStream**) override
    {
        unsupported(
            "IDirectDrawStreamSample::GetMediaStream", "outside the startup movie API subset");
    }
    HRESULT GetSampleTimes(STREAM_TIME*, STREAM_TIME*, STREAM_TIME*) override
    {
        unsupported(
            "IDirectDrawStreamSample::GetSampleTimes", "outside the startup movie API subset");
    }
    HRESULT SetSampleTimes(const STREAM_TIME*, const STREAM_TIME*) override
    {
        unsupported(
            "IDirectDrawStreamSample::SetSampleTimes", "outside the startup movie API subset");
    }
    HRESULT Update(DWORD, HANDLE, PAPCFUNC, DWORD) override
    {
        unsupported("IDirectDrawStreamSample::Update", "outside the startup movie API subset");
    }
    HRESULT CompletionStatus(DWORD, DWORD) override
    {
        unsupported(
            "IDirectDrawStreamSample::CompletionStatus", "outside the startup movie API subset");
    }
    HRESULT GetSurface(IDirectDrawSurface**, RECT*) override
    {
        unsupported("IDirectDrawStreamSample::GetSurface", "outside the startup movie API subset");
    }
    HRESULT SetRect(const RECT*) override
    {
        unsupported("IDirectDrawStreamSample::SetRect", "outside the startup movie API subset");
    }
};
} // namespace beachlife::compat::movie

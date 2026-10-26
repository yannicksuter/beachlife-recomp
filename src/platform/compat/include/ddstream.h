#pragma once
#include "mmstream.h"
struct IDirectDrawStreamSample;
struct IDirectDrawMediaStream : IMediaStream
{
    STDMETHOD(GetFormat)(DDSURFACEDESC*, IDirectDrawPalette**, DDSURFACEDESC*, DWORD*) PURE;
    STDMETHOD(SetFormat)(const DDSURFACEDESC*, IDirectDrawPalette*) PURE;
    STDMETHOD(GetDirectDraw)(IDirectDraw**) PURE;
    STDMETHOD(SetDirectDraw)(IDirectDraw*) PURE;
    STDMETHOD(CreateSample)(
        IDirectDrawSurface*, const RECT*, DWORD, IDirectDrawStreamSample**) PURE;
    STDMETHOD(GetTimePerFrame)(STREAM_TIME*) PURE;
};
struct IDirectDrawStreamSample : IStreamSample
{
    STDMETHOD(GetSurface)(IDirectDrawSurface**, RECT*) PURE;
    STDMETHOD(SetRect)(const RECT*) PURE;
};
extern "C"
{
    extern const GUID IID_IDirectDrawMediaStream BEACHLIFE_COMPAT_SYMBOL(
        IID_IDirectDrawMediaStream),
        IID_IDirectDrawStreamSample BEACHLIFE_COMPAT_SYMBOL(IID_IDirectDrawStreamSample);
}

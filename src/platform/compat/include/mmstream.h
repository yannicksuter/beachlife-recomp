// Multimedia-stream COM interfaces used by the retail movie player.
#pragma once
#include "ddraw.h"
using STREAM_TIME = int64_t;
using MSPID = GUID;
using REFMSPID = REFGUID;
using PAPCFUNC = void(CALLBACK*)(uintptr_t);
enum STREAM_TYPE
{
    STREAMTYPE_READ,
    STREAMTYPE_WRITE,
    STREAMTYPE_TRANSFORM
};
enum STREAM_STATE
{
    STREAMSTATE_STOP,
    STREAMSTATE_RUN
};
#define MS_S_ENDOFSTREAM ((HRESULT)0x00040003)
#define MS_E_NOSTREAM ((HRESULT)0x80040403)
#define MS_E_NOTRUNNING ((HRESULT)0x8004040a)
#ifndef VK_ESCAPE
#define VK_ESCAPE 0x1b
#endif
struct IMediaStream;
struct IStreamSample;
struct IMultiMediaStream : IUnknown
{
    STDMETHOD(GetInformation)(DWORD*, STREAM_TYPE*) PURE;
    STDMETHOD(GetMediaStream)(REFMSPID, IMediaStream**) PURE;
    STDMETHOD(EnumMediaStreams)(LONG, IMediaStream**) PURE;
    STDMETHOD(GetState)(STREAM_STATE*) PURE;
    STDMETHOD(SetState)(STREAM_STATE) PURE;
    STDMETHOD(GetTime)(STREAM_TIME*) PURE;
    STDMETHOD(GetDuration)(STREAM_TIME*) PURE;
    STDMETHOD(Seek)(STREAM_TIME) PURE;
    STDMETHOD(GetEndOfStreamEventHandle)(HANDLE*) PURE;
};
struct IMediaStream : IUnknown
{
    STDMETHOD(GetMultiMediaStream)(IMultiMediaStream**) PURE;
    STDMETHOD(GetInformation)(MSPID*, STREAM_TYPE*) PURE;
    STDMETHOD(SetSameFormat)(IMediaStream*, DWORD) PURE;
    STDMETHOD(AllocateSample)(DWORD, IStreamSample**) PURE;
    STDMETHOD(CreateSharedSample)(IStreamSample*, DWORD, IStreamSample**) PURE;
    STDMETHOD(SendEndOfStream)(DWORD) PURE;
};
struct IStreamSample : IUnknown
{
    STDMETHOD(GetMediaStream)(IMediaStream**) PURE;
    STDMETHOD(GetSampleTimes)(STREAM_TIME*, STREAM_TIME*, STREAM_TIME*) PURE;
    STDMETHOD(SetSampleTimes)(const STREAM_TIME*, const STREAM_TIME*) PURE;
    STDMETHOD(Update)(DWORD, HANDLE, PAPCFUNC, DWORD) PURE;
    STDMETHOD(CompletionStatus)(DWORD, DWORD) PURE;
};
extern "C"
{
    extern const GUID MSPID_PrimaryVideo, MSPID_PrimaryAudio;
    extern const GUID IID_IMultiMediaStream, IID_IMediaStream, IID_IStreamSample;
}

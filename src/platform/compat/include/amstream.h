#pragma once
#include "mmstream.h"
#ifndef CLSCTX_INPROC_SERVER
#define CLSCTX_INPROC_SERVER 1
#endif
#define AMMSF_NOGRAPHTHREAD 0x00000001
#define AMMSF_ADDDEFAULTRENDERER 0x00000001
struct IGraphBuilder;
struct IMediaStreamFilter;
struct IBindCtx;
struct IMoniker;
struct IAMMultiMediaStream : IMultiMediaStream
{
    STDMETHOD(Initialize)(STREAM_TYPE, DWORD, IGraphBuilder*) PURE;
    STDMETHOD(GetFilterGraph)(IGraphBuilder**) PURE;
    STDMETHOD(GetFilter)(IMediaStreamFilter**) PURE;
    STDMETHOD(AddMediaStream)(IUnknown*, const MSPID*, DWORD, IMediaStream**) PURE;
    STDMETHOD(OpenFile)(LPCWSTR, DWORD) PURE;
    STDMETHOD(OpenMoniker)(IBindCtx*, IMoniker*, DWORD) PURE;
    STDMETHOD(Render)(DWORD) PURE;
};
extern "C"
{
    extern const GUID CLSID_AMMultiMediaStream, IID_IAMMultiMediaStream;
}

#include "amstream.h"
#include "compat_host.h"
#include "shell_link.h"

#include <cstring>

namespace beachlife::compat::movie
{
HRESULT create_stream_instance(REFIID iid, LPVOID* output);
}

extern "C"
{
    HRESULT WINAPI CoInitialize(LPVOID)
    {
        return S_OK;
    }

    void WINAPI CoUninitialize(void) {}

    HRESULT WINAPI CoCreateInstance(
        REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID* output)
    {
        using namespace beachlife::compat;
        if (!output)
        {
            return static_cast<HRESULT>(0x80004003);
        }
        *output = nullptr;
        if (outer || context != CLSCTX_INPROC_SERVER)
        {
            unsupported(
                "CoCreateInstance", "only non-aggregated in-process objects are implemented");
        }
        if (!std::memcmp(&clsid, &CLSID_ShellLink, sizeof(GUID)))
        {
            return shell::create(iid, output);
        }
        if (!std::memcmp(&clsid, &CLSID_AMMultiMediaStream, sizeof(GUID)))
        {
            return movie::create_stream_instance(iid, output);
        }
        unsupported("CoCreateInstance", "only ShellLink and AMMultiMediaStream are implemented");
    }
} // extern "C"

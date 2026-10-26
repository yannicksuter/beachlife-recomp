#include "amstream.h"
#include "compat_host.h"
#include "shell_link.h"

#include <cstring>

namespace
{
thread_local unsigned int initialization_count = 0;
}

namespace beachlife::compat::movie
{
HRESULT create_stream_instance(REFIID iid, LPVOID* output);
}

extern "C"
{
    HRESULT WINAPI CoInitialize(LPVOID reserved)
    {
        if (reserved)
        {
            return static_cast<HRESULT>(0x80070057); // E_INVALIDARG
        }
        // The supported in-process interfaces are host implementations and
        // need no Windows apartment, but retain COM's per-thread nesting.
        return initialization_count++ ? 1 : S_OK; // S_FALSE on repeated initialization
    }

    void WINAPI CoUninitialize(void)
    {
        if (initialization_count)
        {
            --initialization_count;
        }
    }

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

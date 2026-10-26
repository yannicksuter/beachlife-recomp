#pragma once

#include <SDL3/SDL.h>

#include <cstdlib>
#include <string>

// Windows has no fork. Run the same fixture in a child, capture its diagnostic
// and require UCRT's abort status. No native SDK headers enter compat tests.
#ifdef _WIN32
inline bool compat_abort_child(const char* executable, const char* argument, const char* message)
{
    const char* arguments[] = {executable, argument, nullptr};
    const auto properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process)
    {
        return false;
    }
    std::size_t bytes = 0;
    int status = 0;
    void* output = SDL_ReadProcess(process, &bytes, &status);
    const std::string text(output ? static_cast<const char*>(output) : "", bytes);
    SDL_free(output);
    SDL_DestroyProcess(process);
    return status == 3 && text.find(message) != std::string::npos;
}

inline void compat_disable_abort_dialog()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}
#endif

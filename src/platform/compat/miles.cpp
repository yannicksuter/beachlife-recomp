#include "mss.h"

#include "compat_host.h"
#include "miles_audio.h"
#include "runtime/retail_files.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace audio = beachlife::compat::audio;
namespace files = beachlife::runtime::retail_files;
using beachlife::compat::unsupported;

struct Driver
{
    ~Driver()
    {
        // Stop and join the callback before destroying the voices or mutex.
        if (output)
        {
            SDL_DestroyAudioStream(output);
        }
    }

    int frequency = 0;
    SDL_AudioStream* output = nullptr;
    std::mutex mutex;
    audio::Mixer mixer;
};

struct Provider
{
    Driver* driver = nullptr;
    bool listener_open = false;
    struct Listener
    {
    } listener;
};

struct State
{
    // Public calls serialize ownership changes; the callback only takes its
    // driver's mutex. Never destroy an SDL stream while holding that mutex.
    std::mutex mutex;
    bool started = false;
    std::string redist_directory;
    std::vector<std::unique_ptr<Driver>> drivers;
    Provider provider;
} state;

template <class F> decltype(auto) call(const char* api, F&& function) noexcept
{
    try
    {
        std::lock_guard lock(state.mutex);
        return function();
    }
    catch (...)
    {
        unsupported(api, "host allocation or synchronization failed");
    }
}

Driver& driver(void* handle, const char* api)
{
    for (auto& candidate : state.drivers)
    {
        if (candidate.get() == handle)
        {
            return *candidate;
        }
    }
    unsupported(api, "invalid or closed digital driver");
}

std::pair<Driver*, audio::Voice*> voice(void* handle, audio::Kind kind, const char* api)
{
    for (auto& candidate : state.drivers)
    {
        // The callback never changes the vector or a voice's kind.
        for (auto& entry : candidate->mixer.voices)
        {
            if (entry.get() == handle && entry->kind == kind)
            {
                return {candidate.get(), entry.get()};
            }
        }
    }
    unsupported(api, "invalid, closed or wrong-kind audio handle");
}

template <class F>
decltype(auto) sample_call(const char* api, void* handle, audio::Kind kind, F&& function)
{
    return call(api,
        [&]() -> decltype(auto)
        {
            auto [owner, sample] = voice(handle, kind, api);
            std::lock_guard lock(owner->mutex);
            return function(*sample);
        });
}

Provider& provider(void* handle, const char* api)
{
    if (handle != &state.provider || !state.provider.driver)
    {
        unsupported(api, "invalid or closed 3D provider");
    }
    return state.provider;
}

void SDLCALL fill_audio(void* userdata, SDL_AudioStream* stream, int additional, int)
{
    if (additional <= 0)
    {
        return;
    }
    try
    {
        auto& owner = *static_cast<Driver*>(userdata);
        std::array<float, 1024> buffer;
        auto frames = (std::size_t(additional) + 2 * sizeof(float) - 1) / (2 * sizeof(float));
        while (frames)
        {
            const auto count = std::min(frames, buffer.size() / 2);
            {
                std::lock_guard lock(owner.mutex);
                owner.mixer.render(std::span(buffer).first(count * 2));
            }
            if (!SDL_PutAudioStreamData(stream, buffer.data(), int(count * 2 * sizeof(float))))
            {
                unsupported("Miles audio callback", SDL_GetError());
            }
            frames -= count;
        }
    }
    catch (...)
    {
        unsupported("Miles audio callback", "host synchronization failed");
    }
}

bool extension(const char* name, const char* expected)
{
    if (!name)
    {
        return false;
    }
    const auto* dot = std::strrchr(name, '.');
    return SDL_strcasecmp(dot ? dot + 1 : name, expected) == 0;
}

bool decode(const char* api, std::span<const unsigned char> bytes, Driver& owner, audio::Clip& clip)
{
    const auto result = audio::decode_file(bytes, owner.frequency, clip);
    if (result == audio::DecodeResult::unsupported)
    {
        unsupported(api, "only PCM/IMA ADPCM WAVE and MP3 files are supported");
    }
    return result == audio::DecodeResult::success;
}

struct CloseFile
{
    void operator()(files::File* file) const
    {
        files::close(file);
    }
};

bool read_file(const char* filename, std::vector<unsigned char>& bytes)
{
    const std::unique_ptr<files::File, CloseFile> file(files::open(filename, "rb"));
    files::Position size = 0;
    if (!file || files::seek(file.get(), 0, files::SeekOrigin::end) != 0 ||
        files::position(file.get(), &size) != 0 || !size ||
        size > files::Position(std::numeric_limits<int>::max()) ||
        files::seek(file.get(), 0, files::SeekOrigin::begin) != 0)
    {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    return files::read(bytes.data(), 1, bytes.size(), file.get()) == bytes.size();
}

void shutdown()
{
    state.drivers.clear();
    state.provider = {};
    if (state.started)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        state.started = false;
    }
}
} // namespace

extern "C" void AIL_set_redist_directory(const char* directory)
{
    call("AIL_set_redist_directory",
        [&]
        {
            // The built-in software provider needs no DLLs. Retain the requested
            // search directory; no file is opened by this configuration call.
            state.redist_directory = directory ? directory : "";
        });
}

extern "C" int AIL_startup()
{
    return call("AIL_startup",
        []
        {
            if (!state.started)
            {
                state.started = SDL_InitSubSystem(SDL_INIT_AUDIO);
            }
            return state.started ? 1 : 0;
        });
}

extern "C" void AIL_shutdown()
{
    call("AIL_shutdown", shutdown);
}

extern "C" void* AIL_open_digital_driver(
    unsigned int frequency, int bits, int channels, unsigned int flags)
{
    return call("AIL_open_digital_driver",
        [&]() -> void*
        {
            if (!state.started)
            {
                return nullptr;
            }
            if (!frequency ||
                frequency > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
                bits != 16 || channels != 2 || flags != 0)
            {
                unsupported("AIL_open_digital_driver", "requires 16-bit stereo and default flags");
            }
            auto result = std::make_unique<Driver>();
            result->frequency = static_cast<int>(frequency);
            const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, result->frequency};
            result->output = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, fill_audio, result.get());
            if (!result->output)
            {
                return nullptr;
            }
            auto* handle = result.get();
            state.drivers.push_back(std::move(result));
            if (!SDL_ResumeAudioStreamDevice(handle->output))
            {
                state.drivers.pop_back();
                return nullptr;
            }
            return handle;
        });
}

extern "C" void AIL_close_digital_driver(void* handle)
{
    call("AIL_close_digital_driver",
        [&]
        {
            auto& owner = driver(handle, "AIL_close_digital_driver");
            if (state.provider.driver == &owner)
            {
                state.provider = {};
            }
            std::erase_if(state.drivers,
                [&](const auto& entry)
                {
                    return entry.get() == &owner;
                });
        });
}

extern "C" void AIL_set_digital_master_volume(void* handle, int volume)
{
    call("AIL_set_digital_master_volume",
        [&]
        {
            auto& owner = driver(handle, "AIL_set_digital_master_volume");
            std::lock_guard lock(owner.mutex);
            owner.mixer.volume = std::clamp(volume, 0, 127);
        });
}

extern "C" void* AIL_allocate_sample_handle(void* handle)
{
    return call("AIL_allocate_sample_handle",
        [&]() -> void*
        {
            auto& owner = driver(handle, "AIL_allocate_sample_handle");
            std::lock_guard lock(owner.mutex);
            return owner.mixer.allocate(audio::Kind::sample);
        });
}

extern "C" void AIL_init_sample(void* handle)
{
    sample_call("AIL_init_sample",
        handle,
        audio::Kind::sample,
        [](auto& sample)
        {
            sample = audio::Voice(audio::Kind::sample);
        });
}

extern "C" void AIL_set_named_sample_file(
    void* handle, const char* name, const void* data, unsigned int bytes, int block)
{
    call("AIL_set_named_sample_file",
        [&]
        {
            constexpr auto api = "AIL_set_named_sample_file";
            auto [owner, sample] = voice(handle, audio::Kind::sample, api);
            if ((!extension(name, "wav") && !extension(name, "mp3")) || block != 0)
            {
                unsupported(api, "requires a WAVE or MP3 file and block zero");
            }
            audio::Clip clip;
            if (!data ||
                !decode(api, {static_cast<const unsigned char*>(data), bytes}, *owner, clip))
            {
                unsupported(api, "invalid or truncated WAVE or MP3 file");
            }
            std::lock_guard lock(owner->mutex);
            sample->clip = std::move(clip);
            sample->frame = 0;
            sample->status = audio::done;
        });
}

extern "C" void AIL_set_sample_loop_count(void* handle, unsigned int count)
{
    sample_call("AIL_set_sample_loop_count",
        handle,
        audio::Kind::sample,
        [&](auto& sample)
        {
            sample.set_loop_count(count);
        });
}

extern "C" void AIL_start_sample(void* handle)
{
    sample_call("AIL_start_sample",
        handle,
        audio::Kind::sample,
        [](auto& sample)
        {
            sample.start();
        });
}

extern "C" void AIL_end_sample(void* handle)
{
    sample_call("AIL_end_sample",
        handle,
        audio::Kind::sample,
        [](auto& sample)
        {
            sample.status = audio::done;
        });
}

extern "C" void AIL_set_sample_volume(void* handle, int volume)
{
    sample_call("AIL_set_sample_volume",
        handle,
        audio::Kind::sample,
        [&](auto& sample)
        {
            sample.volume = std::clamp(volume, 0, 127);
        });
}

extern "C" int AIL_sample_volume(void* handle)
{
    return sample_call("AIL_sample_volume",
        handle,
        audio::Kind::sample,
        [](const auto& sample)
        {
            return sample.volume;
        });
}

extern "C" int AIL_sample_status(void* handle)
{
    return sample_call("AIL_sample_status",
        handle,
        audio::Kind::sample,
        [](const auto& sample)
        {
            return sample.status;
        });
}

extern "C" int AIL_enumerate_3D_providers(unsigned int* cursor, void** handle, const char** name)
{
    return call("AIL_enumerate_3D_providers",
        [&]
        {
            if (!cursor || !handle || !name || !state.started || *cursor != 0)
            {
                return 0;
            }
            *cursor = 1;
            *handle = &state.provider;
            *name = "Miles Fast 2D Positional Audio";
            return 1;
        });
}

extern "C" int AIL_open_3D_provider(void* handle)
{
    return call("AIL_open_3D_provider",
        [&]
        {
            if (handle != &state.provider || !state.started || state.drivers.empty())
            {
                return 1;
            }
            if (!state.provider.driver)
            {
                state.provider.driver = state.drivers.back().get();
            }
            return 0;
        });
}

extern "C" void AIL_close_3D_provider(void* handle)
{
    call("AIL_close_3D_provider",
        [&]
        {
            auto& current = provider(handle, "AIL_close_3D_provider");
            {
                std::lock_guard lock(current.driver->mutex);
                std::erase_if(current.driver->mixer.voices,
                    [](const auto& sample)
                    {
                        return sample->kind == audio::Kind::spatial_sample;
                    });
            }
            current = {};
        });
}

extern "C" void* AIL_open_3D_listener(void* handle)
{
    return call("AIL_open_3D_listener",
        [&]() -> void*
        {
            auto& current = provider(handle, "AIL_open_3D_listener");
            if (!current.listener_open)
            {
                std::lock_guard lock(current.driver->mutex);
                current.driver->mixer.listener = {};
                current.listener_open = true;
            }
            return &current.listener;
        });
}

extern "C" void* AIL_allocate_3D_sample_handle(void* handle)
{
    return call("AIL_allocate_3D_sample_handle",
        [&]() -> void*
        {
            auto& current = provider(handle, "AIL_allocate_3D_sample_handle");
            std::lock_guard lock(current.driver->mutex);
            return current.driver->mixer.allocate(audio::Kind::spatial_sample);
        });
}

extern "C" int AIL_set_3D_sample_file(void* handle, const void* file)
{
    return call("AIL_set_3D_sample_file",
        [&]
        {
            constexpr auto api = "AIL_set_3D_sample_file";
            auto [owner, sample] = voice(handle, audio::Kind::spatial_sample, api);
            if (!file)
            {
                return 0;
            }
            // Miles supplies no length here: its caller owns a complete RIFF
            // allocation. Derive its extent from the header, then use bounded I/O.
            const auto* bytes = static_cast<const unsigned char*>(file);
            if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0)
            {
                unsupported(api, "requires an in-memory RIFF WAVE image with a bounded extent");
            }
            const std::uint64_t size = std::uint64_t(bytes[4]) | (std::uint64_t(bytes[5]) << 8) |
                                       (std::uint64_t(bytes[6]) << 16) |
                                       (std::uint64_t(bytes[7]) << 24);
            if (size < 4 || size + 8 > std::uint64_t(std::numeric_limits<int>::max()))
            {
                return 0;
            }
            audio::Clip clip;
            if (!decode(api, {bytes, static_cast<std::size_t>(size + 8)}, *owner, clip))
            {
                return 0;
            }
            std::lock_guard lock(owner->mutex);
            sample->clip = std::move(clip);
            sample->frame = 0;
            sample->status = audio::done;
            return 1;
        });
}

extern "C" void AIL_set_3D_sample_loop_count(void* handle, unsigned int count)
{
    sample_call("AIL_set_3D_sample_loop_count",
        handle,
        audio::Kind::spatial_sample,
        [&](auto& sample)
        {
            sample.set_loop_count(count);
        });
}

extern "C" void AIL_set_3D_sample_distances(void* handle, float maximum, float minimum)
{
    sample_call("AIL_set_3D_sample_distances",
        handle,
        audio::Kind::spatial_sample,
        [&](auto& sample)
        {
            if (!std::isfinite(maximum) || !std::isfinite(minimum) || minimum <= 0 ||
                maximum < minimum)
            {
                unsupported("AIL_set_3D_sample_distances", "requires finite positive min <= max");
            }
            sample.minimum_distance = minimum;
            sample.maximum_distance = maximum;
        });
}

extern "C" void AIL_set_3D_position(void* handle, float x, float y, float z)
{
    call("AIL_set_3D_position",
        [&]
        {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            {
                unsupported("AIL_set_3D_position", "requires finite coordinates");
            }
            if (handle == &state.provider.listener && state.provider.driver &&
                state.provider.listener_open)
            {
                std::lock_guard lock(state.provider.driver->mutex);
                state.provider.driver->mixer.listener = {x, y, z};
                return;
            }
            auto [owner, sample] =
                voice(handle, audio::Kind::spatial_sample, "AIL_set_3D_position");
            std::lock_guard lock(owner->mutex);
            sample->position = {x, y, z};
        });
}

extern "C" void AIL_start_3D_sample(void* handle)
{
    sample_call("AIL_start_3D_sample",
        handle,
        audio::Kind::spatial_sample,
        [](auto& sample)
        {
            sample.start();
        });
}

extern "C" void AIL_end_3D_sample(void* handle)
{
    sample_call("AIL_end_3D_sample",
        handle,
        audio::Kind::spatial_sample,
        [](auto& sample)
        {
            sample.status = audio::done;
        });
}

extern "C" void AIL_set_3D_sample_volume(void* handle, int volume)
{
    sample_call("AIL_set_3D_sample_volume",
        handle,
        audio::Kind::spatial_sample,
        [&](auto& sample)
        {
            sample.volume = std::clamp(volume, 0, 127);
        });
}

extern "C" int AIL_3D_sample_volume(void* handle)
{
    return sample_call("AIL_3D_sample_volume",
        handle,
        audio::Kind::spatial_sample,
        [](const auto& sample)
        {
            return sample.volume;
        });
}

extern "C" int AIL_3D_sample_status(void* handle)
{
    return sample_call("AIL_3D_sample_status",
        handle,
        audio::Kind::spatial_sample,
        [](const auto& sample)
        {
            return sample.status;
        });
}

extern "C" void* AIL_open_stream(void* handle, const char* filename, int memory)
{
    return call("AIL_open_stream",
        [&]() -> void*
        {
            constexpr auto api = "AIL_open_stream";
            auto& owner = driver(handle, api);
            if (memory < 0)
            {
                unsupported(api, "requires an automatic or positive stream buffer size");
            }
            // memory is a buffering hint. Eager decoding detaches stream playback
            // from the thread-bound VFS; all playback and pause/loop state is live.
            std::vector<unsigned char> bytes;
            audio::Clip clip;
            if (!read_file(filename, bytes) || !decode(api, bytes, owner, clip))
            {
                return nullptr;
            }
            std::lock_guard lock(owner.mutex);
            auto* result = owner.mixer.allocate(audio::Kind::stream);
            result->clip = std::move(clip);
            return result;
        });
}

extern "C" void AIL_close_stream(void* handle)
{
    call("AIL_close_stream",
        [&]
        {
            auto [owner, sample] = voice(handle, audio::Kind::stream, "AIL_close_stream");
            std::lock_guard lock(owner->mutex);
            std::erase_if(owner->mixer.voices,
                [&](const auto& entry)
                {
                    return entry.get() == sample;
                });
        });
}

extern "C" void AIL_start_stream(void* handle)
{
    sample_call("AIL_start_stream",
        handle,
        audio::Kind::stream,
        [](auto& stream)
        {
            stream.start();
        });
}

extern "C" void AIL_pause_stream(void* handle, int pause)
{
    sample_call("AIL_pause_stream",
        handle,
        audio::Kind::stream,
        [&](auto& stream)
        {
            if (pause && stream.status == audio::playing)
            {
                stream.status = audio::stopped;
            }
            else if (!pause && stream.status == audio::stopped)
            {
                stream.status = audio::playing;
            }
        });
}

extern "C" void AIL_set_stream_loop_count(void* handle, unsigned int count)
{
    sample_call("AIL_set_stream_loop_count",
        handle,
        audio::Kind::stream,
        [&](auto& stream)
        {
            stream.set_loop_count(count);
        });
}

extern "C" void AIL_set_stream_volume(void* handle, int volume)
{
    sample_call("AIL_set_stream_volume",
        handle,
        audio::Kind::stream,
        [&](auto& stream)
        {
            stream.volume = std::clamp(volume, 0, 127);
        });
}

extern "C" int AIL_stream_status(void* handle)
{
    return sample_call("AIL_stream_status",
        handle,
        audio::Kind::stream,
        [](const auto& stream)
        {
            return stream.status;
        });
}

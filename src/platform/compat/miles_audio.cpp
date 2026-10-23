#include "miles_audio.h"

// One implementation, with file I/O disabled: compressed input comes from the
// caller's sample buffer or the prepared VFS, never a native filesystem path.
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include <dr_mp3.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace beachlife::compat::audio
{
namespace
{
std::uint32_t little32(const unsigned char* bytes)
{
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

bool tag(const unsigned char* bytes, const char* name)
{
    return std::memcmp(bytes, name, 4) == 0;
}

struct WaveFormat
{
    DecodeResult result = DecodeResult::invalid;
    std::span<const unsigned char> payload;
    bool mp3 = false;
};

WaveFormat wave_format(std::span<const unsigned char> file)
{
    if (file.size() < 12 || !tag(file.data(), "RIFF") || !tag(file.data() + 8, "WAVE"))
    {
        return {};
    }
    const std::uint64_t end = std::uint64_t(little32(file.data() + 4)) + 8;
    if (end < 12 || end > file.size())
    {
        return {};
    }
    bool format = false, data = false;
    WaveFormat result;
    for (std::uint64_t offset = 12; offset < end;)
    {
        if (end - offset < 8)
        {
            return {};
        }
        const auto* chunk = file.data() + offset;
        const auto size = little32(chunk + 4);
        const auto next = offset + 8 + std::uint64_t(size) + (size & 1);
        if (next > end)
        {
            return {};
        }
        if (tag(chunk, "fmt "))
        {
            if (format || size < 16)
            {
                return {};
            }
            const unsigned int encoding = chunk[8] | (static_cast<unsigned int>(chunk[9]) << 8);
            if (encoding != 1 && encoding != 0x11 && encoding != 0x55)
            {
                return {DecodeResult::unsupported, {}, false};
            }
            result.mp3 = encoding == 0x55;
            format = true;
        }
        else if (tag(chunk, "data"))
        {
            if (!data)
            {
                result.payload = file.subspan(static_cast<std::size_t>(offset + 8), size);
            }
            data = true;
        }
        offset = next;
    }
    result.result = format && data ? DecodeResult::success : DecodeResult::invalid;
    return result;
}

struct FreeAudio
{
    void operator()(unsigned char* bytes) const
    {
        SDL_free(bytes);
    }
};

DecodeResult convert(
    const SDL_AudioSpec& source, std::span<const unsigned char> pcm, int frequency, Clip& clip)
{
    if (frequency <= 0 || pcm.empty() || pcm.size() > std::size_t(std::numeric_limits<int>::max()))
    {
        return DecodeResult::invalid;
    }
    const SDL_AudioSpec destination{SDL_AUDIO_F32, 2, frequency};
    unsigned char* converted = nullptr;
    int converted_bytes = 0;
    const bool converted_ok = SDL_ConvertAudioSamples(
        &source, pcm.data(), int(pcm.size()), &destination, &converted, &converted_bytes);
    const std::unique_ptr<unsigned char, FreeAudio> converted_buffer(converted);
    if (!converted_ok || converted_bytes <= 0 || converted_bytes % (2 * sizeof(float)))
    {
        return DecodeResult::invalid;
    }
    Clip result;
    result.samples.resize(std::size_t(converted_bytes) / sizeof(float));
    std::memcpy(result.samples.data(), converted, std::size_t(converted_bytes));
    clip = std::move(result);
    return DecodeResult::success;
}

struct CloseMp3
{
    void operator()(drmp3* decoder) const
    {
        drmp3_uninit(decoder);
    }
};

DecodeResult decode_mp3(std::span<const unsigned char> file, int frequency, Clip& clip)
{
    if (frequency <= 0 || file.empty() ||
        file.size() > std::size_t(std::numeric_limits<int>::max()))
    {
        return DecodeResult::invalid;
    }
    drmp3 decoder{};
    if (!drmp3_init_memory(&decoder, file.data(), file.size(), nullptr))
    {
        return DecodeResult::invalid;
    }
    const std::unique_ptr<drmp3, CloseMp3> close(&decoder);
    if ((decoder.channels != 1 && decoder.channels != 2) || !decoder.sampleRate ||
        decoder.sampleRate > static_cast<unsigned int>(std::numeric_limits<int>::max()))
    {
        return DecodeResult::invalid;
    }
    // dr_mp3 accounts for Xing/LAME encoder delay and padding. Bound the
    // decoded size before allocating or narrowing to SDL's signed byte count.
    const auto frames = drmp3_get_pcm_frame_count(&decoder);
    if (!frames || frames > std::numeric_limits<int>::max() / (decoder.channels * sizeof(float)))
    {
        return DecodeResult::invalid;
    }
    std::vector<float> pcm(static_cast<std::size_t>(frames * decoder.channels));
    if (drmp3_read_pcm_frames_f32(&decoder, frames, pcm.data()) != frames)
    {
        return DecodeResult::invalid;
    }
    const SDL_AudioSpec source{SDL_AUDIO_F32, int(decoder.channels), int(decoder.sampleRate)};
    return convert(source,
        {reinterpret_cast<const unsigned char*>(pcm.data()), pcm.size() * sizeof(float)},
        frequency,
        clip);
}

void gains(const Voice& voice, Position listener, float& left, float& right)
{
    left = right = static_cast<float>(voice.volume) / 127;
    if (voice.kind != Kind::spatial_sample)
    {
        return;
    }
    const double x = double(voice.position.x) - listener.x;
    const double y = double(voice.position.y) - listener.y;
    const double z = double(voice.position.z) - listener.z;
    const auto distance = std::sqrt(x * x + y * y + z * z);
    if (distance >= voice.maximum_distance)
    {
        left = right = 0;
        return;
    }
    const auto attenuation =
        distance <= voice.minimum_distance ? 1.0 : voice.minimum_distance / distance;
    // The listener's default Miles orientation faces +Z with +Y up. The
    // software provider uses inverse-distance attenuation and equal-power pan.
    const auto horizontal = std::hypot(x, z);
    const auto pan = horizontal > 0 ? x / horizontal : 0;
    left *= static_cast<float>(attenuation * std::sqrt((1 - pan) * 0.5));
    right *= static_cast<float>(attenuation * std::sqrt((1 + pan) * 0.5));
}
} // namespace

DecodeResult decode_wave(std::span<const unsigned char> file, int frequency, Clip& clip)
{
    const auto format = wave_format(file);
    if (format.result != DecodeResult::success)
    {
        return format.result;
    }
    if (frequency <= 0 || file.size() > std::size_t(std::numeric_limits<int>::max()))
    {
        return DecodeResult::invalid;
    }
    if (format.mp3)
    {
        return decode_mp3(format.payload, frequency, clip);
    }
    auto* input = SDL_IOFromConstMem(file.data(), file.size());
    if (!input)
    {
        return DecodeResult::invalid;
    }
    SDL_AudioSpec source{};
    unsigned char* decoded = nullptr;
    Uint32 decoded_bytes = 0;
    const bool loaded = SDL_LoadWAV_IO(input, true, &source, &decoded, &decoded_bytes);
    const std::unique_ptr<unsigned char, FreeAudio> source_buffer(decoded);
    if (!loaded || !decoded_bytes || decoded_bytes > Uint32(std::numeric_limits<int>::max()))
    {
        return DecodeResult::invalid;
    }
    return convert(source, {decoded, decoded_bytes}, frequency, clip);
}

DecodeResult decode_file(std::span<const unsigned char> file, int frequency, Clip& clip)
{
    if (file.size() >= 4 && tag(file.data(), "RIFF"))
    {
        return decode_wave(file, frequency, clip);
    }
    return decode_mp3(file, frequency, clip);
}

void Voice::start()
{
    frame = 0;
    remaining = loops;
    status = clip.samples.empty() ? done : playing;
}

void Voice::set_loop_count(unsigned int count)
{
    loops = remaining = count;
}

Voice* Mixer::allocate(Kind kind)
{
    auto voice = std::make_unique<Voice>(kind);
    auto* result = voice.get();
    voices.push_back(std::move(voice));
    return result;
}

void Mixer::render(std::span<float> stereo)
{
    std::fill(stereo.begin(), stereo.end(), 0);
    const auto frames = stereo.size() / 2;
    const float master = static_cast<float>(volume) / 127;
    for (auto& voice : voices)
    {
        if (voice->status != playing)
        {
            continue;
        }
        const auto length = voice->clip.samples.size() / 2;
        if (!length)
        {
            voice->status = done;
            continue;
        }
        float left, right;
        gains(*voice, listener, left, right);
        left *= master;
        right *= master;
        for (std::size_t frame = 0; frame < frames && voice->status == playing; ++frame)
        {
            const auto offset = voice->frame * 2;
            if (voice->kind == Kind::spatial_sample)
            {
                const auto mono =
                    (voice->clip.samples[offset] + voice->clip.samples[offset + 1]) * 0.5f;
                stereo[frame * 2] += mono * left;
                stereo[frame * 2 + 1] += mono * right;
            }
            else
            {
                stereo[frame * 2] += voice->clip.samples[offset] * left;
                stereo[frame * 2 + 1] += voice->clip.samples[offset + 1] * right;
            }
            if (++voice->frame == length)
            {
                if (voice->remaining == 1)
                {
                    voice->status = done;
                }
                else
                {
                    if (voice->remaining > 1)
                    {
                        --voice->remaining;
                    }
                    voice->frame = 0;
                }
            }
        }
    }
    for (auto& sample : stereo)
    {
        sample = std::clamp(sample, -1.0f, 1.0f);
    }
}
} // namespace beachlife::compat::audio

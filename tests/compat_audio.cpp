#include "platform/compat/miles_audio.h"

#include "compat_mp3_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
namespace audio = beachlife::compat::audio;
using Bytes = std::vector<unsigned char>;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.0001f;
}

void word(Bytes& bytes, unsigned int value)
{
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
}

void dword(Bytes& bytes, std::uint32_t value)
{
    word(bytes, value);
    word(bytes, value >> 16);
}

void chunk(Bytes& bytes, const char* name, const Bytes& payload)
{
    bytes.insert(bytes.end(), name, name + 4);
    dword(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if (payload.size() & 1)
    {
        bytes.push_back(0);
    }
}

Bytes wave(unsigned int encoding, const Bytes& data)
{
    const bool mp3 = encoding == 0x55;
    Bytes format;
    word(format, encoding);
    word(format, 1);
    dword(format, mp3 ? 44100 : 8000);
    dword(format, mp3 ? 4000 : encoding == 1 ? 16000 : 7111);
    word(format, mp3 ? 1 : encoding == 1 ? 2 : 8);
    word(format, mp3 ? 0 : encoding == 1 ? 16 : 4);
    if (encoding == 0x11)
    {
        word(format, 2);
        word(format, 9);
    }
    else if (mp3)
    {
        // MPEGLAYER3WAVEFORMAT: extension size, MPEG ID, padding flags,
        // nominal block size, frames per block, and codec delay.
        word(format, 12);
        word(format, 1);
        dword(format, 0);
        word(format, 104);
        word(format, 1);
        word(format, 0);
    }
    Bytes body{'W', 'A', 'V', 'E'};
    chunk(body, "JUNK", {7});
    chunk(body, "fmt ", format);
    if (encoding == 0x11)
    {
        chunk(body, "fact", {9, 0, 0, 0});
    }
    chunk(body, "data", data);
    Bytes result{'R', 'I', 'F', 'F'};
    dword(result, static_cast<std::uint32_t>(body.size()));
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

void decoding()
{
    const auto pcm = wave(1, {0, 0, 0, 32, 0, 192, 255, 127});
    audio::Clip clip;
    expect(audio::decode_wave(pcm, 8000, clip) == audio::DecodeResult::success,
        "Generated PCM WAVE did not decode.");
    expect(clip.samples.size() == 8, "PCM mono-to-stereo length differs.");
    constexpr std::array<float, 4> expected{0, 0.25f, -0.5f, 32767.0f / 32768};
    for (std::size_t frame = 0; frame < expected.size(); ++frame)
    {
        expect(near(clip.samples[frame * 2], expected[frame]) &&
                   near(clip.samples[frame * 2 + 1], expected[frame]),
            "PCM conversion changed a sample or stereo channel.");
    }

    const auto ima = wave(0x11, {232, 3, 0, 0, 0, 0, 0, 0});
    expect(audio::decode_wave(ima, 8000, clip) == audio::DecodeResult::success,
        "Generated IMA ADPCM WAVE did not decode.");
    expect(clip.samples.size() == 18, "IMA block frame count differs.");
    for (const auto sample : clip.samples)
    {
        expect(near(sample, 1000.0f / 32768), "IMA predictor or stereo conversion differs.");
    }
    expect(audio::decode_wave(pcm, 16000, clip) == audio::DecodeResult::success &&
               clip.samples.size() == 16,
        "WAVE resampling length differs.");

    for (std::size_t length = 0; length < pcm.size(); ++length)
    {
        expect(audio::decode_wave(std::span(pcm).first(length), 8000, clip) ==
                   audio::DecodeResult::invalid,
            "Truncated RIFF was accepted.");
    }
    auto corrupt = pcm;
    std::fill(corrupt.begin() + 16, corrupt.begin() + 20, 0xff);
    expect(audio::decode_wave(corrupt, 8000, clip) == audio::DecodeResult::invalid,
        "Overflowing chunk length was accepted.");
    expect(audio::decode_wave(wave(0x55, {1, 2}), 8000, clip) == audio::DecodeResult::invalid,
        "Invalid WAVE-wrapped MP3 payload was accepted.");
    expect(audio::decode_wave(wave(7, {1, 2}), 8000, clip) == audio::DecodeResult::unsupported,
        "Unsupported WAVE codec was accepted.");
}

void mp3_decoding()
{
    const auto& encoded = compat_audio_fixture::mp3;
    audio::Clip clip;
    expect(audio::decode_file(encoded, 44100, clip) == audio::DecodeResult::success,
        "Generated MP3 did not decode.");
    expect(clip.samples.size() == 4410 * 2,
        "MP3 frame count did not account for encoder delay and padding.");
    double power = 0;
    unsigned int crossings = 0;
    for (std::size_t frame = 0; frame < 4410; ++frame)
    {
        const auto sample = clip.samples[frame * 2];
        expect(std::isfinite(sample) && near(sample, clip.samples[frame * 2 + 1]),
            "MP3 mono-to-stereo conversion produced invalid or different channels.");
        power += sample * sample;
        if (frame && clip.samples[(frame - 1) * 2] <= 0 && sample > 0)
        {
            ++crossings;
        }
    }
    const auto rms = std::sqrt(power / 4410);
    expect(rms > 0.06 && rms < 0.11 && crossings >= 42 && crossings <= 46,
        "Decoded MP3 lost the generated sine wave's amplitude or frequency.");

    audio::Clip wrapped;
    expect(audio::decode_wave(wave(0x55, Bytes(encoded.begin(), encoded.end())), 44100, wrapped) ==
                   audio::DecodeResult::success &&
               wrapped.samples == clip.samples,
        "WAVE-wrapped MP3 did not decode the same payload.");
    expect(audio::decode_file(encoded, 22050, clip) == audio::DecodeResult::success &&
               clip.samples.size() == 2205 * 2,
        "MP3 resampling to the Miles driver rate differs.");
    expect(audio::decode_file(std::span(encoded).first(16), 22050, clip) ==
               audio::DecodeResult::invalid,
        "Truncated MP3 header was accepted.");
}

void mixing()
{
    audio::Mixer mixer;
    auto* voice = mixer.allocate(audio::Kind::sample);
    voice->clip.samples = {0.25f, -0.5f, 0.5f, 0.75f};
    voice->set_loop_count(2);
    voice->start();
    std::array<float, 10> output;
    mixer.render(output);
    expect(
        output ==
                std::array<float, 10>{0.25f, -0.5f, 0.5f, 0.75f, 0.25f, -0.5f, 0.5f, 0.75f, 0, 0} &&
            voice->status == audio::done,
        "Finite playback did not repeat exactly twice and finish.");

    voice->set_loop_count(0);
    voice->start();
    mixer.render(output);
    expect(voice->status == audio::playing && near(output[8], 0.25f),
        "Zero loop count did not keep playing.");
    voice->status = audio::stopped;
    const auto paused_frame = voice->frame;
    mixer.render(output);
    expect(std::all_of(output.begin(),
               output.end(),
               [](float sample)
               {
                   return sample == 0;
               }) &&
               voice->frame == paused_frame,
        "Paused voice advanced or produced audio.");

    voice->start();
    voice->volume = 64;
    mixer.volume = 64;
    mixer.render(output);
    expect(near(output[0], 0.25f * 64 * 64 / (127 * 127)),
        "Sample and master volume were not multiplied.");
    voice->set_loop_count(1);
    voice->start();
    mixer.volume = 0;
    mixer.render(output);
    expect(voice->status == audio::done && output[0] == 0,
        "Master mute did not preserve playback progression.");

    mixer = {};
    voice = mixer.allocate(audio::Kind::spatial_sample);
    voice->clip.samples = {0.5f, 0.5f};
    voice->position = {2, 0, 0};
    voice->start();
    mixer.render(output);
    expect(near(output[0], 0) && near(output[1], 0.25f),
        "3D right pan or inverse-distance attenuation differs.");
    mixer.listener = {4, 0, 0};
    voice->start();
    mixer.render(output);
    expect(near(output[0], 0.25f) && near(output[1], 0),
        "Listener movement did not update 3D panning.");
    voice->maximum_distance = 2;
    voice->start();
    mixer.render(output);
    expect(output[0] == 0 && output[1] == 0 && voice->status == audio::done,
        "Inaudible 3D voice failed to advance to completion.");
}
} // namespace

int main()
{
    try
    {
        decoding();
        mp3_decoding();
        mixing();
        std::cout << "Generated WAV/MP3 decoding and Miles mixing passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "mss.h"

#include "runtime/retail_files.h"

#include "compat_mp3_fixture.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;
namespace data = beachlife::data;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void put(Bytes& bytes, std::size_t offset, unsigned int value, unsigned int count)
{
    for (unsigned int index = 0; index < count; ++index)
    {
        bytes[offset + index] = static_cast<unsigned char>(value >> (index * 8));
    }
}

Bytes wave()
{
    // Generated mono PCM: 100 ms at the game's 22050 Hz digital-driver rate.
    constexpr unsigned int data_bytes = 2205 * 2;
    Bytes bytes(44 + data_bytes);
    std::memcpy(bytes.data(), "RIFF", 4);
    put(bytes, 4, 36 + data_bytes, 4);
    std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
    put(bytes, 16, 16, 4);
    put(bytes, 20, 1, 2);
    put(bytes, 22, 1, 2);
    put(bytes, 24, 22050, 4);
    put(bytes, 28, 44100, 4);
    put(bytes, 32, 2, 2);
    put(bytes, 34, 16, 2);
    std::memcpy(bytes.data() + 36, "data", 4);
    put(bytes, 40, data_bytes, 4);
    for (std::size_t offset = 44; offset < bytes.size(); offset += 2)
    {
        put(bytes, offset, 8192, 2);
    }
    return bytes;
}

class MemoryFile final : public data::SourceFile
{
  public:
    explicit MemoryFile(std::shared_ptr<const Bytes> bytes)
        : bytes_(std::move(bytes))
    {
    }
    std::uint64_t size() const override
    {
        return bytes_->size();
    }
    std::uint64_t tell() const override
    {
        return cursor_;
    }
    void seek(std::uint64_t position) override
    {
        expect(position <= size(), "VFS audio seek exceeded the fixture.");
        cursor_ = static_cast<std::size_t>(position);
    }
    std::size_t read(std::span<unsigned char> destination) override
    {
        const auto count = std::min(destination.size(), bytes_->size() - cursor_);
        std::copy_n(bytes_->begin() + cursor_, count, destination.begin());
        cursor_ += count;
        return count;
    }

  private:
    std::shared_ptr<const Bytes> bytes_;
    std::size_t cursor_ = 0;
};

class MemorySource final : public data::FileSource
{
  public:
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override
    {
        if (path == "Sound\\Fixture.MP3")
        {
            const auto& mp3 = compat_audio_fixture::mp3;
            return std::make_unique<MemoryFile>(
                std::make_shared<const Bytes>(mp3.begin(), mp3.end()));
        }
        if (path != "Sound\\Fixture.WAV")
        {
            return nullptr;
        }
        return std::make_unique<MemoryFile>(std::make_shared<const Bytes>(wave()));
    }
};

template <class F> void finishes(F status)
{
    const auto deadline = SDL_GetTicks() + 2000;
    while (status() == SMP_PLAYING && SDL_GetTicks() < deadline)
    {
        SDL_Delay(1);
    }
    expect(status() == SMP_DONE, "SDL audio callback did not finish the voice.");
}

void mp3_playback(void* driver, void* sample)
{
    const auto& mp3 = compat_audio_fixture::mp3;
    Bytes bytes(mp3.begin(), mp3.end());
    AIL_init_sample(sample);
    AIL_set_named_sample_file(
        sample, ".Mp3", bytes.data(), static_cast<unsigned int>(bytes.size()), 0);
    Bytes{}.swap(bytes);
    AIL_set_sample_volume(sample, 64);
    expect(AIL_sample_volume(sample) == 64, "MP3 sample volume differs.");
    AIL_set_sample_loop_count(sample, 2);
    AIL_start_sample(sample);
    finishes(
        [&]
        {
            return AIL_sample_status(sample);
        });
    AIL_set_sample_loop_count(sample, 0);
    AIL_start_sample(sample);
    SDL_Delay(120);
    expect(AIL_sample_status(sample) == SMP_PLAYING, "MP3 sample loop finished.");
    AIL_end_sample(sample);

    void* stream;
    {
        beachlife::runtime::retail_files::Session session(std::make_shared<MemorySource>());
        stream = AIL_open_stream(driver, "Sound\\Fixture.MP3", 0);
        expect(stream != nullptr, "Generated MP3 stream did not open through the VFS.");
    }
    AIL_set_stream_volume(stream, 64);
    AIL_set_stream_loop_count(stream, 0);
    AIL_start_stream(stream);
    AIL_pause_stream(stream, 1);
    SDL_Delay(120);
    expect(AIL_stream_status(stream) == SMP_STOPPED, "Paused MP3 stream advanced or finished.");
    AIL_pause_stream(stream, 0);
    expect(AIL_stream_status(stream) == SMP_PLAYING, "MP3 stream did not resume.");
    AIL_set_stream_loop_count(stream, 1);
    AIL_start_stream(stream);
    finishes(
        [&]
        {
            return AIL_stream_status(stream);
        });
    AIL_close_stream(stream);
}

void lifecycle(void* driver)
{
    std::array<void*, 16> samples;
    for (auto& sample : samples)
    {
        sample = AIL_allocate_sample_handle(driver);
        expect(sample != nullptr, "The game's 16 sample allocations failed.");
        AIL_init_sample(sample);
    }
    auto* sample = samples.front();
    expect(AIL_sample_status(sample) == SMP_DONE, "Initialized sample is not done.");
    auto bytes = wave();
    AIL_set_named_sample_file(
        sample, ".WaV", bytes.data(), static_cast<unsigned int>(bytes.size()), 0);
    Bytes{}.swap(bytes);
    AIL_set_sample_volume(sample, 500);
    expect(AIL_sample_volume(sample) == 127, "Sample volume was not clamped.");
    AIL_set_digital_master_volume(driver, 0);
    AIL_start_sample(sample);
    finishes(
        [&]
        {
            return AIL_sample_status(sample);
        });
    AIL_set_digital_master_volume(driver, 127);
    AIL_set_sample_loop_count(sample, 0);
    AIL_start_sample(sample);
    SDL_Delay(120);
    expect(AIL_sample_status(sample) == SMP_PLAYING, "Infinite sample loop finished.");
    AIL_end_sample(sample);
    expect(AIL_sample_status(sample) == SMP_DONE, "Explicit sample end did not finish.");

    mp3_playback(driver, sample);

    unsigned int cursor = 0;
    void* provider = nullptr;
    const char* name = nullptr;
    expect(AIL_enumerate_3D_providers(&cursor, &provider, &name) == 1 && name && *name,
        "Software 3D provider was not enumerated.");
    expect(AIL_enumerate_3D_providers(&cursor, &provider, &name) == 0,
        "3D provider enumeration did not terminate.");
    expect(AIL_open_3D_provider(provider) == 0, "Software 3D provider did not open.");
    auto* listener = AIL_open_3D_listener(provider);
    expect(listener != nullptr, "3D listener did not open.");
    AIL_set_3D_position(listener, 0, 0, 0);
    std::array<void*, 8> spatial;
    for (auto& entry : spatial)
    {
        entry = AIL_allocate_3D_sample_handle(provider);
        expect(entry != nullptr, "The game's eight 3D allocations failed.");
    }
    bytes = wave();
    expect(AIL_set_3D_sample_file(spatial.front(), bytes.data()) == 1,
        "3D in-memory WAVE did not load.");
    Bytes{}.swap(bytes);
    AIL_set_3D_sample_volume(spatial.front(), 82);
    expect(AIL_3D_sample_volume(spatial.front()) == 82, "3D sample volume differs.");
    AIL_set_3D_sample_distances(spatial.front(), 750, 250);
    AIL_set_3D_position(spatial.front(), 5000, 0, 0);
    AIL_set_3D_sample_loop_count(spatial.front(), 0);
    AIL_start_3D_sample(spatial.front());
    SDL_Delay(120);
    expect(AIL_3D_sample_status(spatial.front()) == SMP_PLAYING,
        "Inaudible 3D sample did not keep looping.");
    AIL_end_3D_sample(spatial.front());
    expect(AIL_3D_sample_status(spatial.front()) == SMP_DONE, "3D sample end did not finish.");

    expect(AIL_open_stream(driver, "Sound\\Fixture.WAV", 0) == nullptr,
        "Stream opened without a prepared VFS session.");
    void* stream;
    {
        beachlife::runtime::retail_files::Session session(std::make_shared<MemorySource>());
        expect(AIL_open_stream(driver, "missing.wav", 0) == nullptr,
            "Missing VFS stream did not fail.");
        stream = AIL_open_stream(driver, "Sound\\Fixture.WAV", 0);
        expect(stream != nullptr, "Generated WAV stream did not open through the VFS.");
    }
    AIL_set_stream_volume(stream, 63);
    AIL_set_stream_loop_count(stream, 0);
    AIL_start_stream(stream);
    AIL_pause_stream(stream, 1);
    SDL_Delay(120);
    expect(AIL_stream_status(stream) == SMP_STOPPED, "Paused stream advanced or finished.");
    AIL_pause_stream(stream, 0);
    expect(AIL_stream_status(stream) == SMP_PLAYING, "Stream did not resume.");
    AIL_set_stream_loop_count(stream, 1);
    AIL_start_stream(stream);
    finishes(
        [&]
        {
            return AIL_stream_status(stream);
        });
    AIL_set_stream_loop_count(stream, 0);
    AIL_start_stream(stream);
    AIL_close_stream(stream);

    AIL_start_3D_sample(spatial.front());
    AIL_close_3D_provider(provider);
    AIL_start_sample(sample);
    AIL_close_digital_driver(driver);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        expect(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"), "Cannot select SDL's dummy driver.");
        expect(SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "128"),
            "Cannot set audio block size.");
        expect(SDL_InitSubSystem(SDL_INIT_AUDIO), "Cannot initialize the host audio subsystem.");
        AIL_set_redist_directory("redist\\");
        expect(AIL_startup() != 0 && AIL_startup() != 0, "Miles startup failed.");
        auto* driver = AIL_open_digital_driver(22050, 16, 2, 0);
        expect(driver != nullptr, "SDL digital driver did not open.");
        if (argc == 2 && std::string_view(argv[1]) == "--invalid-sample")
        {
            const std::array<unsigned char, 16> invalid{};
            AIL_set_named_sample_file(AIL_allocate_sample_handle(driver),
                ".mp3",
                invalid.data(),
                static_cast<unsigned int>(invalid.size()),
                0);
            throw std::runtime_error("Invalid sample did not stop loudly.");
        }
        lifecycle(driver);
        AIL_shutdown();
        AIL_shutdown();
        expect(SDL_WasInit(SDL_INIT_AUDIO) != 0,
            "Miles shutdown released the host's audio reference.");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        expect(
            SDL_WasInit(SDL_INIT_AUDIO) == 0, "Miles startup leaked an audio subsystem reference.");
        std::cout << "Miles WAV/MP3 callbacks, VFS streams and handle lifetimes passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        AIL_shutdown();
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

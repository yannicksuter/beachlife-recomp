#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

// Private mixer and decoder used by the Miles API adapter. All decoded clips
// are interleaved stereo floats at the digital driver's sample rate.
namespace beachlife::compat::audio
{
enum class DecodeResult
{
    success,
    invalid,
    unsupported
};

struct Clip
{
    std::vector<float> samples;
};

DecodeResult decode_wave(std::span<const unsigned char> file, int frequency, Clip& clip);
DecodeResult decode_file(std::span<const unsigned char> file, int frequency, Clip& clip);

struct Position
{
    float x = 0, y = 0, z = 0;
};

enum class Kind
{
    sample,
    spatial_sample,
    stream
};

inline constexpr int done = 2;
inline constexpr int playing = 4;
inline constexpr int stopped = 8;

struct Voice
{
    explicit Voice(Kind kind)
        : kind(kind)
    {
    }

    Kind kind;
    Clip clip;
    std::size_t frame = 0;
    unsigned int loops = 1;
    unsigned int remaining = 1;
    int volume = 127;
    int status = done;
    Position position;
    float minimum_distance = 1;
    float maximum_distance = 200;

    void start();
    void set_loop_count(unsigned int count);
};

struct Mixer
{
    std::vector<std::unique_ptr<Voice>> voices;
    int volume = 127;
    Position listener;

    Voice* allocate(Kind kind);
    void render(std::span<float> stereo);
};
} // namespace beachlife::compat::audio

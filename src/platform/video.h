#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ImDrawData;

// The game's graphics boundary. The game (later its DirectDraw layer) composes
// whole frames at its own resolution; the video layer owns the window and the GPU
// and only ever shows finished frames, scaled into the window, with an optional
// ImGui overlay. One implementation uses SDL's GPU API, which runs on Direct3D 12
// and Vulkan on Windows, Metal on macOS and Vulkan on Linux; another provider
// could replace it behind this interface.
namespace beachlife::platform
{

// A frame in the game's resolution: 32-bit RGBA (red in the lowest byte), row by row.
struct Frame
{
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels;
};

enum class GraphicsApi
{
    Automatic,
    Direct3D12,
    Vulkan,
    Metal
};

struct VideoSettings
{
    std::string title = "Beach Life";
    bool fullscreen = false;        // borderless, at the display's own resolution
    int width = 1280, height = 960; // the window's size when not fullscreen
    int display = 0;                // 0 is the primary display, otherwise its 1-based position
    bool vsync = true;
    bool stretch = false; // fill the window instead of keeping the frame's proportions
    GraphicsApi api = GraphicsApi::Automatic;
    bool hidden = false; // checks and screenshots
};

// What the video layer actually got.
struct VideoInfo
{
    std::string api;          // SDL's driver name: "vulkan", "direct3d12" or "metal"
    std::string present_mode; // "vsync", "mailbox" or "immediate"
};

class Video
{
  public:
    virtual ~Video() = default;
    virtual SDL_Window* window() const = 0;
    virtual const VideoInfo& info() const = 0;
    // Before ImGui::NewFrame (the caller owns the ImGui context and its input).
    virtual void new_overlay_frame() = 0;
    // Shows `frame` with `overlay` (may be null) on top. Nothing is shown while the
    // window cannot present (minimized).
    virtual void present(const Frame& frame, ImDrawData* overlay) = 0;
    // Saves the next presented picture, overlay included, as a PNG file.
    virtual void save_next(const std::filesystem::path& file) = 0;
};

// Opens the window and the GPU device on the requested API. An API that is not
// available here is an error naming it; there is no silent fallback to another one.
std::unique_ptr<Video> create_video(const VideoSettings& settings);

// Where a frame lands in a window of the given pixel size: all of it when
// stretching, otherwise the largest centred rectangle with the frame's proportions.
SDL_Rect frame_rect(
    int frame_width, int frame_height, int window_width, int window_height, bool stretch);

} // namespace beachlife::platform

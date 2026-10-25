#include "platform/path.h"
#include "platform/video.h"

#include "imgui.h"

#include <stdexcept>
#include <utility>

namespace beachlife::platform
{
namespace
{
// Tests and automation: the window lives on SDL's offscreen driver, so nothing
// appears on screen and nothing needs input focus. No GPU is used; frames are
// only kept for screenshots, which are saved at the game's own resolution.
class HeadlessVideo final : public Video
{
  public:
    explicit HeadlessVideo(const VideoSettings& settings)
        : window_(SDL_CreateWindow(settings.title.c_str(), settings.width, settings.height, 0))
    {
        if (!window_)
        {
            throw std::runtime_error(
                std::string("Cannot open the headless window: ") + SDL_GetError());
        }
        info_.api = "none";
        info_.present_mode = "headless";
    }
    ~HeadlessVideo() override
    {
        SDL_DestroyWindow(window_);
    }
    HeadlessVideo(const HeadlessVideo&) = delete;
    HeadlessVideo& operator=(const HeadlessVideo&) = delete;

    SDL_Window* window() const override
    {
        return window_;
    }
    const VideoInfo& info() const override
    {
        return info_;
    }
    void new_overlay_frame() override
    {
        // No renderer uploads the font atlas; ImGui only needs it built.
        auto* fonts = ImGui::GetIO().Fonts;
        if (!fonts->IsBuilt())
        {
            fonts->Build();
        }
    }
    void present(const Frame& frame, ImDrawData*) override
    {
        if (frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height)
        {
            throw std::runtime_error("Malformed frame");
        }
        set_frame_size(frame.width, frame.height);
        if (screenshot_.empty())
        {
            return;
        }
        const auto file = std::exchange(screenshot_, {});
        // Red in the lowest byte; the game's alpha is not meaningful.
        SDL_Surface* surface = SDL_CreateSurfaceFrom(frame.width,
            frame.height,
            SDL_PIXELFORMAT_XBGR8888,
            const_cast<std::uint32_t*>(frame.pixels.data()),
            frame.width * 4);
        const bool saved = surface && SDL_SavePNG(surface, utf8(file).c_str());
        SDL_DestroySurface(surface);
        if (!saved)
        {
            throw std::runtime_error(std::string("Cannot save the screenshot: ") + SDL_GetError());
        }
    }
    void save_next(const std::filesystem::path& file) override
    {
        screenshot_ = file;
    }

  private:
    SDL_Window* window_;
    VideoInfo info_;
    std::filesystem::path screenshot_;
};
} // namespace

std::unique_ptr<Video> create_headless_video(const VideoSettings& settings)
{
    return std::make_unique<HeadlessVideo>(settings);
}

} // namespace beachlife::platform

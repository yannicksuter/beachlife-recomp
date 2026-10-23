#include "amstream.h"
#include "compat_movie_fixture.h"
#include "ddstream.h"
#include "platform/video.h"
#include "runtime/retail_files.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
namespace data = beachlife::data;
namespace platform = beachlife::platform;
namespace files = beachlife::runtime::retail_files;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "movie test: %s (%s)\n", message, SDL_GetError());
        std::exit(1);
    }
}
struct Counts
{
    int opened = 0, closed = 0, reads = 0, seeks = 0;
};
class MemoryFile final : public data::SourceFile
{
  public:
    MemoryFile(std::shared_ptr<Counts> counts, bool broken)
        : counts_(std::move(counts))
        , broken_(broken)
    {
        ++counts_->opened;
    }
    ~MemoryFile() override
    {
        ++counts_->closed;
    }
    std::uint64_t size() const override
    {
        return broken_ ? 8 : compat_movie_fixture::mpeg.size();
    }
    std::uint64_t tell() const override
    {
        return cursor_;
    }
    void seek(std::uint64_t position) override
    {
        require(position <= size(), "decoder sought outside the VFS file");
        cursor_ = position;
        ++counts_->seeks;
    }
    std::size_t read(std::span<unsigned char> output) override
    {
        const auto count = std::min<size_t>({37, output.size(), size() - cursor_});
        std::copy_n(compat_movie_fixture::mpeg.begin() + cursor_, count, output.begin());
        cursor_ += count;
        ++counts_->reads;
        return count;
    }

  private:
    std::shared_ptr<Counts> counts_;
    bool broken_;
    size_t cursor_ = 0;
};
class MemorySource final : public data::FileSource
{
  public:
    std::shared_ptr<Counts> counts = std::make_shared<Counts>();
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override
    {
        // The VFS, like the real mounted source, owns path normalization.
        std::string normalized(path);
        for (char& c : normalized)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c += 'a' - 'A';
            }
            if (c == '\\')
            {
                c = '/';
            }
        }
        if (normalized != "data/movie.mpg" && normalized != "broken.mpg")
        {
            return nullptr;
        }
        return std::make_unique<MemoryFile>(counts, normalized == "broken.mpg");
    }
};
class TestVideo final : public platform::Video
{
  public:
    TestVideo()
    {
        window_ = SDL_CreateWindow("generated movie test", 64, 48, SDL_WINDOW_HIDDEN);
        require(window_, "create test window");
    }
    ~TestVideo() override
    {
        SDL_DestroyWindow(window_);
    }
    SDL_Window* window() const override
    {
        return window_;
    }
    const platform::VideoInfo& info() const override
    {
        return info_;
    }
    void new_overlay_frame() override {}
    void save_next(const std::filesystem::path&) override {}
    void present(const platform::Frame& frame, ImDrawData*) override
    {
        last = frame;
        ++presented;
        SDL_Delay(presentation_delay);
    }
    platform::Frame last;
    unsigned presented = 0;
    Uint32 presentation_delay = 0;

  private:
    SDL_Window* window_ = nullptr;
    platform::VideoInfo info_{};
};

void skip_path()
{
    setenv("BEACHLIFE_BOOT_SKIP_MOVIE", "1", 1);
    IDirectDraw* draw = reinterpret_cast<IDirectDraw*>(1);
    require(DirectDrawCreate(nullptr, &draw, nullptr) == DDERR_UNSUPPORTED && !draw,
        "skip must fail DirectDraw creation and clear its output");
    void* object = reinterpret_cast<void*>(1);
    require(DirectDrawCreateEx(nullptr, &object, IID_IDirectDraw4, nullptr) == DDERR_UNSUPPORTED &&
                !object,
        "extended skip must fail and clear its output");
    IAMMultiMediaStream* stream = nullptr;
    require(CoCreateInstance(CLSID_AMMultiMediaStream,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IAMMultiMediaStream,
                reinterpret_cast<void**>(&stream)) == S_OK &&
                stream,
        "retail follow-on stream must be a valid object");
    require(FAILED(stream->Initialize(STREAMTYPE_READ, AMMSF_NOGRAPHTHREAD, nullptr)),
        "skip initialization");
    require(FAILED(stream->AddMediaStream(draw, &MSPID_PrimaryVideo, 0, nullptr)),
        "skip video creation");
    require(FAILED(stream->AddMediaStream(
                nullptr, &MSPID_PrimaryAudio, AMMSF_ADDDEFAULTRENDERER, nullptr)),
        "skip audio creation");
    const WCHAR path[] = {'x', 0};
    require(FAILED(stream->OpenFile(path, 0)), "retail OpenFile must clear ready on skip");
    stream->Release();
    // Only the exact value 1 disables playback.
    setenv("BEACHLIFE_BOOT_SKIP_MOVIE", "true", 1);
    require(
        DirectDrawCreate(nullptr, &draw, nullptr) == DD_OK && draw, "non-1 switch must not skip");
    draw->Release();
    unsetenv("BEACHLIFE_BOOT_SKIP_MOVIE");
}

DDSURFACEDESC offscreen(int width, int height)
{
    DDSURFACEDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = width;
    desc.dwHeight = height;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    return desc;
}

struct Surfaces
{
    TestVideo video;
    IDirectDraw* draw = nullptr;
    IDirectDraw4* draw4 = nullptr;
    IDirectDrawSurface4* primary = nullptr;
    IDirectDrawSurface4* back4 = nullptr;
    IDirectDrawSurface* back = nullptr;
    Surfaces()
    {
        require(DirectDrawCreate(nullptr, &draw, nullptr) == DD_OK, "create DirectDraw");
        require(draw->QueryInterface(IID_IDirectDraw4, reinterpret_cast<void**>(&draw4)) == S_OK,
            "query DirectDraw4");
        require(draw4->SetCooperativeLevel(
                    reinterpret_cast<HWND>(&video), DDSCL_NORMAL | DDSCL_FPUSETUP) == DD_OK,
            "set cooperative window");
        DDSURFACEDESC2 desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        desc.dwBackBufferCount = 1;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        require(draw4->CreateSurface(&desc, &primary, nullptr) == DD_OK,
            "create primary with attached back buffer");
        DDSCAPS2 caps{DDSCAPS_BACKBUFFER, 0, 0, 0};
        require(primary->GetAttachedSurface(&caps, &back4) == DD_OK, "get attached back buffer");
        require(
            back4->QueryInterface(IID_IDirectDrawSurface, reinterpret_cast<void**>(&back)) == S_OK,
            "query old back interface");
        IDirectDrawSurface4* again = nullptr;
        require(back->QueryInterface(IID_IDirectDrawSurface4, reinterpret_cast<void**>(&again)) ==
                        S_OK &&
                    again == back4,
            "surface QI round trip must retain identity");
        again->Release();
    }
    ~Surfaces()
    {
        back->Release();
        back4->Release();
        primary->Release();
        draw4->Release();
        draw->Release();
    }
};

void surface_operations(Surfaces& surfaces)
{
    auto desc = offscreen(3, 2);
    IDirectDrawSurface* input = nullptr;
    require(
        surfaces.draw->CreateSurface(&desc, &input, nullptr) == DD_OK, "create odd-width surface");
    desc = {};
    desc.dwSize = sizeof(desc);
    require(input->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr) == DD_OK && desc.lPitch == 8,
        "odd-width pitch must align rows without shortening them");
    auto* data = static_cast<BYTE*>(desc.lpSurface);
    const std::array<unsigned, 6> colours{0xf800, 0x07e0, 0x001f, 0xffff, 0x0000, 0xffe0};
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            data[y * desc.lPitch + x * 2] = colours[y * 3 + x];
            data[y * desc.lPitch + x * 2 + 1] = colours[y * 3 + x] >> 8;
        }
    }
    RECT target{0, 0, 6, 4};
    require(surfaces.back->Blt(&target, input, nullptr, DDBLT_WAIT, nullptr) == DDERR_SURFACEBUSY,
        "blitting locked source must fail");
    require(input->Unlock(desc.lpSurface) == DD_OK, "unlock input");
    require(input->Unlock(nullptr) == DDERR_NOTLOCKED, "double unlock must fail");
    require(surfaces.back->Blt(&target, input, nullptr, DDBLT_WAIT, nullptr) == DD_OK,
        "scale source into back buffer");
    require(surfaces.primary->Flip(nullptr, 0) == DD_OK, "present through host Video");
    const auto& frame = surfaces.video.last;
    require(frame.width == 64 && frame.height == 48 && frame.pixels[0] == 0xff0000ff &&
                frame.pixels[2] == 0xff00ff00 && frame.pixels[4] == 0xffff0000 &&
                frame.pixels[2 * 64] == 0xffffffff,
        "RGB565 conversion, channels or scaled coordinates differ");
    DDSURFACEDESC2 front{};
    front.dwSize = sizeof(front);
    require(surfaces.primary->Lock(nullptr, &front, DDLOCK_READONLY, nullptr) == DD_OK,
        "lock flipped front");
    auto* word = static_cast<BYTE*>(front.lpSurface);
    require(word[0] == 0 && word[1] == 0xf8, "Flip must exchange front/back storage");
    require(
        surfaces.primary->Flip(nullptr, 0) == DDERR_SURFACEBUSY, "locked primary must not flip");
    require(surfaces.primary->Unlock(nullptr) == DD_OK, "unlock front");
    RECT bad{-1, 0, 2, 2};
    desc.dwSize = sizeof(desc);
    require(input->Lock(&bad, &desc, 0, nullptr) == DDERR_INVALIDRECT,
        "negative lock rectangle must fail");
    input->Release();
}

void display_mode()
{
    for (const bool fullscreen : {false, true})
    {
        TestVideo video;
        require(
            SDL_SetWindowFullscreen(video.window(), fullscreen) && SDL_SyncWindow(video.window()),
            "establish the player's presentation preference");
        IDirectDraw4* draw = nullptr;
        require(DirectDrawCreateEx(
                    nullptr, reinterpret_cast<void**>(&draw), IID_IDirectDraw4, nullptr) == DD_OK,
            "create extended DirectDraw");
        const auto handle = reinterpret_cast<HWND>(&video);
        require(draw->SetCooperativeLevel(
                    handle, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_FPUSETUP) == DD_OK,
            "enter exclusive movie mode");
        require(
            FAILED(draw->SetDisplayMode(0, 600, 16, 0, 0)), "invalid display dimensions must fail");
        require(
            draw->SetDisplayMode(800, 600, 16, 0, 0) == DD_OK, "apply logical movie dimensions");
        require(bool(SDL_GetWindowFlags(video.window()) & SDL_WINDOW_FULLSCREEN) == fullscreen,
            "movie must retain the player's window preference");
        DDSURFACEDESC2 desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        IDirectDrawSurface4* surface = nullptr;
        require(draw->CreateSurface(&desc, &surface, nullptr) == DD_OK, "create logical primary");
        require(
            surface->GetSurfaceDesc(&desc) == DD_OK && desc.dwWidth == 800 && desc.dwHeight == 600,
            "primary must reflect the emulated game resolution");
        surface->Release();
        require(draw->RestoreDisplayMode() == DD_OK &&
                    draw->SetCooperativeLevel(handle, DDSCL_NORMAL) == DD_OK,
            "restore previous mode and cooperative level");
        require(bool(SDL_GetWindowFlags(video.window()) & SDL_WINDOW_FULLSCREEN) == fullscreen,
            "restore the player's presentation preference");
        draw->Release();
        require(SDL_SetWindowFullscreen(video.window(), false) && SDL_SyncWindow(video.window()),
            "restore fixture window");
        int width = 0, height = 0;
        require(SDL_GetWindowSizeInPixels(video.window(), &width, &height) && width == 64 &&
                    height == 48,
            "restore original client dimensions");
    }
}

IAMMultiMediaStream* new_stream(IDirectDraw* draw)
{
    IAMMultiMediaStream* stream = nullptr;
    require(CoCreateInstance(CLSID_AMMultiMediaStream,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IAMMultiMediaStream,
                reinterpret_cast<void**>(&stream)) == S_OK,
        "create multimedia stream");
    require(stream->Initialize(STREAMTYPE_READ, AMMSF_NOGRAPHTHREAD, nullptr) == S_OK,
        "initialize read stream");
    require(stream->AddMediaStream(draw, &MSPID_PrimaryVideo, 0, nullptr) == S_OK, "add video");
    require(stream->AddMediaStream(
                nullptr, &MSPID_PrimaryAudio, AMMSF_ADDDEFAULTRENDERER, nullptr) == S_OK,
        "add audio renderer");
    return stream;
}

HRESULT open_file(IAMMultiMediaStream* stream, const char* path)
{
    WCHAR wide[260];
    require(MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 260) > 0, "convert fixture path");
    return stream->OpenFile(wide, 0);
}

void playback(Surfaces& surfaces, const std::shared_ptr<MemorySource>& source, bool slow = false)
{
    const auto closed_before = source->counts->closed;
    // More than two 25 fps frame intervals: Update must decode past stale
    // pictures after Flip returns, including dependent MPEG B/P frames.
    surfaces.video.presentation_delay = slow ? 95 : 0;
    auto* stream = new_stream(surfaces.draw);
    require(FAILED(open_file(stream, "missing.mpg")), "missing VFS movie must fail");
    require(open_file(stream, "dAtA\\MoViE.MpG") == S_OK, "open generated MPEG through VFS");
    IMediaStream* media = nullptr;
    require(stream->GetMediaStream(MSPID_PrimaryVideo, &media) == S_OK, "get video media stream");
    IDirectDrawMediaStream* video = nullptr;
    require(
        media->QueryInterface(IID_IDirectDrawMediaStream, reinterpret_cast<void**>(&video)) == S_OK,
        "query DD stream");
    DDSURFACEDESC desc{};
    desc.dwSize = sizeof(desc);
    require(video->GetFormat(&desc, nullptr, nullptr, nullptr) == S_OK && desc.dwWidth == 32 &&
                desc.dwHeight == 32,
        "movie dimensions must come from MPEG headers");
    IDirectDrawSurface* surface = nullptr;
    require(surfaces.draw->CreateSurface(&desc, &surface, nullptr) == DD_OK,
        "create movie sample surface");
    RECT rectangle{0, 0, 32, 32};
    IDirectDrawStreamSample* sample = nullptr;
    require(video->CreateSample(surface, &rectangle, 0, &sample) == S_OK, "create movie sample");
    require(sample->Update(0, nullptr, nullptr, 0) == MS_E_NOTRUNNING,
        "stopped sample update must fail");
    require(stream->SetState(STREAMSTATE_RUN) == S_OK, "run stream");
    SDL_AudioSpec output_spec{};
    require(SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &output_spec, nullptr) &&
                output_spec.format == SDL_AUDIO_F32 && output_spec.channels == 2 &&
                output_spec.freq == 44100,
        "generated audio capture must use float stereo at 44100 Hz");
    const auto begin = SDL_GetTicksNS();
    unsigned frames = 0;
    bool dropped = false;
    STREAM_TIME previous = -1;
    for (;;)
    {
        const auto result = sample->Update(0, nullptr, nullptr, 0);
        if (result != S_OK)
        {
            require(result == MS_S_ENDOFSTREAM, "expected EOF status");
            break;
        }
        STREAM_TIME start, end, current;
        require(sample->GetSampleTimes(&start, &end, &current) == S_OK && start > previous &&
                    end - start >= 399999 && end - start <= 400001,
            "frame timestamps/order must be 25 fps");
        require(current >= start && current < end,
            "Update must return a frame covering the current stream time");
        dropped |= previous >= 0 && start - previous > 400001;
        previous = start;
        RECT destination{0, 0, 64, 48};
        require(surfaces.back->Blt(&destination, surface, &rectangle, DDBLT_WAIT, nullptr) == DD_OK,
            "blit MPEG frame");
        require(surfaces.primary->Flip(nullptr, 0) == DD_OK, "flip MPEG frame");
        ++frames;
        if (!slow && frames == 2)
        {
            require(stream->SetState(STREAMSTATE_STOP) == S_OK, "pause movie");
            STREAM_TIME before, after;
            stream->GetTime(&before);
            SDL_Delay(50);
            stream->GetTime(&after);
            require(before == after, "stopped stream clock must not advance");
            require(stream->SetState(STREAMSTATE_RUN) == S_OK, "resume movie");
        }
        require(frames <= 8, "decoder repeated video frames");
    }
    const double elapsed = (SDL_GetTicksNS() - begin) / 1e9;
    if (slow)
    {
        require(frames > 0 && frames < 8 && dropped && elapsed >= 0.32 && elapsed < 0.50,
            "slow presentation must drop frames and finish on the stream clock");
    }
    else
    {
        require(frames == 8 && !dropped && elapsed >= 0.34 && elapsed < 3,
            "clock must pace all eight frames");
    }
    require(sample->Update(0, nullptr, nullptr, 0) == MS_S_ENDOFSTREAM, "EOF must be stable");
    require(stream->SetState(STREAMSTATE_STOP) == S_OK, "stop completed stream");
    const auto& pixels = surfaces.video.last.pixels;
    require(std::any_of(pixels.begin(),
                pixels.end(),
                [](auto p)
                {
                    return (p & 0xffffffu) != 0;
                }) &&
                pixels.front() != pixels.back(),
        "decoded video must contain colour and image detail");
    surface->Release(); // Sample must retain this surface despite retail release order.
    stream->Release();  // Samples/media must also retain their stream owner.
    video->Release();
    media->Release();
    require(
        source->counts->closed == closed_before, "sample must keep its VFS/decoder owner alive");
    sample->Release();
    require(source->counts->opened == source->counts->closed && source->counts->reads > 1 &&
                source->counts->seeks > 0,
        "VFS stream must be read, seeked and closed exactly once");
    std::printf("movie: %u frames, %.3f seconds, %u ms presentation, VFS lifetime verified\n",
        frames,
        elapsed,
        surfaces.video.presentation_delay);
    surfaces.video.presentation_delay = 0;
}

void loud_case(const char* executable, const char* argument)
{
    const pid_t child = fork();
    require(child >= 0, "fork failed");
    if (child == 0)
    {
        execl(executable, executable, argument, nullptr);
        _exit(127);
    }
    int status = 0;
    require(
        waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
        "unsupported movie operation must stop loudly");
}
} // namespace

int main(int argc, char** argv)
{
    setenv("BEACHLIFE_BOOT_QUIET", "1", 1);
    unsetenv("BEACHLIFE_BOOT_SKIP_MOVIE");
    if (argc == 1)
    {
        skip_path();
        loud_case(argv[0], "--unsupported");
        loud_case(argv[0], "--broken");
    }
    const char* driver = std::getenv("SDL_VIDEODRIVER");
    require(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, driver ? driver : "dummy"), "select video driver");
    require(SDL_Init(SDL_INIT_VIDEO), "initialize SDL video");
    const auto audio_path = std::filesystem::temp_directory_path() /
                            ("beachlife-movie-" + std::to_string(getpid()) + ".raw");
    require(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk"), "select disk audio");
    require(SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, audio_path.c_str()),
        "set generated audio output");
    auto source = std::make_shared<MemorySource>();
    files::Session vfs(source);
    {
        Surfaces surfaces;
        if (argc > 1)
        {
            if (std::strcmp(argv[1], "--unsupported") == 0)
            {
                surfaces.draw->Compact();
            }
            auto* stream = new_stream(surfaces.draw);
            open_file(stream, "broken.mpg");
            return 2;
        }
        display_mode();
        surface_operations(surfaces);
        playback(surfaces, source);
        playback(surfaces, source, true);
    }
    SDL_Quit();
    std::ifstream audio_file(audio_path, std::ios::binary);
    const std::vector<char> samples((std::istreambuf_iterator<char>(audio_file)), {});
    require(samples.size() > 4096 && std::any_of(samples.begin(),
                                         samples.end(),
                                         [](char b)
                                         {
                                             return b != 0;
                                         }),
        "MP2 must produce non-silent SDL audio output");
    std::vector<float> mono;
    for (size_t i = 0; i + 8 <= samples.size(); i += 8)
    {
        float left, right;
        std::memcpy(&left, samples.data() + i, 4);
        std::memcpy(&right, samples.data() + i + 4, 4);
        require(std::isfinite(left) && std::abs(left) <= 1 && std::abs(left - right) < 0.005f,
            "decoded MP2 channels must be finite matching sine waves");
        mono.push_back(left);
    }
    double correlation = 0, energy = 0;
    for (size_t i = 44; i < mono.size(); ++i)
    {
        correlation += double(mono[i]) * mono[i - 44];
        energy += double(mono[i]) * mono[i];
    }
    require(energy > 1 && correlation / energy > 0.95,
        "MP2 output must retain the generated 997 Hz tone");
    audio_file.close();
    std::filesystem::remove(audio_path);
    std::printf("movie: %zu bytes of SDL audio verified\n", samples.size());
    return 0;
}

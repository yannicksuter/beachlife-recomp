#include "amstream.h"
#include "ddstream.h"
#include "movie_common.h"
#include "movie_defaults.h"
#include "runtime/retail_files.h"

#define PLM_NO_STDIO
#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace beachlife::compat::movie
{
namespace
{
namespace files = runtime::retail_files;
constexpr double audio_lead = 0.2;
constexpr double ticks_per_second = 10000000.0;

struct Movie
{
    std::thread::id thread = std::this_thread::get_id();
    IDirectDraw* draw = nullptr;
    files::File* file = nullptr;
    files::Position file_length = 0;
    plm_t* decoder = nullptr;
    SDL_AudioStream* audio = nullptr;
    bool initialized = false, video_added = false, audio_added = false, audio_initialized = false;
    bool audio_ended = false, video_ended = false;
    STREAM_STATE state = STREAMSTATE_STOP;
    Uint64 epoch = 0;
    double stopped_time = 0, audio_until = 0, video_until = 0;
    std::vector<uint8_t> rgb;

    ~Movie()
    {
        if (audio)
        {
            SDL_DestroyAudioStream(audio);
        }
        if (audio_initialized)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        if (decoder)
        {
            plm_destroy(decoder);
        }
        if (file)
        {
            files::close(file);
        }
        if (draw)
        {
            draw->Release();
        }
    }
    void check(const char* api) const
    {
        thread_check(thread, api);
    }
    double time() const
    {
        // A shared monotonic stream clock schedules video and audio read-ahead.
        // Stopping freezes this origin and pauses the SDL audio device together.
        return stopped_time + (state == STREAMSTATE_RUN ? (SDL_GetTicksNS() - epoch) / 1e9 : 0);
    }
    static void load(plm_buffer_t* buffer, void* user)
    {
        auto& self = *static_cast<Movie*>(user);
        std::array<uint8_t, 64 * 1024> bytes;
        const auto count = files::read(bytes.data(), 1, bytes.size(), self.file);
        if (count)
        {
            if (plm_buffer_write(buffer, bytes.data(), count) != count)
            {
                unsupported("MPEG VFS read", "decoder buffer allocation failed");
            }
        }
        else
        {
            files::Position position = 0;
            if (files::position(self.file, &position) || position != self.file_length)
            {
                unsupported("MPEG VFS read", "stream read failed before the end of the file");
            }
            plm_buffer_signal_end(buffer);
        }
    }
    static void seek(plm_buffer_t*, size_t position, void* user)
    {
        auto& self = *static_cast<Movie*>(user);
        if (position > INT32_MAX ||
            files::seek(self.file, static_cast<int32_t>(position), files::SeekOrigin::begin) != 0)
        {
            unsupported("MPEG VFS seek", "stream seek failed");
        }
    }
    static size_t tell(plm_buffer_t*, void* user)
    {
        files::Position position = 0;
        if (files::position(static_cast<Movie*>(user)->file, &position) != 0)
        {
            unsupported("MPEG VFS position", "stream position failed");
        }
        return static_cast<size_t>(position);
    }
    void fill_audio(double until)
    {
        if (!audio || audio_ended)
        {
            return;
        }
        while (audio_until < until)
        {
            auto* samples = plm_decode_audio(decoder);
            if (!samples)
            {
                if (!plm_has_ended(decoder))
                {
                    unsupported("MPEG audio decode", "invalid or unsupported MP2 stream");
                }
                audio_ended = true;
                if (!SDL_FlushAudioStream(audio))
                {
                    unsupported("MPEG audio flush", SDL_GetError());
                }
                break;
            }
            if (!SDL_PutAudioStreamData(audio,
                    samples->interleaved,
                    static_cast<int>(samples->count * 2 * sizeof(float))))
            {
                unsupported("MPEG audio queue", SDL_GetError());
            }
            audio_until = samples->time + double(samples->count) / plm_get_samplerate(decoder);
        }
    }
    void wait_until(double target)
    {
        while (time() < target)
        {
            fill_audio(time() + audio_lead);
            // Pump, but do not remove, input events. Retail GetAsyncKeyState
            // and its window procedure must still observe Escape and close.
            SDL_PumpEvents();
            SDL_Delay(1);
        }
    }
    HRESULT open(LPCWSTR path, DWORD flags)
    {
        check("IAMMultiMediaStream::OpenFile");
        if (flags)
        {
            unsupported("IAMMultiMediaStream::OpenFile", "open flags");
        }
        if (!initialized || !video_added || !draw || !path)
        {
            return E_FAIL;
        }
        if (file || decoder)
        {
            unsupported("IAMMultiMediaStream::OpenFile", "reopening a stream");
        }
        const int bytes = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, nullptr, 0, nullptr, nullptr);
        if (!bytes)
        {
            return DDERR_INVALIDPARAMS;
        }
        std::string name(bytes, '\0');
        if (!WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, name.data(), bytes, nullptr, nullptr))
        {
            return DDERR_INVALIDPARAMS;
        }
        file = files::open(name.c_str(), "rb");
        if (!file)
        {
            return static_cast<HRESULT>(0x80070002);
        }
        files::Position length = 0;
        if (files::seek(file, 0, files::SeekOrigin::end) || files::position(file, &length) ||
            files::seek(file, 0, files::SeekOrigin::begin))
        {
            return E_FAIL;
        }
        if (!length || length > INT32_MAX)
        {
            unsupported("IAMMultiMediaStream::OpenFile", "empty or larger than 2 GiB MPEG stream");
        }
        file_length = length;
        auto* buffer = plm_buffer_create_with_callbacks(load, seek, tell, length, this);
        if (!buffer)
        {
            return DDERR_OUTOFMEMORY;
        }
        decoder = plm_create_with_buffer(buffer, 1);
        if (!decoder)
        {
            plm_buffer_destroy(buffer);
            return DDERR_OUTOFMEMORY;
        }
        if (!plm_has_headers(decoder) || plm_get_num_video_streams(decoder) != 1)
        {
            unsupported("IAMMultiMediaStream::OpenFile",
                "requires MPEG-1 program stream video and MP2 audio");
        }
        const int width = plm_get_width(decoder), height = plm_get_height(decoder);
        const double fps = plm_get_framerate(decoder);
        if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || !std::isfinite(fps) ||
            fps <= 0)
        {
            unsupported("IAMMultiMediaStream::OpenFile", "invalid movie dimensions or frame rate");
        }
        if ((width & 1) || (height & 1))
        {
            unsupported("IAMMultiMediaStream::OpenFile",
                "pl_mpeg colour conversion requires even dimensions");
        }
        rgb.resize(static_cast<size_t>(width) * height * 3);
        const bool have_audio = audio_added && plm_get_num_audio_streams(decoder) > 0;
        plm_set_audio_enabled(decoder, have_audio);
        if (have_audio)
        {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            {
                unsupported("MPEG audio initialization", SDL_GetError());
            }
            audio_initialized = true;
            SDL_AudioSpec spec{SDL_AUDIO_F32, 2, plm_get_samplerate(decoder)};
            audio = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (!audio)
            {
                unsupported("MPEG audio device", SDL_GetError());
            }
        }
        return S_OK;
    }
    HRESULT set_state(STREAM_STATE next)
    {
        check("IMultiMediaStream::SetState");
        if (next != STREAMSTATE_STOP && next != STREAMSTATE_RUN)
        {
            return DDERR_INVALIDPARAMS;
        }
        if (next == state)
        {
            return S_OK;
        }
        if (!decoder)
        {
            return E_FAIL;
        }
        if (next == STREAMSTATE_RUN)
        {
            fill_audio(stopped_time + audio_lead);
            epoch = SDL_GetTicksNS();
            if (audio && !SDL_ResumeAudioStreamDevice(audio))
            {
                unsupported("MPEG audio resume", SDL_GetError());
            }
        }
        else
        {
            stopped_time = time();
            if (audio && !SDL_PauseAudioStreamDevice(audio))
            {
                unsupported("MPEG audio pause", SDL_GetError());
            }
        }
        state = next;
        return S_OK;
    }
};

class Media;
class Multi final : public IAMMultiMediaStreamDefaults
{
  public:
    Multi()
        : movie_(std::make_shared<Movie>())
    {
    }
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (!same(iid, unknown_iid) && !same(iid, IID_IMultiMediaStream) &&
            !same(iid, IID_IAMMultiMediaStream))
        {
            return no_interface;
        }
        *output = static_cast<IAMMultiMediaStream*>(this);
        AddRef();
        return S_OK;
    }
    ULONG AddRef() override
    {
        return ++references_;
    }
    ULONG Release() override
    {
        const auto count = --references_;
        if (!count)
        {
            delete this;
        }
        return count;
    }
    HRESULT Initialize(STREAM_TYPE type, DWORD flags, IGraphBuilder* graph) override
    {
        movie_->check("IAMMultiMediaStream::Initialize");
        if (type != STREAMTYPE_READ || flags != AMMSF_NOGRAPHTHREAD || graph)
        {
            unsupported("IAMMultiMediaStream::Initialize",
                "requires synchronous reading without a filter graph");
        }
        if (movie_->initialized)
        {
            return static_cast<HRESULT>(0x800704df); // already initialized
        }
        movie_->initialized = true;
        return S_OK;
    }
    HRESULT AddMediaStream(
        IUnknown* object, const MSPID* purpose, DWORD flags, IMediaStream** output) override
    {
        if (output)
        {
            *output = nullptr;
        }
        if (!purpose)
        {
            return pointer_error;
        }
        movie_->check("IAMMultiMediaStream::AddMediaStream");
        if (!movie_->initialized || movie_->decoder)
        {
            return E_FAIL;
        }
        if (same(*purpose, MSPID_PrimaryVideo))
        {
            if (flags)
            {
                unsupported("IAMMultiMediaStream::AddMediaStream", "video flags");
            }
            if (!object || movie_->video_added)
            {
                return DDERR_INVALIDPARAMS;
            }
            const auto result =
                object->QueryInterface(IID_IDirectDraw, reinterpret_cast<void**>(&movie_->draw));
            if (FAILED(result))
            {
                return result;
            }
            movie_->video_added = true;
            return output ? GetMediaStream(*purpose, output) : S_OK;
        }
        if (same(*purpose, MSPID_PrimaryAudio))
        {
            if (object || flags != AMMSF_ADDDEFAULTRENDERER || output)
            {
                unsupported("IAMMultiMediaStream::AddMediaStream",
                    "only the default audio renderer is supported");
            }
            movie_->audio_added = true;
            return S_OK;
        }
        unsupported("IAMMultiMediaStream::AddMediaStream", "media purpose");
    }
    HRESULT OpenFile(LPCWSTR path, DWORD flags) override
    {
        return boundary("IAMMultiMediaStream::OpenFile",
            [&]
            {
                return movie_->open(path, flags);
            });
    }
    HRESULT GetMediaStream(REFMSPID purpose, IMediaStream** output) override;
    HRESULT GetState(STREAM_STATE* output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = movie_->state;
        return S_OK;
    }
    HRESULT SetState(STREAM_STATE state) override
    {
        return boundary("IMultiMediaStream::SetState",
            [&]
            {
                return movie_->set_state(state);
            });
    }
    HRESULT GetTime(STREAM_TIME* output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = static_cast<STREAM_TIME>(movie_->time() * ticks_per_second);
        return S_OK;
    }

  private:
    ULONG references_ = 1;
    std::shared_ptr<Movie> movie_;
};

class Sample final : public IDirectDrawStreamSampleDefaults
{
  public:
    Sample(std::shared_ptr<Movie> movie,
        IMediaStream* media,
        IDirectDrawSurface* surface,
        RECT rectangle)
        : movie_(std::move(movie))
        , media_(media)
        , surface_(surface)
        , rectangle_(rectangle)
    {
        media_->AddRef();
        surface_->AddRef();
    }
    ~Sample()
    {
        surface_->Release();
        media_->Release();
    }
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (!same(iid, unknown_iid) && !same(iid, IID_IStreamSample) &&
            !same(iid, IID_IDirectDrawStreamSample))
        {
            return no_interface;
        }
        *output = static_cast<IDirectDrawStreamSample*>(this);
        AddRef();
        return S_OK;
    }
    ULONG AddRef() override
    {
        return ++references_;
    }
    ULONG Release() override
    {
        const auto count = --references_;
        if (!count)
        {
            delete this;
        }
        return count;
    }
    HRESULT GetMediaStream(IMediaStream** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = media_;
        media_->AddRef();
        return S_OK;
    }
    HRESULT GetSurface(IDirectDrawSurface** surface, RECT* rectangle) override
    {
        if (surface)
        {
            *surface = surface_;
            surface_->AddRef();
        }
        if (rectangle)
        {
            *rectangle = rectangle_;
        }
        return S_OK;
    }
    HRESULT GetSampleTimes(STREAM_TIME* start, STREAM_TIME* end, STREAM_TIME* current) override
    {
        if (start)
        {
            *start = start_;
        }
        if (end)
        {
            *end = end_;
        }
        if (current)
        {
            *current = static_cast<STREAM_TIME>(movie_->time() * ticks_per_second);
        }
        return S_OK;
    }
    HRESULT Update(DWORD flags, HANDLE event, PAPCFUNC callback, DWORD callback_data) override
    {
        return boundary("IStreamSample::Update",
            [&]() -> HRESULT
            {
                movie_->check("IStreamSample::Update");
                if (flags || event || callback || callback_data)
                {
                    unsupported("IStreamSample::Update", "asynchronous samples or notifications");
                }
                if (movie_->state != STREAMSTATE_RUN)
                {
                    return MS_E_NOTRUNNING;
                }
                if (movie_->video_ended)
                {
                    return MS_S_ENDOFSTREAM;
                }
                for (;;)
                {
                    movie_->fill_audio(movie_->time() + audio_lead);
                    auto* frame = plm_decode_video(movie_->decoder);
                    if (!frame)
                    {
                        if (!plm_has_ended(movie_->decoder))
                        {
                            unsupported("MPEG video decode", "invalid MPEG-1 frame");
                        }
                        // Keep the final picture visible through its interval and let
                        // the queued MP2 tail reach the device before retail stops it.
                        while (movie_->audio && !movie_->audio_ended)
                        {
                            movie_->fill_audio(movie_->time() + audio_lead);
                            movie_->wait_until(movie_->time() + 0.005);
                        }
                        movie_->wait_until(std::max(movie_->video_until, movie_->audio_until));
                        movie_->video_ended = true;
                        return MS_S_ENDOFSTREAM;
                    }
                    // Decode reference frames even when the caller's previous Blt/Flip
                    // took too long. Only deliver pictures whose presentation interval
                    // has not expired on the stream clock; never retime audio or video
                    // to accommodate the presentation cost.
                    movie_->video_until = frame->time + 1.0 / plm_get_framerate(movie_->decoder);
                    if (movie_->time() >= movie_->video_until)
                    {
                        continue;
                    }
                    movie_->wait_until(frame->time);
                    if (movie_->time() >= movie_->video_until)
                    {
                        continue;
                    }
                    const int width = plm_get_width(movie_->decoder),
                              height = plm_get_height(movie_->decoder);
                    if (frame->width != static_cast<unsigned>(width) ||
                        frame->height != static_cast<unsigned>(height))
                    {
                        unsupported("MPEG video decode", "midstream resolution changes");
                    }
                    plm_frame_to_rgb(frame, movie_->rgb.data(), width * 3);
                    DDSURFACEDESC desc{};
                    desc.dwSize = sizeof(desc);
                    const auto result =
                        surface_->Lock(&rectangle_, &desc, DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr);
                    if (FAILED(result))
                    {
                        return result;
                    }
                    for (int y = 0; y < height; ++y)
                    {
                        for (int x = 0; x < width; ++x)
                        {
                            const auto* pixel = movie_->rgb.data() + (y * width + x) * 3;
                            const unsigned word =
                                ((pixel[0] >> 3) << 11) | ((pixel[1] >> 2) << 5) | (pixel[2] >> 3);
                            auto* output =
                                static_cast<BYTE*>(desc.lpSurface) + y * desc.lPitch + x * 2;
                            output[0] = word;
                            output[1] = word >> 8;
                        }
                    }
                    const auto unlocked = surface_->Unlock(desc.lpSurface);
                    if (FAILED(unlocked))
                    {
                        return unlocked;
                    }
                    // Conversion or surface access may also have missed this frame's
                    // deadline. Catch up before returning control to retail's renderer.
                    if (movie_->time() >= movie_->video_until)
                    {
                        continue;
                    }
                    start_ = static_cast<STREAM_TIME>(frame->time * ticks_per_second);
                    end_ = static_cast<STREAM_TIME>(movie_->video_until * ticks_per_second);
                    return S_OK;
                }
            });
    }

  private:
    ULONG references_ = 1;
    std::shared_ptr<Movie> movie_;
    IMediaStream* media_;
    IDirectDrawSurface* surface_;
    RECT rectangle_;
    STREAM_TIME start_ = 0, end_ = 0;
};

class Media final : public IDirectDrawMediaStreamDefaults
{
  public:
    Media(std::shared_ptr<Movie> movie, Multi* owner)
        : movie_(std::move(movie))
        , owner_(owner)
    {
        owner_->AddRef();
    }
    ~Media()
    {
        owner_->Release();
    }
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (!same(iid, unknown_iid) && !same(iid, IID_IMediaStream) &&
            !same(iid, IID_IDirectDrawMediaStream))
        {
            return no_interface;
        }
        *output = static_cast<IDirectDrawMediaStream*>(this);
        AddRef();
        return S_OK;
    }
    ULONG AddRef() override
    {
        return ++references_;
    }
    ULONG Release() override
    {
        const auto count = --references_;
        if (!count)
        {
            delete this;
        }
        return count;
    }
    HRESULT GetMultiMediaStream(IMultiMediaStream** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = owner_;
        owner_->AddRef();
        return S_OK;
    }
    HRESULT GetInformation(MSPID* purpose, STREAM_TYPE* type) override
    {
        if (purpose)
        {
            *purpose = MSPID_PrimaryVideo;
        }
        if (type)
        {
            *type = STREAMTYPE_READ;
        }
        return S_OK;
    }
    HRESULT GetFormat(DDSURFACEDESC* current,
        IDirectDrawPalette** palette,
        DDSURFACEDESC* desired,
        DWORD* flags) override
    {
        movie_->check("IDirectDrawMediaStream::GetFormat");
        if (!movie_->decoder)
        {
            return E_FAIL;
        }
        if ((current && current->dwSize != sizeof(*current)) ||
            (desired && desired->dwSize != sizeof(*desired)))
        {
            return DDERR_INVALIDPARAMS;
        }
        DDSURFACEDESC desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_PIXELFORMAT;
        desc.dwWidth = plm_get_width(movie_->decoder);
        desc.dwHeight = plm_get_height(movie_->decoder);
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.ddpfPixelFormat = {sizeof(DDPIXELFORMAT), DDPF_RGB, 0, 16, 0xf800, 0x07e0, 0x001f, 0};
        if (current)
        {
            *current = desc;
        }
        if (desired)
        {
            *desired = desc;
        }
        if (palette)
        {
            *palette = nullptr;
        }
        if (flags)
        {
            *flags = 0;
        }
        return S_OK;
    }
    HRESULT GetDirectDraw(IDirectDraw** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = movie_->draw;
        if (!*output)
        {
            return E_FAIL;
        }
        movie_->draw->AddRef();
        return S_OK;
    }
    HRESULT GetTimePerFrame(STREAM_TIME* output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        if (!movie_->decoder)
        {
            return E_FAIL;
        }
        *output = static_cast<STREAM_TIME>(ticks_per_second / plm_get_framerate(movie_->decoder));
        return S_OK;
    }
    HRESULT CreateSample(IDirectDrawSurface* surface,
        const RECT* rectangle,
        DWORD flags,
        IDirectDrawStreamSample** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (flags)
        {
            unsupported("IDirectDrawMediaStream::CreateSample", "sample flags");
        }
        if (!surface || !movie_->decoder)
        {
            return DDERR_INVALIDPARAMS;
        }
        DDSURFACEDESC desc{};
        desc.dwSize = sizeof(desc);
        const auto result = surface->GetSurfaceDesc(&desc);
        if (FAILED(result))
        {
            return result;
        }
        const RECT r =
            rectangle
                ? *rectangle
                : RECT{0, 0, static_cast<LONG>(desc.dwWidth), static_cast<LONG>(desc.dwHeight)};
        if (r.left < 0 || r.top < 0 || r.right > static_cast<LONG>(desc.dwWidth) ||
            r.bottom > static_cast<LONG>(desc.dwHeight) ||
            int64_t(r.right) - r.left != plm_get_width(movie_->decoder) ||
            int64_t(r.bottom) - r.top != plm_get_height(movie_->decoder))
        {
            return DDERR_INVALIDRECT;
        }
        const auto& f = desc.ddpfPixelFormat;
        if (f.dwFlags != DDPF_RGB || f.dwRGBBitCount != 16 || f.dwRBitMask != 0xf800 ||
            f.dwGBitMask != 0x07e0 || f.dwBBitMask != 0x001f || f.dwRGBAlphaBitMask)
        {
            unsupported("IDirectDrawMediaStream::CreateSample", "requires an RGB565 surface");
        }
        return boundary("IDirectDrawMediaStream::CreateSample",
            [&]() -> HRESULT
            {
                *output = new Sample(movie_, this, surface, r);
                return S_OK;
            });
    }

  private:
    ULONG references_ = 1;
    std::shared_ptr<Movie> movie_;
    Multi* owner_;
};

HRESULT Multi::GetMediaStream(REFMSPID purpose, IMediaStream** output)
{
    if (!output)
    {
        return pointer_error;
    }
    *output = nullptr;
    if (!same(purpose, MSPID_PrimaryVideo) || !movie_->video_added)
    {
        return MS_E_NOSTREAM;
    }
    return boundary("IMultiMediaStream::GetMediaStream",
        [&]() -> HRESULT
        {
            *output = new Media(movie_, this);
            return S_OK;
        });
}

// DirectDraw failure does not clear the retail ready flag. This static failure
// object handles the follow-on open without allocating a leaked stream owner.
class UnavailableStream final : public IAMMultiMediaStreamDefaults
{
  public:
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (!same(iid, unknown_iid) && !same(iid, IID_IAMMultiMediaStream) &&
            !same(iid, IID_IMultiMediaStream))
        {
            return no_interface;
        }
        *output = static_cast<IAMMultiMediaStream*>(this);
        AddRef();
        return S_OK;
    }
    ULONG AddRef() override
    {
        return ++references_;
    }
    ULONG Release() override
    {
        return references_ ? --references_ : 0;
    }
    HRESULT Initialize(STREAM_TYPE, DWORD, IGraphBuilder*) override
    {
        return DDERR_UNSUPPORTED;
    }
    HRESULT AddMediaStream(IUnknown*, const MSPID*, DWORD, IMediaStream** output) override
    {
        if (output)
        {
            *output = nullptr;
        }
        return DDERR_UNSUPPORTED;
    }
    HRESULT OpenFile(LPCWSTR, DWORD) override
    {
        return DDERR_UNSUPPORTED;
    }

  private:
    ULONG references_ = 0;
};
} // namespace
} // namespace beachlife::compat::movie

namespace beachlife::compat::movie
{
HRESULT create_stream_instance(REFIID iid, LPVOID* output)
{
    return boundary("CoCreateInstance",
        [&]() -> HRESULT
        {
            if (skipped())
            {
                static UnavailableStream stream;
                return stream.QueryInterface(iid, output);
            }
            auto* stream = new Multi;
            const auto result = stream->QueryInterface(iid, output);
            stream->Release();
            return result;
        });
}

} // namespace beachlife::compat::movie

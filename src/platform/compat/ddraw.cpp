#include "ddraw.h"
#include "movie_common.h"
#include "movie_defaults.h"
#include "platform/video.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace beachlife::compat::movie
{
namespace
{
constexpr HRESULT invalid_mode = static_cast<HRESULT>(0x88760078);
constexpr HRESULT no_cooperative_level = static_cast<HRESULT>(0x887600d4);

DDPIXELFORMAT rgb565()
{
    return {sizeof(DDPIXELFORMAT), DDPF_RGB, 0, 16, 0xf800, 0x07e0, 0x001f, 0};
}
bool is565(const DDPIXELFORMAT& format)
{
    const auto expected = rgb565();
    return format.dwSize == sizeof(format) && format.dwFlags == DDPF_RGB &&
           format.dwRGBBitCount == 16 && format.dwRBitMask == expected.dwRBitMask &&
           format.dwGBitMask == expected.dwGBitMask && format.dwBBitMask == expected.dwBBitMask &&
           !format.dwRGBAlphaBitMask;
}

struct Display
{
    HWND handle = nullptr;
    SDL_WindowID id = 0;
    std::thread::id thread = std::this_thread::get_id();
    int width = 0, height = 0;
    bool exclusive = false, emulated = false;
    bool saved = false, was_fullscreen = false, had_mode = false;
    SDL_DisplayMode previous_mode{};

    SDL_Window* window() const
    {
        thread_check(thread, "DirectDraw window");
        auto* result = SDL_GetWindowFromID(id);
        if (!handle || !result || result != video(handle)->window())
        {
            unsupported("DirectDraw window", "host window no longer exists");
        }
        return result;
    }
    void restore()
    {
        if (!saved)
        {
            return;
        }
        auto* target = window();
        if (!SDL_SetWindowFullscreenMode(target, had_mode ? &previous_mode : nullptr) ||
            !SDL_SetWindowFullscreen(target, was_fullscreen) || !SDL_SyncWindow(target))
        {
            unsupported("DirectDraw restore", SDL_GetError());
        }
        saved = false;
        exclusive = false;
        emulated = false;
        video(handle)->set_game_mode(0, 0, false);
    }
    ~Display()
    {
        restore();
    }
};

class Clipper final : public IDirectDrawClipperDefaults
{
  public:
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (!same(iid, unknown_iid) && !same(iid, IID_IDirectDrawClipper))
        {
            return no_interface;
        }
        *output = static_cast<IDirectDrawClipper*>(this);
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
    HRESULT SetHWnd(DWORD flags, HWND handle) override
    {
        if (flags)
        {
            unsupported("IDirectDrawClipper::SetHWnd", "flags");
        }
        if (handle && !video(handle)->window())
        {
            return DDERR_INVALIDPARAMS;
        }
        handle_ = handle;
        return DD_OK;
    }
    HRESULT GetHWnd(HWND* output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = handle_;
        return DD_OK;
    }

  private:
    ULONG references_ = 1;
    HWND handle_ = nullptr;
};

class Surface final : public IDirectDrawSurfaceDefaults, public IDirectDrawSurface4Defaults
{
  public:
    Surface(std::shared_ptr<Display> display, int width, int height, DWORD caps)
        : display_(std::move(display))
        , width_(width)
        , height_(height)
        , pitch_((width * 2 + 3) & ~3)
        , caps_(caps)
        , pixels_(static_cast<size_t>(pitch_) * height)
    {
    }
    ~Surface()
    {
        if (back_)
        {
            back_->Release();
        }
        if (clipper_)
        {
            clipper_->Release();
        }
    }
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (same(iid, unknown_iid) || same(iid, IID_IDirectDrawSurface))
        {
            *output = static_cast<IDirectDrawSurface*>(this);
        }
        else if (same(iid, IID_IDirectDrawSurface4))
        {
            *output = static_cast<IDirectDrawSurface4*>(this);
        }
        else
        {
            return no_interface;
        }
        AddRef();
        return DD_OK;
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
    void attach(Surface* surface)
    {
        back_ = surface;
    }

    template <class Description> HRESULT description(Description* output)
    {
        if (!output || output->dwSize != sizeof(Description))
        {
            return DDERR_INVALIDPARAMS;
        }
        *output = {};
        output->dwSize = sizeof(Description);
        output->dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
        output->dwWidth = width_;
        output->dwHeight = height_;
        output->lPitch = pitch_;
        output->ddpfPixelFormat = rgb565();
        output->ddsCaps.dwCaps = caps_;
        if (back_)
        {
            output->dwFlags |= DDSD_BACKBUFFERCOUNT;
            output->dwBackBufferCount = 1;
        }
        return DD_OK;
    }
    HRESULT GetSurfaceDesc(DDSURFACEDESC* out) override
    {
        return description(out);
    }
    HRESULT GetSurfaceDesc(DDSURFACEDESC2* out) override
    {
        return description(out);
    }
    HRESULT GetPixelFormat(DDPIXELFORMAT* output) override
    {
        if (!output || output->dwSize != sizeof(*output))
        {
            return DDERR_INVALIDPARAMS;
        }
        *output = rgb565();
        return DD_OK;
    }
    HRESULT GetCaps(DDSCAPS* out) override
    {
        if (!out)
        {
            return pointer_error;
        }
        *out = {caps_};
        return DD_OK;
    }
    HRESULT GetCaps(DDSCAPS2* out) override
    {
        if (!out)
        {
            return pointer_error;
        }
        *out = {caps_, 0, 0, 0};
        return DD_OK;
    }
    template <class Caps, class Interface> HRESULT attached(Caps* caps, Interface** output)
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (!caps || caps->dwCaps != DDSCAPS_BACKBUFFER)
        {
            return DDERR_INVALIDPARAMS;
        }
        if (!back_)
        {
            return DDERR_NOTFOUND;
        }
        *output = static_cast<Interface*>(back_);
        back_->AddRef();
        return DD_OK;
    }
    HRESULT GetAttachedSurface(DDSCAPS* caps, IDirectDrawSurface** out) override
    {
        return attached(caps, out);
    }
    HRESULT GetAttachedSurface(DDSCAPS2* caps, IDirectDrawSurface4** out) override
    {
        return attached(caps, out);
    }
    HRESULT SetClipper(IDirectDrawClipper* clipper) override
    {
        if (clipper)
        {
            clipper->AddRef();
        }
        if (clipper_)
        {
            clipper_->Release();
        }
        clipper_ = clipper;
        return DD_OK;
    }
    HRESULT GetClipper(IDirectDrawClipper** out) override
    {
        if (!out)
        {
            return pointer_error;
        }
        *out = clipper_;
        if (!clipper_)
        {
            return DDERR_NOTFOUND;
        }
        clipper_->AddRef();
        return DD_OK;
    }
    HRESULT IsLost() override
    {
        return DD_OK;
    } // CPU storage survives display changes.
    HRESULT Restore() override
    {
        return DD_OK;
    }

    RECT whole() const
    {
        return {0, 0, width_, height_};
    }
    bool contains(const RECT& r) const
    {
        return r.left >= 0 && r.top >= 0 && r.right > r.left && r.bottom > r.top &&
               r.right <= width_ && r.bottom <= height_;
    }
    template <class Description>
    HRESULT lock(RECT* rectangle, Description* output, DWORD flags, HANDLE event)
    {
        thread_check(display_->thread, "IDirectDrawSurface::Lock");
        if (flags & ~(DDLOCK_WAIT | DDLOCK_READONLY | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK) || event)
        {
            unsupported("IDirectDrawSurface::Lock", "lock flags or event notification");
        }
        if (locked_)
        {
            return DDERR_SURFACEBUSY;
        }
        const auto r = rectangle ? *rectangle : whole();
        if (!contains(r))
        {
            return DDERR_INVALIDRECT;
        }
        const auto result = description(output);
        if (FAILED(result))
        {
            return result;
        }
        output->dwFlags |= DDSD_LPSURFACE;
        output->dwWidth = r.right - r.left;
        output->dwHeight = r.bottom - r.top;
        lock_pointer_ = pixels_.data() + r.top * pitch_ + r.left * 2;
        output->lpSurface = lock_pointer_;
        locked_rectangle_ = r;
        locked_ = true;
        return DD_OK;
    }
    HRESULT Lock(RECT* r, DDSURFACEDESC* d, DWORD f, HANDLE e) override
    {
        return lock(r, d, f, e);
    }
    HRESULT Lock(RECT* r, DDSURFACEDESC2* d, DWORD f, HANDLE e) override
    {
        return lock(r, d, f, e);
    }
    HRESULT Unlock(LPVOID pointer) override
    {
        if (!locked_)
        {
            return DDERR_NOTLOCKED;
        }
        if (pointer && pointer != lock_pointer_)
        {
            return DDERR_INVALIDPARAMS;
        }
        locked_ = false;
        lock_pointer_ = nullptr;
        return DD_OK;
    }
    HRESULT Unlock(RECT* rectangle) override
    {
        if (rectangle && std::memcmp(rectangle, &locked_rectangle_, sizeof(RECT)))
        {
            return DDERR_INVALIDRECT;
        }
        return Unlock(static_cast<LPVOID>(nullptr));
    }
    HRESULT blit(
        RECT* destination, Surface* source, RECT* source_rect, DWORD flags, DDBLTFX* effects)
    {
        return boundary("IDirectDrawSurface::Blt",
            [&]() -> HRESULT
            {
                thread_check(display_->thread, "IDirectDrawSurface::Blt");
                if (flags & ~DDBLT_WAIT || effects)
                {
                    unsupported(
                        "IDirectDrawSurface::Blt", "only synchronous RGB copies are supported");
                }
                if (!source)
                {
                    return DDERR_INVALIDPARAMS;
                }
                if (locked_ || source->locked_)
                {
                    return DDERR_SURFACEBUSY;
                }
                RECT src = source_rect ? *source_rect : source->whole();
                RECT dst = destination ? *destination : whole();
                if (!source->contains(src) || dst.right <= dst.left || dst.bottom <= dst.top)
                {
                    return DDERR_INVALIDRECT;
                }
                if (!contains(dst) && !clipper_)
                {
                    return DDERR_INVALIDRECT;
                }
                const int64_t dw = int64_t(dst.right) - dst.left,
                              dh = int64_t(dst.bottom) - dst.top;
                // A snapshot also makes overlapping self-blits deterministic.
                const auto snapshot = source == this ? source->pixels_ : std::vector<BYTE>{};
                const BYTE* input = source == this ? snapshot.data() : source->pixels_.data();
                for (int y = std::max<LONG>(0, dst.top); y < std::min<LONG>(height_, dst.bottom);
                    ++y)
                {
                    const int sy = src.top + (int64_t(y) - dst.top) * (src.bottom - src.top) / dh;
                    for (int x = std::max<LONG>(0, dst.left); x < std::min<LONG>(width_, dst.right);
                        ++x)
                    {
                        const int sx =
                            src.left + (int64_t(x) - dst.left) * (src.right - src.left) / dw;
                        std::memcpy(pixels_.data() + y * pitch_ + x * 2,
                            input + sy * source->pitch_ + sx * 2,
                            2);
                    }
                }
                if ((caps_ & DDSCAPS_PRIMARYSURFACE) && !back_)
                {
                    present(pixels_);
                }
                return DD_OK;
            });
    }
    HRESULT Blt(RECT* d, IDirectDrawSurface* s, RECT* r, DWORD f, DDBLTFX* e) override
    {
        return blit(d, dynamic_cast<Surface*>(s), r, f, e);
    }
    HRESULT Blt(RECT* d, IDirectDrawSurface4* s, RECT* r, DWORD f, DDBLTFX* e) override
    {
        return blit(d, dynamic_cast<Surface*>(s), r, f, e);
    }
    HRESULT flip(Surface* target, DWORD flags)
    {
        return boundary("IDirectDrawSurface::Flip",
            [&]() -> HRESULT
            {
                if (flags & ~DDFLIP_WAIT || (target && target != back_))
                {
                    unsupported("IDirectDrawSurface::Flip", "alternate targets or flip flags");
                }
                if (!back_)
                {
                    return static_cast<HRESULT>(0x88760246); // DDERR_NOTFLIPPABLE
                }
                if (locked_ || back_->locked_)
                {
                    return DDERR_SURFACEBUSY;
                }
                present(back_->pixels_);
                pixels_.swap(back_->pixels_);
                return DD_OK;
            });
    }
    HRESULT Flip(IDirectDrawSurface* t, DWORD flags) override
    {
        auto* target = dynamic_cast<Surface*>(t);
        return t && !target ? DDERR_INVALIDPARAMS : flip(target, flags);
    }
    HRESULT Flip(IDirectDrawSurface4* t, DWORD flags) override
    {
        auto* target = dynamic_cast<Surface*>(t);
        return t && !target ? DDERR_INVALIDPARAMS : flip(target, flags);
    }

  private:
    void present(const std::vector<BYTE>& pixels)
    {
        display_->window();
        platform::Frame frame;
        frame.width = width_;
        frame.height = height_;
        frame.pixels.resize(static_cast<size_t>(width_) * height_);
        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {
                const auto* p = pixels.data() + y * pitch_ + x * 2;
                const unsigned word = p[0] | unsigned(p[1]) << 8;
                const unsigned r = word >> 11, g = (word >> 5) & 63, b = word & 31;
                frame.pixels[y * width_ + x] = ((r << 3) | (r >> 2)) |
                                               (((g << 2) | (g >> 4)) << 8) |
                                               (((b << 3) | (b >> 2)) << 16) | 0xff000000u;
            }
        }
        video(display_->handle)->present(frame, nullptr);
    }
    ULONG references_ = 1;
    std::shared_ptr<Display> display_;
    int width_, height_, pitch_;
    DWORD caps_;
    std::vector<BYTE> pixels_;
    Surface* back_ = nullptr;
    IDirectDrawClipper* clipper_ = nullptr;
    bool locked_ = false;
    void* lock_pointer_ = nullptr;
    RECT locked_rectangle_{};
};

class Draw final : public IDirectDrawDefaults, public IDirectDraw4Defaults
{
  public:
    Draw()
        : display_(std::make_shared<Display>())
    {
    }
    HRESULT QueryInterface(REFIID iid, void** output) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (same(iid, unknown_iid) || same(iid, IID_IDirectDraw))
        {
            *output = static_cast<IDirectDraw*>(this);
        }
        else if (same(iid, IID_IDirectDraw4))
        {
            *output = static_cast<IDirectDraw4*>(this);
        }
        else
        {
            return no_interface;
        }
        AddRef();
        return DD_OK;
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
    HRESULT SetCooperativeLevel(HWND handle, DWORD flags) override
    {
        thread_check(display_->thread, "IDirectDraw::SetCooperativeLevel");
        const DWORD mode = flags & ~DDSCL_FPUSETUP;
        if (mode != DDSCL_NORMAL && mode != (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN))
        {
            unsupported("IDirectDraw::SetCooperativeLevel", "cooperative flags");
        }
        if (!handle)
        {
            return DDERR_INVALIDPARAMS;
        }
        if (display_->handle && display_->handle != handle)
        {
            unsupported("IDirectDraw::SetCooperativeLevel", "changing host windows");
        }
        auto* target = video(handle)->window();
        if (!target)
        {
            return DDERR_INVALIDPARAMS;
        }
        display_->handle = handle;
        display_->id = SDL_GetWindowID(target);
        if (mode == DDSCL_NORMAL)
        {
            display_->restore();
            if (!SDL_GetWindowSizeInPixels(target, &display_->width, &display_->height))
            {
                unsupported("IDirectDraw::SetCooperativeLevel", SDL_GetError());
            }
            return DD_OK;
        }
        if (!display_->saved)
        {
            display_->was_fullscreen = (SDL_GetWindowFlags(target) & SDL_WINDOW_FULLSCREEN) != 0;
            if (const auto* previous = SDL_GetWindowFullscreenMode(target))
            {
                display_->previous_mode = *previous;
                display_->had_mode = true;
            }
            display_->saved = true;
        }
        // Match the renderer: the player's window preference owns presentation.
        // A windowed host emulates the game's exclusive mode in CPU surfaces.
        display_->emulated = !(SDL_GetWindowFlags(target) & SDL_WINDOW_FULLSCREEN);
        display_->exclusive = true;
        return DD_OK;
    }
    HRESULT SetDisplayMode(DWORD w, DWORD h, DWORD bits) override
    {
        return SetDisplayMode(w, h, bits, 0, 0);
    }
    HRESULT SetDisplayMode(DWORD w, DWORD h, DWORD bits, DWORD refresh, DWORD flags) override
    {
        if (bits != 16 || flags)
        {
            unsupported("IDirectDraw::SetDisplayMode", "only 16-bit movie display modes");
        }
        if (!display_->exclusive)
        {
            return no_cooperative_level;
        }
        if (!w || !h || w > 8192 || h > 8192)
        {
            return invalid_mode;
        }
        auto* target = display_->window();
        const auto emulate = [&]() -> HRESULT
        {
            if (refresh)
            {
                unsupported("IDirectDraw::SetDisplayMode", "explicit refresh in an emulated mode");
            }
            display_->emulated = true;
            display_->width = w;
            display_->height = h;
            video(display_->handle)->set_game_mode(w, h, true);
            return DD_OK;
        };
        if (display_->emulated)
        {
            return emulate();
        }
        int count = 0;
        SDL_DisplayMode** modes =
            SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(target), &count);
        if (!modes)
        {
            return emulate();
        }
        std::unique_ptr<void, decltype(&SDL_free)> memory(modes, SDL_free);
        const SDL_DisplayMode* selected = nullptr;
        for (int i = 0; i < count; ++i)
        {
            if (modes[i]->w == static_cast<int>(w) && modes[i]->h == static_cast<int>(h) &&
                modes[i]->pixel_density == 1.0f &&
                (!refresh || static_cast<DWORD>(modes[i]->refresh_rate + 0.5f) == refresh))
            {
                selected = modes[i];
                break;
            }
        }
        if (!selected)
        {
            return emulate();
        }
        if (!SDL_SetWindowFullscreenMode(target, selected) || !SDL_SyncWindow(target))
        {
            unsupported("IDirectDraw::SetDisplayMode", SDL_GetError());
        }
        int actual_width = 0, actual_height = 0;
        const auto* applied = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(target));
        if (!SDL_GetWindowSizeInPixels(target, &actual_width, &actual_height) ||
            actual_width != static_cast<int>(w) || actual_height != static_cast<int>(h) ||
            !applied || applied->w != static_cast<int>(w) || applied->h != static_cast<int>(h) ||
            !(SDL_GetWindowFlags(target) & SDL_WINDOW_FULLSCREEN))
        {
            if (!SDL_SetWindowFullscreenMode(target, nullptr) || !SDL_SyncWindow(target))
            {
                unsupported("IDirectDraw::SetDisplayMode", SDL_GetError());
            }
            return emulate();
        }
        display_->width = w;
        display_->height = h;
        video(display_->handle)->set_game_mode(w, h, true);
        return DD_OK;
    }
    HRESULT RestoreDisplayMode() override
    {
        display_->restore();
        return DD_OK;
    }
    HRESULT CreateClipper(DWORD flags, IDirectDrawClipper** output, IUnknown* outer) override
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (flags || outer)
        {
            unsupported("IDirectDraw::CreateClipper", "flags or aggregation");
        }
        return boundary("IDirectDraw::CreateClipper",
            [&]() -> HRESULT
            {
                *output = new Clipper;
                return DD_OK;
            });
    }
    template <class Description, class Interface>
    HRESULT create(Description* desc, Interface** output, IUnknown* outer)
    {
        if (!output)
        {
            return pointer_error;
        }
        *output = nullptr;
        if (!desc || desc->dwSize != sizeof(Description))
        {
            return DDERR_INVALIDPARAMS;
        }
        if (outer)
        {
            unsupported("IDirectDraw::CreateSurface", "COM aggregation");
        }
        thread_check(display_->thread, "IDirectDraw::CreateSurface");
        if (desc->dwFlags &
            ~(DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_BACKBUFFERCOUNT))
        {
            unsupported("IDirectDraw::CreateSurface", "surface description flags");
        }
        if (!(desc->dwFlags & DDSD_CAPS))
        {
            return DDERR_INVALIDPARAMS;
        }
        const DWORD caps = desc->ddsCaps.dwCaps;
        if (caps & ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP |
                       DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY))
        {
            unsupported("IDirectDraw::CreateSurface", "surface capabilities");
        }
        if constexpr (sizeof(Description) == sizeof(DDSURFACEDESC2))
        {
            if (desc->ddsCaps.dwCaps2 || desc->ddsCaps.dwCaps3 || desc->ddsCaps.dwCaps4)
            {
                unsupported("IDirectDraw::CreateSurface", "extended capabilities");
            }
        }
        if ((desc->dwFlags & DDSD_PIXELFORMAT) && !is565(desc->ddpfPixelFormat))
        {
            unsupported("IDirectDraw::CreateSurface", "only RGB565 movie surfaces");
        }
        const bool primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
        const bool chain = (caps & DDSCAPS_FLIP) != 0;
        if (chain && (!primary || !(caps & DDSCAPS_COMPLEX) ||
                         !(desc->dwFlags & DDSD_BACKBUFFERCOUNT) || desc->dwBackBufferCount != 1))
        {
            unsupported("IDirectDraw::CreateSurface", "requires one attached back buffer");
        }
        if (!chain && (desc->dwFlags & DDSD_BACKBUFFERCOUNT) && desc->dwBackBufferCount)
        {
            return DDERR_INVALIDPARAMS;
        }
        if (primary && !display_->handle)
        {
            return no_cooperative_level;
        }
        const DWORD width = primary ? display_->width : desc->dwWidth;
        const DWORD height = primary ? display_->height : desc->dwHeight;
        if (!primary && (desc->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT)) != (DDSD_WIDTH | DDSD_HEIGHT))
        {
            return DDERR_INVALIDPARAMS;
        }
        if (!width || !height || width > 8192 || height > 8192)
        {
            return DDERR_INVALIDPARAMS;
        }
        return boundary("IDirectDraw::CreateSurface",
            [&]() -> HRESULT
            {
                auto surface = std::make_unique<Surface>(
                    display_, width, height, caps | (chain ? DDSCAPS_FRONTBUFFER : 0));
                if (chain)
                {
                    surface->attach(new Surface(display_,
                        width,
                        height,
                        DDSCAPS_BACKBUFFER | DDSCAPS_FLIP |
                            (caps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY))));
                }
                *output = static_cast<Interface*>(surface.release());
                return DD_OK;
            });
    }
    HRESULT CreateSurface(
        DDSURFACEDESC* desc, IDirectDrawSurface** output, IUnknown* outer) override
    {
        return create(desc, output, outer);
    }
    HRESULT CreateSurface(
        DDSURFACEDESC2* desc, IDirectDrawSurface4** output, IUnknown* outer) override
    {
        return create(desc, output, outer);
    }

  private:
    ULONG references_ = 1;
    std::shared_ptr<Display> display_;
};

HRESULT create_draw(GUID* device, void** output, REFIID iid, IUnknown* outer, const char* api)
{
    if (output)
    {
        *output = nullptr;
    }
    if (skipped())
    {
        std::fputs("Beach Life boot build: movie skipped (BEACHLIFE_BOOT_SKIP_MOVIE=1)\n", stderr);
        return DDERR_UNSUPPORTED;
    }
    if (!output)
    {
        return pointer_error;
    }
    if (device || outer)
    {
        unsupported(api, "device selection or COM aggregation");
    }
    return boundary(api,
        [&]() -> HRESULT
        {
            auto* draw = new Draw;
            const auto result = draw->QueryInterface(iid, output);
            draw->Release();
            return result;
        });
}
} // namespace
} // namespace beachlife::compat::movie

extern "C" HRESULT WINAPI DirectDrawCreate(GUID* device, IDirectDraw** output, IUnknown* outer)
{
    return beachlife::compat::movie::create_draw(
        device, reinterpret_cast<void**>(output), IID_IDirectDraw, outer, "DirectDrawCreate");
}
extern "C" HRESULT WINAPI DirectDrawCreateEx(
    GUID* device, void** output, REFIID iid, IUnknown* outer)
{
    return beachlife::compat::movie::create_draw(device, output, iid, outer, "DirectDrawCreateEx");
}

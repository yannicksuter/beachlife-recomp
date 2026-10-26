#include "runtime/boot_session.h"
#include "data/content_manifest.h"
#include "platform/compat/compat_files.h"
#include "platform/compat/cursor_provider.h"
#include "platform/compat/kernel_files.h"
#include "platform/compat/user32_cursor.h"
#include "platform/compat/user32_host.h"
#include "runtime/credits_data.h"
#include "runtime/jpeg_export.h"
#include "runtime/resource_files.h"
#include "runtime/retail_cursor.h"
#include "runtime/retail_files.h"
#include "startup_screen_credits_data.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show);

namespace beachlife::runtime
{
// The decompiled game borrows the prepared file services and compat cursor
// provider for its whole lifetime. Destroy cursors before their SDL video and
// file services; restore any outer bindings after the game returns.
int run_boot_game(GameHost& host)
{
    const auto* prepared = dynamic_cast<const data::PreparedSource*>(host.source.get());
    if (!prepared)
    {
        throw std::runtime_error("The boot build needs prepared game files.");
    }
    std::vector<data::SourceEntry> entries(
        prepared->manifest().files.begin(), prepared->manifest().files.end());
    retail_files::Session files(host.source);
    resource_files::Session resources(host.source);
    compat::DirectorySession directories(entries);
    compat::ResourceCursorSession cursor_resources;
    retail_cursor::Session cursors(compat::retail_cursor_provider());
    // Only the credits screen needs these PE records. Missing or unsupported
    // data must not reject unrelated startup; RequireCreditsData() rejects its
    // first actual use when there is no binding. Never publish placeholder rows.
    const auto credits_data = CreditsData::read_if_available(*host.source);
    std::optional<CreditsDataBinding> credits;
    if (credits_data)
    {
        credits.emplace(*credits_data);
    }
    // The retail installer creates an empty Data\Temp folder (listed in
    // data1.hdr); the game keeps its map cache there. Writes go to the
    // per-user folder, so recreate the folder there as the installer did.
    std::filesystem::create_directories(compat::kernel::user_path("data/temp"));
    // Photos (photos\\BeachLife_Snapshot_*.jpg) go through the game's C runtime
    // like every other file it writes, into the player's folder.
    jpeg::set_open_function(
        [](const char* path, const char* mode) -> std::FILE*
        {
            return beachlife_crt_fopen(path, mode);
        });
    compat::WindowSession window(
        reinterpret_cast<HWND>(&host.video), host.prepare_frame, host.context, host.hidden);
    char command[] = "";
    return WinMain(GetModuleHandleA(nullptr), nullptr, command, 1);
}
} // namespace beachlife::runtime

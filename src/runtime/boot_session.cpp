#include "runtime/boot_session.h"
#include "data/content_manifest.h"
#include "platform/compat/compat_files.h"
#include "platform/compat/cursor_provider.h"
#include "platform/compat/kernel_files.h"
#include "platform/compat/user32_cursor.h"
#include "platform/compat/user32_host.h"
#include "runtime/resource_files.h"
#include "runtime/retail_cursor.h"
#include "runtime/retail_files.h"

#include <filesystem>
#include <stdexcept>
#include <vector>

int beachlife_boot_main(beachlife::runtime::GameHost& host);

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
    // The retail installer creates an empty Data\Temp folder (listed in
    // data1.hdr); the game keeps its map cache there. Writes go to the
    // per-user folder, so recreate the folder there as the installer did.
    std::filesystem::create_directories(compat::kernel::user_path("data/temp"));
    // The retail HWND is Play's Video (see winmain_native.cpp).
    compat::bind_window(reinterpret_cast<HWND>(&host.video));
    return beachlife_boot_main(host);
}
} // namespace beachlife::runtime

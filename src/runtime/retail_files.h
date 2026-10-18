#pragma once

#include "data/source.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace beachlife::runtime::retail_files
{

struct File;
using Position = std::uint64_t;

enum class SeekOrigin
{
    begin,
    current,
    end
};

// Binds an explicit VFS on this thread. Nested sessions restore the previous
// binding. The session must be destroyed on its creating thread in stack order.
// Open handles retain the mount and may outlive the binding; close each handle
// exactly once on its creating thread. No process-directory fallback exists.
class Session
{
  public:
    explicit Session(std::shared_ptr<const data::FileSource> source);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

  private:
    friend File* open(const char* filename, const char* mode) noexcept;
    std::shared_ptr<const data::FileSource> source_;
    Session* previous_;
    std::thread::id thread_;
};

// Only the read-only operations needed by the reconstructed helpers are
// provided, with exactly "rb" mode. This is not a complete CRT: no errno,
// buffering or error flags are exposed. VFS failures use null/zero/-1 results;
// a throwing read may have modified its destination before reporting failure.
// Handles and live buffers remain caller-owned.
File* open(const char* filename, const char* mode) noexcept;
std::size_t read(void* buffer, std::size_t size, std::size_t count, File* file) noexcept;
int seek(File* file, std::int32_t offset, SeekOrigin origin) noexcept;
int position(File* file, Position* result) noexcept;
int close(File* file) noexcept;

// VC6's 32-bit long receives the low word of its 64-bit fpos_t. Preserve the
// signed result on LP64 hosts too, including values outside the loader domain.
constexpr std::int32_t low_position(Position value) noexcept
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

// The reconstructed allocation/read helpers intentionally retain their unchecked
// behavior. Qualified callers supply stable files of 1..INT32_MAX-1 bytes and a
// successful second open; BLE decoding additionally requires a complete read.
// An empty or missing file returns null. Returned arrays belong to the caller.

} // namespace beachlife::runtime::retail_files

#pragma once

#include "data/source.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace beachlife::runtime::resource_files
{

struct Stream;
struct SessionState;

// Resource I/O has its own binding. It does not change retail_files::Session
// or the exactly-once close contract used by existing TIFF/file consumers.
// A Session and all its operations belong to one thread. Nested sessions
// restore the previous binding. Destroy game owners before their Session.
class Session
{
  public:
    // Normal resource IDs start at 3, leaving the conventional stdio range
    // reserved. A zero-based fixture/domain is explicit, so ordinary use does
    // not force the game's descriptor-zero destructor branch.
    explicit Session(std::shared_ptr<const data::FileSource> source, int first_descriptor = 3);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

  private:
    std::unique_ptr<SessionState> state_;
};

// Only "rb" is supported. Tokens remain alive until Session teardown, including
// after close. First close releases the real stream and returns 0; another close
// returns -1. This explicit native rule does not emulate CRT FILE slot reuse.
// No token may be used after its owning Session ends.
Stream* open_stream(const char* path, const char* mode) noexcept;
std::size_t read_stream(void* buffer, std::size_t size, std::size_t count, Stream* stream) noexcept;
int close_stream(Stream* stream) noexcept;

// Read-only logical cursor of a live token in the current Session. No I/O,
// allocation or cursor movement. Failure leaves position unchanged, including
// for null, closed, foreign or unbound tokens.
bool stream_position(Stream* stream, std::uint64_t& position) noexcept;

// Descriptor values belong to the current Session and are not reused during
// it. Flags are read-only (0), optionally with the original Windows binary bit
// (0x8000). These are not OS descriptors or replacements for process stdio.
int open_descriptor(const char* path, int flags) noexcept;
int read_descriptor(int descriptor, void* buffer, unsigned int bytes) noexcept;
// Displacement is interpreted as a signed 32-bit word; origin is 0/1/2.
// A successful seek returns the signed interpretation of the low 32 position
// bits, matching the existing native raw-loader bridge. Beyond-EOF seeks retain
// their logical position. Qualification must state its finite-file domain.
int seek_descriptor(int descriptor, unsigned int displacement, int origin) noexcept;
int close_descriptor(int descriptor) noexcept;

} // namespace beachlife::runtime::resource_files

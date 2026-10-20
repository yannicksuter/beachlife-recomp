#include "startup_resource_rasters.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"

#include <array>
#include <bit>
#include <climits>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

using Record = ScreenDialogResourceRecord;
using Frame = ScreenDialogResourceFrame;
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(short) == 2 && sizeof(int) == 4 && sizeof(float) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::is_standard_layout_v<Record> && !std::is_polymorphic_v<Record>);
static_assert(std::is_standard_layout_v<Frame> && !std::is_polymorphic_v<Frame>);
static_assert(!std::is_aggregate_v<Record> && !std::is_trivially_destructible_v<Record>);
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
static_assert(sizeof(Record) == (sizeof(void*) == 8 ? 56 : 36));
static_assert(sizeof(Frame) == (sizeof(void*) == 8 ? 176 : 120));
static_assert(offsetof(Record, unknown_20) == (sizeof(void*) == 8 ? 48 : 32));
static_assert(offsetof(Frame, flag_74) == (sizeof(void*) == 8 ? 168 : 116));

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
using RecordBytes = std::array<unsigned char, sizeof(Record)>;
using FrameBytes = std::array<unsigned char, sizeof(Frame)>;
struct Totals
{
    std::size_t sessions = 0, constructors = 0, destructors = 0, calls = 0;
    std::size_t frames = 0, palettes = 0, palette_words = 0, consumed = 0;
    std::size_t allocations = 0, releases = 0, checks = 0;
    std::size_t loads = 0, raw_bytes = 0, rasters = 0, pixels = 0, images = 0;
} totals;
char label[64];
struct FixtureFailure
{
};
const char* first_error = nullptr;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "asset preview %s: %s\n", label, reason);
    throw FixtureFailure{};
}

void expect(bool condition, const char* reason)
{
    ++totals.checks;
    if (!condition && !first_error)
    {
        first_error = reason;
    }
}

void necessary(bool condition, const char* reason)
{
    if (!condition)
    {
        fail(reason);
    }
}

enum class Phase
{
    outside,
    method,
    destruction
};
Phase phase = Phase::outside;
struct Trace
{
    Phase saved;
    explicit Trace(Phase next)
        : saved(phase)
    {
        phase = next;
    }
    ~Trace()
    {
        phase = saved;
    }
};

enum class Role
{
    record_name,
    palette_table,
    palette,
    frame_table,
    frame,
    frame_name,
    indices,
    offsets,
    data
};
struct Request
{
    Role role;
    std::size_t bytes;
    long position;
};
struct Allocation
{
    unsigned char* raw = nullptr;
    std::uintptr_t address = 0;
    std::size_t bytes = 0, capacity = 0;
    Role role = Role::record_name;
    bool live = false;
};
std::array<Request, 16> requests;
std::size_t request_count = 0, request_next = 0;
std::array<Allocation, 16> allocations;
std::size_t allocation_count = 0;
std::array<std::size_t, 16> release_ids;
std::size_t release_count = 0;
FILE* active_stream = nullptr;
int observed_descriptor = -1;

long raw_seek(int descriptor, long displacement, int origin)
{
#if defined(_WIN32)
    return ::_lseek(descriptor, displacement, origin);
#else
    const off_t position = ::lseek(descriptor, static_cast<off_t>(displacement), origin);
    necessary(position >= -1 && position <= INT_MAX, "raw position outside signed32 domain");
    return static_cast<long>(position);
#endif
}

int raw_close(int descriptor)
{
#if defined(_WIN32)
    return ::_close(descriptor);
#else
    return ::close(descriptor);
#endif
}

int raw_read(int descriptor, unsigned char* buffer, unsigned int bytes)
{
#if defined(_WIN32)
    return ::_read(descriptor, buffer, bytes);
#else
    const ssize_t count = ::read(descriptor, buffer, bytes);
    necessary(count >= -1 && count <= INT_MAX, "raw read outside signed32 domain");
    return static_cast<int>(count);
#endif
}

void allocation_guards(const Allocation& a)
{
    necessary(a.live, "allocation already dead");
    for (std::size_t i = 0; i < guard; ++i)
    {
        expect(a.raw[i] == 0xa5, "allocation prefix changed");
        expect(a.raw[guard + a.capacity + i] == 0x5a, "allocation suffix changed");
    }
    if (a.bytes == 0)
    {
        expect(a.raw[guard] == 0x6b, "zero-size allocation was written");
    }
}

// Fixture-only scalar allocation observation; real malloc/free supply storage.
// No failure injection, file-reader replacement or game implementation is here.
void* allocate(std::size_t bytes)
{
    if (phase == Phase::outside)
    {
        void* value = std::malloc(bytes ? bytes : 1);
        if (!value)
        {
            throw std::bad_alloc();
        }
        return value;
    }
    necessary(phase == Phase::method && (active_stream || observed_descriptor >= 0),
        "unexpected allocation phase");
    necessary(request_next < request_count, "unexpected scalar allocation");
    const Request& r = requests[request_next++];
    necessary(bytes == r.bytes, "scalar allocation size differs");
    if (active_stream)
    {
        expect(std::ftell(active_stream) == r.position, "allocation stream position differs");
    }
    else
    {
        expect(raw_seek(observed_descriptor, 0, SEEK_CUR) == r.position,
            "allocation raw position differs");
        expect(screen_dialog_frame_file == observed_descriptor &&
                   screen_dialog_frame_cursor == static_cast<unsigned int>(r.position),
            "allocation file globals differ");
    }
    necessary(allocation_count < allocations.size(), "allocation ledger full");
    const std::size_t capacity = bytes ? bytes : 1;
    necessary(capacity <= 4096, "allocation outside bounded fixture domain");
    auto* raw = static_cast<unsigned char*>(std::malloc(capacity + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, guard);
    volatile unsigned char* bytes_before_construction = raw + guard;
    for (std::size_t i = 0; i < capacity; ++i)
    {
        bytes_before_construction[i] = 0x6b;
    }
    std::memset(raw + guard + capacity, 0x5a, guard);
    allocations[allocation_count++] = {
        raw, reinterpret_cast<std::uintptr_t>(raw + guard), bytes, capacity, r.role, true};
    ++totals.allocations;
    return raw + guard;
}

void release(void* pointer) noexcept
{
    if (!pointer)
    {
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        Allocation& a = allocations[i];
        if (a.address != address)
        {
            continue;
        }
        necessary(a.live, "duplicate scalar delete");
        allocation_guards(a);
        necessary(phase == Phase::destruction, "parser unexpectedly released an owner");
        necessary(release_count < release_ids.size(), "release ledger full");
        release_ids[release_count++] = i;
        a.live = false;
        std::free(a.raw);
        a.raw = nullptr;
        ++totals.releases;
        return;
    }
    necessary(phase == Phase::outside, "untracked game scalar delete");
    std::free(pointer);
}

void begin(const char* family, std::size_t number)
{
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(!allocations[i].live, "prior owner remained live");
    }
    allocation_count = request_count = request_next = release_count = 0;
    active_stream = nullptr;
    observed_descriptor = -1;
    first_error = nullptr;
    std::snprintf(label, sizeof(label), "%s-%zu", family, number);
    ++totals.sessions;
}

void finish()
{
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        necessary(!allocations[i].live, "owner remained after real Record destruction");
    }
    if (first_error)
    {
        fail(first_error);
    }
}

struct Span
{
    std::size_t offset, size;
};
#define MEMBER(T, name)                                                                            \
    Span                                                                                           \
    {                                                                                              \
        offsetof(T, name), sizeof(T::name)                                                         \
    }
constexpr auto record_members = std::to_array<Span>({MEMBER(Record, name),
    MEMBER(Record, reference_count),
    MEMBER(Record, frame_count),
    MEMBER(Record, frames),
    MEMBER(Record, palette_count),
    MEMBER(Record, palettes),
    MEMBER(Record, format_18),
    MEMBER(Record, word_1c),
    MEMBER(Record, unknown_20)});
constexpr auto frame_members = std::to_array<Span>({MEMBER(Frame, name),
    MEMBER(Frame, time_04),
    MEMBER(Frame, loaded_08),
    MEMBER(Frame, unknown_09),
    MEMBER(Frame, count_0a),
    MEMBER(Frame, plane_count_0c),
    MEMBER(Frame, unknown_0d),
    MEMBER(Frame, width_0e),
    MEMBER(Frame, height_10),
    MEMBER(Frame, unknown_12),
    MEMBER(Frame, word_14),
    MEMBER(Frame, index_ready_16),
    MEMBER(Frame, unknown_17),
    MEMBER(Frame, data_size_18),
    MEMBER(Frame, file_offset_1c),
    MEMBER(Frame, plane_offsets_20),
    MEMBER(Frame, offset_count_40),
    MEMBER(Frame, unknown_42),
    MEMBER(Frame, indices_44),
    MEMBER(Frame, whole_data_48),
    MEMBER(Frame, unknown_49),
    MEMBER(Frame, data_4c),
    MEMBER(Frame, plane_data_50),
    MEMBER(Frame, offsets_70),
    MEMBER(Frame, flag_74)});
#undef MEMBER

template <std::size_t N, std::size_t M>
void same_members(const std::array<unsigned char, N>& actual,
    const std::array<unsigned char, N>& expected,
    const std::array<Span, M>& members)
{
    for (const Span& span : members)
    {
        for (std::size_t i = 0; i < span.size; ++i)
        {
            expect(actual[span.offset + i] == expected[span.offset + i],
                "member representation differs");
        }
    }
}

template <std::size_t N, class T>
void put(std::array<unsigned char, N>& bytes, std::size_t offset, const T& value)
{
    necessary(offset + sizeof(value) <= N, "expected member out of bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

RecordBytes constructed_record(RecordBytes initial)
{
#define ZERO(name) put(initial, offsetof(Record, name), decltype(Record::name){})
    ZERO(name);
    ZERO(reference_count);
    ZERO(frame_count);
    ZERO(frames);
    ZERO(palette_count);
    ZERO(palettes);
    ZERO(word_1c);
#undef ZERO
    put(initial, offsetof(Record, format_18), 1U);
    return initial;
}

FrameBytes constructed_frame()
{
    FrameBytes expected;
    expected.fill(0x6b);
#define ZERO(name) put(expected, offsetof(Frame, name), decltype(Frame::name){})
    ZERO(name);
    ZERO(loaded_08);
    ZERO(count_0a);
    ZERO(plane_count_0c);
    ZERO(width_0e);
    ZERO(height_10);
    ZERO(word_14);
    ZERO(index_ready_16);
    ZERO(data_size_18);
    ZERO(file_offset_1c);
    ZERO(offset_count_40);
    ZERO(indices_44);
    ZERO(whole_data_48);
    ZERO(data_4c);
    ZERO(offsets_70);
    ZERO(flag_74);
#undef ZERO
    put(expected, offsetof(Frame, unknown_12), std::array<unsigned char, 2>{});
    for (std::size_t i = 0; i < 8; ++i)
    {
        put(expected, offsetof(Frame, plane_offsets_20) + i * sizeof(unsigned int), 0U);
        put(expected,
            offsetof(Frame, plane_data_50) + i * sizeof(unsigned char*),
            static_cast<unsigned char*>(nullptr));
    }
    return expected;
}

struct Storage
{
    std::array<unsigned char, guard> before;
    alignas(Record) RecordBytes bytes;
    std::array<unsigned char, guard> after;
    Record* object = nullptr;

    Storage()
    {
        before.fill(0x39);
        after.fill(0x93);
        volatile unsigned char* raw = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            raw[i] = 0x35;
        }
        const RecordBytes expected = constructed_record(bytes);
        {
            Trace trace(Phase::method);
            object = ::new (bytes.data()) Record;
        }
        ++totals.constructors;
        same_members(bytes, expected, record_members);
        object->reference_count = 0x10203040U;
        object->word_1c = 0x11223344U;
        object->unknown_20[0] = 0xde;
        object->unknown_20[1] = 0xad;
        object->unknown_20[2] = 0xbe;
        object->unknown_20[3] = 0xef;
    }
    void destroy()
    {
        Trace trace(Phase::destruction);
        object->~Record();
        object = nullptr;
        ++totals.destructors;
    }
    void guards() const
    {
        for (std::size_t i = 0; i < guard; ++i)
        {
            expect(before[i] == 0x39 && after[i] == 0x93, "record outside guard changed");
        }
    }
};
static_assert(offsetof(Storage, bytes) == guard);
static_assert(offsetof(Storage, after) == guard + sizeof(Record));

struct FramePlan
{
    std::uint32_t begin, end;
    std::vector<unsigned char> name;
    unsigned short count, width, height, opaque, offsets;
    unsigned char planes;
    short word;
    unsigned int position;
    int data_size;
};
struct Plan
{
    unsigned int index, begin, end, format, classification, palette_count, palette_words;
    unsigned int palette_begin;
    std::vector<unsigned char> name;
    std::array<unsigned char, 4> opaque;
    std::array<std::vector<unsigned short>, 2> palette;
    std::vector<FramePlan> frames;
};
struct Oracle
{
    unsigned int metadata_size, bank_size, begin, end, raw_begin, raw_end;
    unsigned int stride, x, y, columns, rows, background;
    Plan record;
    std::vector<unsigned char> metadata, prefix, raw;
    std::array<std::vector<unsigned char>, 2> canvas, ppm;
};

// This is the host sidecar format, not a second implementation of ReadMetadata.
// The sidecar carries independently prepared field, palette and canvas expectations.
struct Cursor
{
    const std::vector<unsigned char>& input;
    std::size_t position = 0;

    unsigned char byte()
    {
        necessary(position < input.size(), "sidecar is truncated");
        return input[position++];
    }
    unsigned short word()
    {
        const unsigned int low = byte();
        return static_cast<unsigned short>(low | (static_cast<unsigned int>(byte()) << 8));
    }
    unsigned int dword()
    {
        const unsigned int low = word();
        return low | (static_cast<unsigned int>(word()) << 16);
    }
    std::vector<unsigned char> bytes(std::size_t count)
    {
        necessary(count <= input.size() - position, "sidecar byte span exceeds input");
        std::vector<unsigned char> result(
            input.begin() + position, input.begin() + position + count);
        position += count;
        return result;
    }
    std::vector<unsigned char> name()
    {
        const unsigned int count = dword();
        necessary(count <= 255, "sidecar name exceeds byte-length domain");
        return bytes(count);
    }
};

std::vector<unsigned char> read_sidecar(const char* path)
{
    FILE* stream = std::fopen(path, "rb");
    necessary(stream != nullptr, "sidecar open failed");
    necessary(std::fseek(stream, 0, SEEK_END) == 0, "sidecar size seek failed");
    const long length = std::ftell(stream);
    necessary(length > 0 && length <= 5 * 1024 * 1024, "sidecar size outside bounded domain");
    necessary(std::fseek(stream, 0, SEEK_SET) == 0, "sidecar rewind failed");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    const bool complete = std::fread(bytes.data(), 1, bytes.size(), stream) == bytes.size();
    const bool closed = std::fclose(stream) == 0;
    necessary(complete && closed, "sidecar full read or close failed");
    return bytes;
}

Oracle read_oracle(const std::vector<unsigned char>& bytes)
{
    Cursor c{bytes};
    constexpr std::array<unsigned char, 8> magic = {'R', '1', '4', '8', 'v', '0', '0', '1'};
    for (unsigned char value : magic)
    {
        necessary(c.byte() == value, "sidecar magic differs");
    }
    Oracle result{};
    result.metadata_size = c.dword();
    result.bank_size = c.dword();
    result.begin = c.dword();
    result.end = c.dword();
    result.raw_begin = c.dword();
    result.raw_end = c.dword();
    result.stride = c.dword();
    result.x = c.dword();
    result.y = c.dword();
    result.columns = c.dword();
    result.rows = c.dword();
    result.background = c.dword();
    necessary(result.metadata_size <= INT_MAX && result.begin < result.end &&
                  result.end <= result.metadata_size && result.bank_size <= INT_MAX &&
                  result.raw_begin < result.raw_end && result.raw_end <= result.bank_size,
        "sidecar file bounds differ");
    necessary(result.stride == 15 && result.columns == 15 && result.rows == 14 && result.x == 2 &&
                  result.y == 1 && result.background == 0x89abcdefU,
        "sidecar canvas outside reviewed domain");
    result.metadata = c.bytes(c.dword());
    result.prefix = c.bytes(c.dword());
    result.raw = c.bytes(c.dword());
    necessary(result.metadata.size() == result.end - result.begin && result.prefix.size() == 24 &&
                  result.raw.size() == 150 && result.raw_end - result.raw_begin == 150,
        "sidecar input span differs");
    const unsigned int record_bytes = c.dword();
    const std::size_t record_start = c.position;
    Plan& p = result.record;
    p.index = c.dword();
    p.begin = c.dword();
    p.end = c.dword();
    p.format = c.dword();
    p.classification = c.dword();
    p.palette_count = c.dword();
    p.palette_words = c.dword();
    const unsigned int frames = c.dword();
    necessary(p.begin == result.begin && p.end == result.end && p.format == 1 &&
                  p.classification == 0 && p.palette_count == 1 && p.palette_words == 256 &&
                  frames == 1,
        "sidecar record outside reviewed one-frame domain");
    p.name = c.name();
    necessary(!p.name.empty() && std::memchr(p.name.data(), 0, p.name.size()) != nullptr,
        "Record name lacks in-allocation terminator");
    for (unsigned char value : p.name)
    {
        necessary(value < 128, "Record name outside C-locale ASCII domain");
    }
    for (unsigned char& value : p.opaque)
    {
        value = c.byte();
    }
    p.palette_begin = c.dword();
    necessary(p.palette_begin == p.begin + 1 + p.name.size() + 8, "palette input boundary differs");
    for (auto& palette : p.palette)
    {
        for (unsigned int i = 0; i < 256; ++i)
        {
            palette.push_back(c.word());
        }
    }
    FramePlan f{};
    f.begin = c.dword();
    f.end = c.dword();
    f.name = c.name();
    const unsigned int count = c.dword(), planes = c.dword(), width = c.dword();
    const unsigned int height = c.dword(), opaque = c.dword();
    const std::int32_t word = std::bit_cast<std::int32_t>(c.dword());
    f.position = c.dword();
    f.data_size = std::bit_cast<std::int32_t>(c.dword());
    const unsigned int offsets = c.dword();
    necessary(count == 1 && planes == 1 && width == 11 && height == 12 && offsets == 1 &&
                  f.data_size == 144 && opaque <= 65535 && word >= -32768 && word <= 32767 &&
                  f.position == result.raw_begin && f.begin == p.palette_begin + 768 + 4 &&
                  f.end == p.end && f.end == f.begin + 22 + f.name.size(),
        "sidecar Frame outside reviewed packet domain");
    f.count = static_cast<unsigned short>(count);
    f.planes = static_cast<unsigned char>(planes);
    f.width = static_cast<unsigned short>(width);
    f.height = static_cast<unsigned short>(height);
    f.opaque = static_cast<unsigned short>(opaque);
    f.word = static_cast<short>(word);
    f.offsets = static_cast<unsigned short>(offsets);
    p.frames.push_back(std::move(f));
    necessary(c.position - record_start == record_bytes, "record sidecar extent differs");
    for (auto& canvas : result.canvas)
    {
        canvas = c.bytes(c.dword());
        necessary(canvas.size() == 840, "expected canvas size differs");
    }
    for (auto& ppm : result.ppm)
    {
        ppm = c.bytes(c.dword());
        necessary(ppm.size() == 409, "expected preview size differs");
    }
    necessary(c.position == bytes.size(), "sidecar trailing bytes");
    return result;
}

struct Input
{
    FILE* stream;
    explicit Input(const char* path)
        : stream(std::fopen(path, "rb"))
    {
        necessary(stream != nullptr, "metadata open failed");
    }
    ~Input()
    {
        if (stream)
        {
            std::fclose(stream);
        }
    }
    void verify(const Oracle& oracle)
    {
        necessary(std::fseek(stream, 0, SEEK_END) == 0 &&
                      std::ftell(stream) == static_cast<long>(oracle.metadata_size),
            "metadata size differs");
        necessary(std::fseek(stream, static_cast<long>(oracle.begin), SEEK_SET) == 0,
            "selected metadata seek failed");
        std::array<unsigned char, 4096> block;
        necessary(oracle.metadata.size() <= block.size(), "selected metadata exceeds buffer");
        necessary(
            std::fread(block.data(), 1, oracle.metadata.size(), stream) == oracle.metadata.size() &&
                std::memcmp(block.data(), oracle.metadata.data(), oracle.metadata.size()) == 0,
            "selected metadata differs from pinned sidecar");
        necessary(std::fseek(stream, static_cast<long>(oracle.begin), SEEK_SET) == 0,
            "selected metadata rewind failed");
    }
    void close()
    {
        FILE* closing = stream;
        stream = nullptr;
        necessary(std::fclose(closing) == 0, "metadata close failed");
    }
};

struct RawInput
{
    int descriptor;
    explicit RawInput(const char* path)
    {
#if defined(_WIN32)
        descriptor = ::_open(path, _O_RDONLY | _O_BINARY);
#else
        descriptor = ::open(path, O_RDONLY);
#endif
        necessary(descriptor >= 0, "raw bank open failed");
        necessary(screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0,
            "file globals already borrowed");
        necessary(raw_seek(descriptor, 0, SEEK_CUR) == 0, "raw initial position differs");
        screen_dialog_frame_file = descriptor;
        screen_dialog_frame_cursor = 0;
    }
    ~RawInput()
    {
        if (descriptor >= 0)
        {
            raw_close(descriptor);
            screen_dialog_frame_file = -1;
            screen_dialog_frame_cursor = 0;
        }
    }
    void verify(const Oracle& oracle)
    {
        const long saved = raw_seek(descriptor, 0, SEEK_CUR);
        necessary(saved >= 0, "raw position unavailable");
        necessary(raw_seek(descriptor, 0, SEEK_END) == static_cast<long>(oracle.bank_size),
            "raw bank size differs");
        std::array<unsigned char, 150> buffer{};
        necessary(raw_seek(descriptor, 0, SEEK_SET) == 0 &&
                      raw_read(descriptor, buffer.data(), 24) == 24 &&
                      std::memcmp(buffer.data(), oracle.prefix.data(), 24) == 0,
            "raw bank prefix differs");
        necessary(raw_seek(descriptor, static_cast<long>(oracle.raw_begin), SEEK_SET) ==
                          static_cast<long>(oracle.raw_begin) &&
                      raw_read(descriptor, buffer.data(), 150) == 150 &&
                      std::memcmp(buffer.data(), oracle.raw.data(), 150) == 0,
            "selected raw span differs");
        necessary(
            raw_seek(descriptor, saved, SEEK_SET) == saved, "raw verification restore failed");
    }
    void close()
    {
        expect(screen_dialog_frame_file == descriptor, "borrowed descriptor publication changed");
        const int closing = descriptor;
        descriptor = -1;
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
        necessary(raw_close(closing) == 0, "raw bank close failed");
    }
};

struct Graph
{
    std::size_t name, palette_table, frame_table;
    std::array<std::size_t, 1> palettes{};
    std::array<std::size_t, 1> frames{}, frame_names{};
};

std::size_t request(Role role, std::size_t bytes, long position)
{
    necessary(request_count < requests.size(), "request protocol capacity");
    const std::size_t id = request_count;
    requests[request_count++] = {role, bytes, position};
    return id;
}

Graph protocol(const Plan& p)
{
    request_count = request_next = 0;
    Graph g{};
    g.name = request(Role::record_name, p.name.size(), p.begin + 1);
    g.palette_table =
        request(Role::palette_table, p.palette_count * sizeof(unsigned short*), p.palette_begin);
    for (unsigned int i = 0; i < p.palette_count; ++i)
    {
        g.palettes[i] = request(Role::palette, 2 * p.palette_words, p.palette_begin + 768 * i);
    }
    g.frame_table = request(Role::frame_table,
        p.frames.size() * sizeof(Frame*),
        p.palette_begin + 768 * p.palette_count + 4);
    for (std::size_t i = 0; i < p.frames.size(); ++i)
    {
        g.frames[i] = request(Role::frame, sizeof(Frame), p.frames[i].begin);
        g.frame_names[i] =
            request(Role::frame_name, p.frames[i].name.size(), p.frames[i].begin + 1);
    }
    return g;
}

void* live(std::size_t id)
{
    necessary(id < allocation_count && allocations[id].live, "expected owner is absent");
    return reinterpret_cast<void*>(allocations[id].address);
}

void check_name(const void* actual, const std::vector<unsigned char>& expected)
{
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        expect(static_cast<const unsigned char*>(actual)[i] == expected[i], "name byte differs");
    }
}

void check_frame(const FramePlan& p, Frame* object, std::size_t id, std::size_t name_id)
{
    necessary(object == live(id), "actual Frame owner differs");
    necessary(object->name == live(name_id), "actual Frame name owner differs");
    FrameBytes expected = constructed_frame();
    put(expected, offsetof(Frame, name), static_cast<const char*>(live(name_id)));
    put(expected, offsetof(Frame, count_0a), p.count);
    put(expected, offsetof(Frame, plane_count_0c), p.planes);
    put(expected, offsetof(Frame, width_0e), p.width);
    put(expected, offsetof(Frame, height_10), p.height);
    put(expected,
        offsetof(Frame, unknown_12),
        std::array<unsigned char, 2>{
            static_cast<unsigned char>(p.opaque), static_cast<unsigned char>(p.opaque >> 8)});
    put(expected, offsetof(Frame, word_14), p.word);
    put(expected, offsetof(Frame, file_offset_1c), p.position);
    put(expected, offsetof(Frame, data_size_18), p.data_size);
    put(expected, offsetof(Frame, offset_count_40), p.offsets);
    FrameBytes actual;
    std::memcpy(actual.data(), object, sizeof(Frame));
    same_members(actual, expected, frame_members);
    check_name(object->name, p.name);
    ++totals.frames;
}

Graph call(Storage& storage, FILE* stream, const Plan& p, unsigned int surface)
{
    Record& record = *storage.object;
    RecordBytes expected = storage.bytes;
    const std::size_t released = totals.releases;
    const Graph graph = protocol(p);
    necessary(std::ftell(stream) == static_cast<long>(p.begin), "metadata initial cursor differs");
    active_stream = stream;
    {
        Trace trace(Phase::method);
        record.ReadMetadata(stream, surface);
    }
    active_stream = nullptr;
    ++totals.calls;
    expect(request_next == request_count && allocation_count == request_count,
        "scalar allocation protocol incomplete");
    expect(totals.releases == released, "parser unexpectedly released an owner");
    expect(std::ftell(stream) == static_cast<long>(p.end), "parser stream position differs");
    expect(
        std::ferror(stream) == 0 && std::feof(stream) == 0, "full parser read set a stream error");
    totals.consumed += p.end - p.begin;
    expect(record.word_1c == p.classification, "record classification differs");
    necessary(record.name == live(graph.name), "record name owner differs");
    necessary(record.palettes == live(graph.palette_table), "palette table owner differs");
    necessary(record.frames == live(graph.frame_table), "frame table owner differs");
    put(expected, offsetof(Record, name), static_cast<const char*>(live(graph.name)));
    put(expected, offsetof(Record, palette_count), static_cast<int>(p.palette_count));
    put(expected,
        offsetof(Record, palettes),
        static_cast<unsigned short**>(live(graph.palette_table)));
    put(expected, offsetof(Record, frame_count), static_cast<int>(p.frames.size()));
    put(expected, offsetof(Record, frames), static_cast<Frame**>(live(graph.frame_table)));
    put(expected, offsetof(Record, format_18), p.format);
    put(expected, offsetof(Record, word_1c), p.classification);
    put(expected, offsetof(Record, unknown_20), p.opaque);
    same_members(storage.bytes, expected, record_members);
    check_name(record.name, p.name);
    for (unsigned int palette = 0; palette < p.palette_count; ++palette)
    {
        necessary(
            record.palettes[palette] == live(graph.palettes[palette]), "palette owner differs");
        for (unsigned int word = 0; word < p.palette_words; ++word)
        {
            expect(record.palettes[palette][word] ==
                       p.palette[surface][palette * p.palette_words + word],
                "palette word differs");
            ++totals.palette_words;
        }
        ++totals.palettes;
    }
    for (std::size_t frame = 0; frame < p.frames.size(); ++frame)
    {
        check_frame(
            p.frames[frame], record.frames[frame], graph.frames[frame], graph.frame_names[frame]);
    }
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        allocation_guards(allocations[i]);
    }
    storage.guards();
    return graph;
}

struct Canvas
{
    std::array<unsigned short, 16> before;
    std::array<unsigned short, 420> pixels;
    std::array<unsigned short, 16> after;

    void initialize(unsigned int background)
    {
        before.fill(0x39a5);
        after.fill(0xa593);
        auto* bytes = reinterpret_cast<unsigned char*>(pixels.data());
        for (std::size_t i = 0; i < sizeof(pixels); i += sizeof(background))
        {
            std::memcpy(bytes + i, &background, sizeof(background));
        }
    }
    void check(const std::vector<unsigned char>& expected) const
    {
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            expect(before[i] == 0x39a5 && after[i] == 0xa593, "destination outside guard changed");
        }
        necessary(expected.size() == sizeof(pixels), "canvas expectation extent differs");
        const auto* bytes = reinterpret_cast<const unsigned char*>(pixels.data());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            expect(bytes[i] == expected[i], "direct canvas differs");
        }
    }
};
static_assert(offsetof(Canvas, pixels) == sizeof(Canvas::before));
static_assert(offsetof(Canvas, after) == sizeof(Canvas::before) + sizeof(Canvas::pixels));

std::array<std::size_t, 3> load_and_draw(Storage& storage,
    const Graph& graph,
    RawInput& raw,
    const Oracle& oracle,
    unsigned int surface,
    Canvas& canvas)
{
    Record& record = *storage.object;
    Frame* frame = record.frames[0];
    necessary(frame == live(graph.frames[0]) && record.palette_count == 1 &&
                  record.frame_count == 1 && record.format_18 == 1,
        "actual record outside selected graph");
    necessary(frame->count_0a == 1 && frame->plane_count_0c == 1 && frame->width_0e == 11 &&
                  frame->height_10 == 12 && frame->data_size_18 == 144 &&
                  frame->offset_count_40 == 1 && frame->file_offset_1c == oracle.raw_begin &&
                  frame->loaded_08 == 0 && frame->indices_44 == nullptr &&
                  frame->offsets_70 == nullptr && frame->data_4c == nullptr,
        "actual Frame outside selected loader domain");
    necessary(request_next == request_count && allocation_count == 6,
        "metadata graph owner count differs");
    const std::array<std::size_t, 3> loaded = {request(Role::indices, 2, oracle.raw_begin),
        request(Role::offsets, 4, oracle.raw_begin + 2),
        request(Role::data, 144, oracle.raw_begin + 6)};
    FrameBytes expected;
    std::memcpy(expected.data(), frame, sizeof(Frame));
    const RecordBytes record_before = storage.bytes;
    observed_descriptor = raw.descriptor;
    {
        Trace trace(Phase::method);
        frame->LoadAllData();
    }
    observed_descriptor = -1;
    ++totals.loads;
    totals.raw_bytes += 150;
    expect(request_next == request_count && allocation_count == 9 && release_count == 0,
        "raw allocation protocol incomplete");
    expect(screen_dialog_frame_file == raw.descriptor &&
               screen_dialog_frame_cursor == oracle.raw_end &&
               raw_seek(raw.descriptor, 0, SEEK_CUR) == static_cast<long>(oracle.raw_end),
        "whole-frame final file position differs");
    necessary(frame->indices_44 == live(loaded[0]) && frame->offsets_70 == live(loaded[1]) &&
                  frame->data_4c == live(loaded[2]),
        "loaded pointer ownership differs");
    put(expected, offsetof(Frame, indices_44), static_cast<unsigned short*>(live(loaded[0])));
    put(expected, offsetof(Frame, offsets_70), static_cast<unsigned int*>(live(loaded[1])));
    put(expected, offsetof(Frame, data_4c), static_cast<unsigned char*>(live(loaded[2])));
    put(expected, offsetof(Frame, whole_data_48), static_cast<unsigned char>(1));
    put(expected, offsetof(Frame, loaded_08), static_cast<unsigned char>(1));
    FrameBytes actual;
    std::memcpy(actual.data(), frame, sizeof(Frame));
    same_members(actual, expected, frame_members);
    same_members(storage.bytes, record_before, record_members);
    expect(std::memcmp(frame->indices_44, oracle.raw.data(), 2) == 0, "index bytes differ");
    expect(std::memcmp(frame->offsets_70, oracle.raw.data() + 2, 4) == 0, "offset bytes differ");
    expect(std::memcmp(frame->data_4c, oracle.raw.data() + 6, 144) == 0, "packet bytes differ");
    // Explicit fixture selection of the independently audited index/offset tables.
    // This is not a substitute implementation or invocation of SelectData.
    necessary(frame->indices_44[0] == 0 && frame->offsets_70[0] == 0,
        "selected index or stream offset differs");
    const unsigned char* selected = frame->data_4c + frame->offsets_70[frame->indices_44[0]];
    std::array<std::array<unsigned char, 4096>, 9> inputs;
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        allocation_guards(allocations[i]);
        std::memcpy(inputs[i].data(), live(i), allocations[i].bytes);
    }
    const RecordBytes before_raster = storage.bytes;
    canvas.initialize(oracle.background);
    {
        Trace trace(Phase::method);
        ResourceRaster1Direct(canvas.pixels.data(),
            selected,
            record.palettes[0],
            oracle.stride,
            static_cast<int>(oracle.x),
            static_cast<int>(oracle.y),
            frame->width_0e,
            frame->height_10,
            0,
            0,
            frame->width_0e,
            frame->height_10);
    }
    ++totals.rasters;
    totals.pixels += 132;
    canvas.check(oracle.canvas[surface]);
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        allocation_guards(allocations[i]);
        expect(std::memcmp(live(i), inputs[i].data(), allocations[i].bytes) == 0,
            "raster changed a graph or input allocation");
    }
    same_members(storage.bytes, before_raster, record_members);
    storage.guards();
    expect(screen_dialog_frame_file == raw.descriptor &&
               screen_dialog_frame_cursor == oracle.raw_end &&
               raw_seek(raw.descriptor, 0, SEEK_CUR) == static_cast<long>(oracle.raw_end),
        "raster changed file state");
    return loaded;
}

void write_preview(
    const char* prefix, const Canvas& canvas, const Oracle& oracle, unsigned int surface)
{
    // Host-only P6 conversion of the verified crop; no game renderer replacement.
    const FramePlan& f = oracle.record.frames[0];
    std::array<unsigned char, 4096> output;
    const int header = std::snprintf(reinterpret_cast<char*>(output.data()),
        output.size(),
        "P6\n%u %u\n255\n",
        static_cast<unsigned int>(f.width),
        static_cast<unsigned int>(f.height));
    necessary(
        header > 0 && static_cast<std::size_t>(header) < output.size(), "preview header overflow");
    std::size_t count = static_cast<std::size_t>(header);
    const auto* bytes = reinterpret_cast<const unsigned char*>(canvas.pixels.data());
    for (unsigned int row = 0; row < f.height; ++row)
    {
        for (unsigned int column = 0; column < f.width; ++column)
        {
            unsigned int pixel;
            const std::size_t offset = ((oracle.y + row) * oracle.stride + oracle.x + column) * 4;
            necessary(offset + sizeof(pixel) <= sizeof(canvas.pixels) && count + 3 <= output.size(),
                "preview pixel extent differs");
            std::memcpy(&pixel, bytes + offset, sizeof(pixel));
            const unsigned int red = (pixel >> (surface == 0 ? 11 : 10)) & 31;
            const unsigned int green = (pixel >> 5) & (surface == 0 ? 63U : 31U);
            output[count++] = static_cast<unsigned char>(red * 255 / 31);
            output[count++] = static_cast<unsigned char>(green * 255 / (surface == 0 ? 63 : 31));
            output[count++] = static_cast<unsigned char>((pixel & 31) * 255 / 31);
        }
    }
    necessary(count == oracle.ppm[surface].size() &&
                  std::memcmp(output.data(), oracle.ppm[surface].data(), count) == 0,
        "preview RGB conversion differs");
    std::array<char, 1024> path;
    const int length =
        std::snprintf(path.data(), path.size(), "%s-%s.ppm", prefix, surface == 0 ? "565" : "555");
    necessary(
        length > 0 && static_cast<std::size_t>(length) < path.size(), "preview path too long");
    FILE* image = std::fopen(path.data(), "wbx");
    necessary(image != nullptr, "preview output must be a new writable file");
    const bool written = std::fwrite(output.data(), 1, count, image) == count;
    const bool closed = std::fclose(image) == 0;
    necessary(written && closed, "preview write or close failed");
    ++totals.images;
}

void destroy(
    Storage& storage, const Plan& p, const Graph& graph, const std::array<std::size_t, 3>& loaded)
{
    std::array<std::size_t, 16> order{};
    std::size_t count = 0;
    order[count++] = graph.name;
    for (unsigned int i = 0; i < p.palette_count; ++i)
    {
        order[count++] = graph.palettes[i];
    }
    order[count++] = graph.palette_table;
    for (std::size_t i = 0; i < p.frames.size(); ++i)
    {
        order[count++] = graph.frame_names[i];
        order[count++] = loaded[2]; // Real Frame destructor: data, offsets, indices.
        order[count++] = loaded[1];
        order[count++] = loaded[0];
        order[count++] = graph.frames[i];
    }
    order[count++] = graph.frame_table;
    const RecordBytes before = storage.bytes;
    storage.destroy();
    expect(release_count == count, "Record destructor release count differs");
    for (std::size_t i = 0; i < count && i < release_count; ++i)
    {
        expect(release_ids[i] == order[i], "Record destructor release order differs");
    }
    same_members(storage.bytes, before, record_members); // Retained raw storage only.
    storage.guards();
}
} // namespace

void* operator new(std::size_t bytes)
{
    return allocate(bytes);
}
void operator delete(void* pointer) noexcept
{
    release(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    release(pointer);
}

int main(int argc, char** argv)
{
    try
    {
        necessary(argc == 4 || argc == 5,
            "usage: asset-preview METADATA RAWBANK SIDECAR [OUTPUT_PREFIX]");
        necessary(std::setlocale(LC_ALL, "C") != nullptr, "C locale unavailable");
        necessary(screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0,
            "initial genuine file globals differ");
        const auto sidecar = read_sidecar(argv[3]);
        const Oracle oracle = read_oracle(sidecar);
        Input metadata(argv[1]);
        metadata.verify(oracle);
        std::array<Canvas, 2> canvases;
        for (unsigned int surface = 0; surface < 2; ++surface)
        {
            begin(surface == 0 ? "565" : "555", oracle.record.index);
            metadata.verify(oracle);
            RawInput raw(argv[2]);
            raw.verify(oracle);
            Storage storage;
            const Graph graph = call(storage, metadata.stream, oracle.record, surface);
            const auto loaded =
                load_and_draw(storage, graph, raw, oracle, surface, canvases[surface]);
            destroy(storage, oracle.record, graph, loaded);
            raw.verify(oracle);
            raw.close();
            metadata.verify(oracle);
            finish();
        }
        metadata.close();
        necessary(totals.sessions == 2 && totals.constructors == 2 && totals.destructors == 2 &&
                      totals.calls == 2 && totals.frames == 2 && totals.palettes == 2 &&
                      totals.palette_words == 512 && totals.consumed == 1674 && totals.loads == 2 &&
                      totals.raw_bytes == 300 && totals.rasters == 2 && totals.pixels == 264 &&
                      totals.allocations == 18 && totals.releases == 18 &&
                      screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0,
            "asset preview protocol differs");
        if (argc == 5)
        {
            for (unsigned int surface = 0; surface < 2; ++surface)
            {
                write_preview(argv[4], canvases[surface], oracle, surface);
            }
        }
        std::printf("{\"passes\":2,\"sessions\":%zu,\"constructors\":%zu,\"destructors\":%zu,"
                    "\"parser_calls\":%zu,\"frames\":%zu,\"palettes\":%zu,\"palette_words\":%zu,"
                    "\"metadata_bytes\":%zu,\"load_calls\":%zu,\"raw_bytes\":%zu,"
                    "\"raster_calls\":%zu,\"pixels\":%zu,\"allocations\":%zu,\"releases\":%zu,"
                    "\"images\":%zu,\"checks\":%zu}\n",
            totals.sessions,
            totals.constructors,
            totals.destructors,
            totals.calls,
            totals.frames,
            totals.palettes,
            totals.palette_words,
            totals.consumed,
            totals.loads,
            totals.raw_bytes,
            totals.rasters,
            totals.pixels,
            totals.allocations,
            totals.releases,
            totals.images,
            totals.checks);
        return 0;
    }
    catch (const FixtureFailure&)
    {
        return 2;
    }
}

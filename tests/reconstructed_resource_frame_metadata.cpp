#include "startup_resource_binary_read.h"
#include "startup_screen_dialog_resource.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

using Frame = ScreenDialogResourceFrame;
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(short) == 2 && sizeof(int) == 4 && sizeof(float) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::is_standard_layout_v<Frame> && !std::is_polymorphic_v<Frame>);
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
static_assert(sizeof(Frame) == (sizeof(void*) == 8 ? 176 : 120));
static_assert(offsetof(Frame, flag_74) == (sizeof(void*) == 8 ? 168 : 116));

namespace
{
constexpr std::size_t guard = alignof(std::max_align_t);
using Bytes = std::array<unsigned char, sizeof(Frame)>;
struct Totals
{
    std::size_t sessions = 0, constructors = 0, destructors = 0;
    std::size_t metadata = 0, completed_metadata = 0, direct_reads = 0, consumed = 0;
    std::size_t allocations = 0, releases = 0, attempts = 0, exceptions = 0, checks = 0;
} totals;
char label[80];
const char* first_error = nullptr;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "resource frame %s: %s\n", label, reason);
    std::exit(2);
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
    setup,
    method,
    destruction,
    cleanup
};
Phase phase = Phase::outside;
struct Trace
{
    Phase saved;
    explicit Trace(Phase value)
        : saved(phase)
    {
        phase = value;
    }
    ~Trace()
    {
        phase = saved;
    }
};

struct Allocation
{
    unsigned char* raw = nullptr;
    std::uintptr_t address = 0;
    std::size_t bytes = 0;
    std::size_t capacity = 0;
    bool live = false;
};
std::array<Allocation, 32> allocations;
std::size_t allocation_count = 0;
std::array<std::size_t, 32> release_ids;
std::size_t release_count = 0;
bool fail_next = false;
Frame* observed_frame = nullptr;
FILE* observed_stream = nullptr;
Bytes allocation_state;
long allocation_position = -1;

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

// Test-only scalar allocation boundary: real malloc/free, no game replacement.
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
    ++totals.attempts;
    if (phase == Phase::method && observed_frame)
    {
        std::memcpy(allocation_state.data(), observed_frame, sizeof(Frame));
        allocation_position = std::ftell(observed_stream);
    }
    if (fail_next && phase == Phase::method)
    {
        fail_next = false;
        throw std::bad_alloc();
    }
    necessary(allocation_count < allocations.size(), "allocation ledger full");
    const std::size_t capacity = bytes ? bytes : 1;
    if (capacity > std::numeric_limits<std::size_t>::max() - 2 * guard)
    {
        throw std::bad_alloc();
    }
    auto* raw = static_cast<unsigned char*>(std::malloc(capacity + 2 * guard));
    if (!raw)
    {
        throw std::bad_alloc();
    }
    std::memset(raw, 0xa5, guard);
    std::memset(raw + guard, 0x6b, capacity);
    std::memset(raw + guard + capacity, 0x5a, guard);
    allocations[allocation_count++] = {
        raw, reinterpret_cast<std::uintptr_t>(raw + guard), bytes, capacity, true};
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
        if (phase == Phase::destruction)
        {
            necessary(release_count < release_ids.size(), "release ledger full");
            release_ids[release_count++] = i;
        }
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
        necessary(!allocations[i].live, "prior allocation remained live");
    }
    allocation_count = release_count = 0;
    first_error = nullptr;
    observed_frame = nullptr;
    observed_stream = nullptr;
    fail_next = false;
    std::snprintf(label, sizeof(label), "%s-%zu", family, number);
    ++totals.sessions;
}

void cleanup()
{
    Trace trace(Phase::cleanup);
    for (std::size_t i = 0; i < allocation_count; ++i)
    {
        if (allocations[i].live)
        {
            ::operator delete(reinterpret_cast<void*>(allocations[i].address));
        }
    }
}

void finish()
{
    cleanup();
    if (first_error)
    {
        fail(first_error);
    }
    allocation_count = release_count = 0;
}

struct Span
{
    std::size_t offset, size;
};
#define MEMBER(name)                                                                               \
    Span                                                                                           \
    {                                                                                              \
        offsetof(Frame, name), sizeof(Frame::name)                                                 \
    }
constexpr auto members = std::to_array<Span>({MEMBER(name),
    MEMBER(time_04),
    MEMBER(loaded_08),
    MEMBER(unknown_09),
    MEMBER(count_0a),
    MEMBER(plane_count_0c),
    MEMBER(unknown_0d),
    MEMBER(width_0e),
    MEMBER(height_10),
    MEMBER(unknown_12),
    MEMBER(word_14),
    MEMBER(index_ready_16),
    MEMBER(unknown_17),
    MEMBER(data_size_18),
    MEMBER(file_offset_1c),
    MEMBER(plane_offsets_20),
    MEMBER(offset_count_40),
    MEMBER(unknown_42),
    MEMBER(indices_44),
    MEMBER(whole_data_48),
    MEMBER(unknown_49),
    MEMBER(data_4c),
    MEMBER(plane_data_50),
    MEMBER(offsets_70),
    MEMBER(flag_74)});
#undef MEMBER

void same_members(const Bytes& actual, const Bytes& expected)
{
    for (const Span& span : members)
    {
        for (std::size_t n = 0; n < span.size; ++n)
        {
            expect(actual[span.offset + n] == expected[span.offset + n],
                "member representation differs");
        }
    }
}

template <class T> void put(Bytes& expected, std::size_t offset, const T& value)
{
    necessary(offset + sizeof(value) <= expected.size(), "expected member out of bounds");
    std::memcpy(expected.data() + offset, &value, sizeof(value));
}

struct Storage
{
    std::array<unsigned char, guard> before;
    alignas(Frame) Bytes bytes;
    std::array<unsigned char, guard> after;
    Frame* object = nullptr;

    explicit Storage(unsigned char pattern)
    {
        before.fill(0x39);
        after.fill(0x93);
        volatile unsigned char* raw = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            raw[i] = pattern;
        }
    }
    void construct()
    {
        Trace trace(Phase::method);
        object = ::new (bytes.data()) Frame;
        ++totals.constructors;
    }
    void destroy()
    {
        Trace trace(Phase::destruction);
        object->~Frame();
        object = nullptr;
        ++totals.destructors;
    }
    void guards() const
    {
        for (std::size_t i = 0; i < guard; ++i)
        {
            expect(before[i] == 0x39 && after[i] == 0x93, "frame outside guard changed");
        }
    }
};
static_assert(offsetof(Storage, bytes) == guard);
static_assert(offsetof(Storage, after) == guard + sizeof(Frame));

Bytes constructed(Bytes initial)
{
#define ZERO(name) put(initial, offsetof(Frame, name), decltype(Frame::name){})
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
    const std::array<unsigned char, 2> zero{};
    put(initial, offsetof(Frame, unknown_12), zero);
    for (std::size_t i = 0; i < 8; ++i)
    {
        put(initial, offsetof(Frame, plane_offsets_20) + i * sizeof(unsigned int), 0U);
        put(initial,
            offsetof(Frame, plane_data_50) + i * sizeof(unsigned char*),
            static_cast<unsigned char*>(nullptr));
    }
    return initial;
}

void initialize(Frame& f)
{
    f.time_04 = 17.25F;
    f.loaded_08 = 0xa1;
    f.unknown_09 = 0x39;
    f.count_0a = 9;
    f.plane_count_0c = 7;
    f.unknown_0d = 0x5b;
    f.width_0e = 11;
    f.height_10 = 13;
    f.unknown_12[0] = 0x76;
    f.unknown_12[1] = 0x2e;
    f.word_14 = -17;
    f.index_ready_16 = 0x81;
    f.unknown_17 = 0xce;
    f.data_size_18 = 42;
    f.file_offset_1c = 0x12345678U;
    for (unsigned int i = 0; i < 8; ++i)
    {
        f.plane_offsets_20[i] = 0x1234U + i;
    }
    f.offset_count_40 = 23;
    f.unknown_42[0] = 0xab;
    f.unknown_42[1] = 0x54;
    f.whole_data_48 = 0x92;
    f.unknown_49[0] = 0xe7;
    f.unknown_49[1] = 0x31;
    f.unknown_49[2] = 0x6a;
    f.flag_74 = 0xe3;
}

void owners(Frame& f, unsigned int mask)
{
    Trace trace(Phase::setup);
    for (unsigned int slot = 0; slot < 12; ++slot)
    {
        if (!(mask & (1U << slot)))
        {
            continue;
        }
        void* value = ::operator new(slot == 0 ? 5 : 16);
        switch (slot)
        {
        case 0:
            f.name = static_cast<char*>(value);
            break;
        case 1:
            f.data_4c = static_cast<unsigned char*>(value);
            break;
        case 2:
            f.offsets_70 = static_cast<unsigned int*>(value);
            break;
        case 3:
            f.indices_44 = static_cast<unsigned short*>(value);
            break;
        default:
            f.plane_data_50[slot - 4] = static_cast<unsigned char*>(value);
            break;
        }
    }
}

void release_order(std::size_t count, std::size_t first = 0)
{
    expect(release_count == count, "destructor release count");
    for (std::size_t i = 0; i < count && i < release_count; ++i)
    {
        expect(release_ids[i] == first + i, "destructor release order");
    }
}

struct File
{
    FILE* stream = nullptr;
    std::array<unsigned char, 512> input{};
    std::size_t size = 0;

    void append_hex(const char* hex)
    {
        auto digit = [](char c) -> unsigned int
        {
            return c <= '9' ? static_cast<unsigned int>(c - '0')
                            : static_cast<unsigned int>(c - 'a' + 10);
        };
        while (*hex)
        {
            necessary(hex[1] && size < input.size(), "invalid literal bytes");
            input[size++] = static_cast<unsigned char>((digit(hex[0]) << 4) | digit(hex[1]));
            hex += 2;
        }
    }
    void open(long position)
    {
        stream = std::tmpfile();
        necessary(stream != nullptr, "real tmpfile failed");
        necessary(
            std::fwrite(input.data(), 1, size, stream) == size, "generated file write failed");
        necessary(std::fseek(stream, position, SEEK_SET) == 0, "generated file seek failed");
    }
    void verify_and_close()
    {
        expect(
            std::ferror(stream) == 0 && std::feof(stream) == 0, "complete read set a stream error");
        necessary(std::fseek(stream, 0, SEEK_SET) == 0, "verification seek failed");
        std::array<unsigned char, 512> actual{};
        expect(std::fread(actual.data(), 1, size, stream) == size, "file length changed");
        expect(std::memcmp(actual.data(), input.data(), size) == 0, "file bytes changed");
        expect(std::fclose(stream) == 0, "caller stream close failed");
        stream = nullptr;
    }
};

unsigned int read(File& file, unsigned int width)
{
    ++totals.direct_reads;
    if (width == 1)
    {
        return ReadResourceByte(file.stream);
    }
    if (width == 2)
    {
        return ReadResourceWord(file.stream);
    }
    return ReadResourceDword(file.stream);
}

// Literal tables below are copied from the accepted D257 and D255 ledgers.
struct ReadPlan
{
    unsigned int width;
    const char* file;
    long begin, end;
    unsigned int value;
};
constexpr std::array<ReadPlan, 24> read_plans = {{
    {1, "008100ff2a", 0, 1, 0U},
    {1, "51018100ff2a", 1, 2, 1U},
    {1, "51a7007f8100ff2a", 3, 4, 127U},
    {1, "51a700fe32808100ff2a", 5, 6, 128U},
    {1, "ff8100ff2a", 0, 1, 255U},
    {1, "512c8100ff2a", 1, 2, 44U},
    {1, "51a7000a8100ff2a", 3, 4, 10U},
    {1, "51a700fe32a58100ff2a", 5, 6, 165U},
    {2, "00008100ff2a", 0, 2, 0U},
    {2, "5101008100ff2a", 1, 3, 1U},
    {2, "51a700ff008100ff2a", 3, 5, 255U},
    {2, "51a700fe3200ff8100ff2a", 5, 7, 65280U},
    {2, "ff7f8100ff2a", 0, 2, 32767U},
    {2, "5100808100ff2a", 1, 3, 32768U},
    {2, "51a700ffff8100ff2a", 3, 5, 65535U},
    {2, "51a700fe3234128100ff2a", 5, 7, 4660U},
    {4, "000000008100ff2a", 0, 4, 0U},
    {4, "51010000008100ff2a", 1, 5, 1U},
    {4, "51a700ffff00008100ff2a", 3, 7, 65535U},
    {4, "51a700fe32000001008100ff2a", 5, 9, 65536U},
    {4, "ffffff7f8100ff2a", 0, 4, 2147483647U},
    {4, "51000000808100ff2a", 1, 5, 2147483648U},
    {4, "51a700ffffffff8100ff2a", 3, 7, 4294967295U},
    {4, "51a700fe32785634128100ff2a", 5, 9, 305419896U},
}};
struct Sequence
{
    const char* file;
    long begin;
    std::size_t count;
    std::array<unsigned int, 4> widths, values;
    std::array<long, 4> positions;
};
constexpr std::array<Sequence, 4> sequences = {{
    {"51a700ff341278563412000081ff2a",
        3,
        4,
        {1, 2, 4, 1},
        {255U, 4660U, 305419896U, 0U},
        {4, 6, 10, 11}},
    {"510080ff8100ff2a", 1, 3, {1, 1, 1, 0}, {0U, 128U, 255U, 0U}, {2, 3, 4, 0}},
    {"51a70000000080ffff8100ff2a", 3, 3, {2, 2, 2, 0}, {0U, 32768U, 65535U, 0U}, {5, 7, 9, 0}},
    {"51a700fe320000000000000080ffffffff8100ff2a",
        5,
        3,
        {4, 4, 4, 0},
        {0U, 2147483648U, 4294967295U, 0U},
        {9, 13, 17, 0}},
}};
struct Record
{
    const char* prefix;
    const char* encoded;
    const char* name;
    long begin, end;
    unsigned short count;
    unsigned char planes;
    unsigned short width, height, unknown;
    short word;
    unsigned int offset;
    int size;
    unsigned short offsets;
};
constexpr std::array<Record, 8> records = {{
    {"",
        "000200024001c8003412fdff4030201000f401000400",
        "",
        0,
        22,
        2,
        2,
        320,
        200,
        4660,
        -3,
        270544960U,
        128000,
        4},
    {"51",
        "01000200024001c8003412fdff4030201000f401000400",
        "00",
        1,
        24,
        2,
        2,
        320,
        200,
        4660,
        -3,
        270544960U,
        128000,
        4},
    {"51a7",
        "034142000200024001c8003412fdff4030201000f401000400",
        "414200",
        2,
        27,
        2,
        2,
        320,
        200,
        4660,
        -3,
        270544960U,
        128000,
        4},
    {"51a700",
        "0480ff007f0200024001c8003412fdff4030201000f401000400",
        "80ff007f",
        3,
        29,
        2,
        2,
        320,
        200,
        4660,
        -3,
        270544960U,
        128000,
        4},
    {"51a700fe",
        "0541004243000200024001c8003412fdff4030201000f401000400",
        "4100424300",
        4,
        31,
        2,
        2,
        320,
        200,
        4660,
        -3,
        270544960U,
        128000,
        4},
    {"51a700fe32",
        "034142430200024001c8003412fdff4030201000f401000400",
        "414243",
        5,
        30,
        2,
        2,
        320,
        200,
        4660,
        -3,
        270544960U,
        128000,
        4},
    {"",
        "ff000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b"
        "2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152535455565758"
        "595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485"
        "868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2"
        "b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
        "e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfe0200024001c8003412fdff403020"
        "1000f401000400",
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c"
        "2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f50515253545556575859"
        "5a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f80818283848586"
        "8788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3"
        "b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0"
        "e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfe",
        0,
        277,
        2,
        2,
        320,
        200,
        4660,
        -3,
        270544960U,
        128000,
        4},
    {"51",
        "025a00ffffffffffffffcdab0080ffffffff00000080ffff",
        "5a00",
        1,
        25,
        65535,
        255,
        65535,
        65535,
        43981,
        -32768,
        4294967295U,
        -2147483648,
        65535},
}};

void readers()
{
    for (std::size_t i = 0; i < read_plans.size(); ++i)
    {
        const auto& p = read_plans[i];
        begin(p.width == 1 ? "read-byte" : p.width == 2 ? "read-word" : "read-dword", i % 8);
        File file;
        file.append_hex(p.file);
        file.open(p.begin);
        const unsigned int value = read(file, p.width);
        expect(value == p.value, "reader result");
        expect(std::ftell(file.stream) == p.end, "reader cursor");
        totals.consumed += static_cast<std::size_t>(std::ftell(file.stream) - p.begin);
        file.verify_and_close();
        finish();
    }
    for (std::size_t i = 0; i < sequences.size(); ++i)
    {
        begin("reader-sequence", i);
        const auto& p = sequences[i];
        File file;
        file.append_hex(p.file);
        file.open(p.begin);
        for (std::size_t n = 0; n < p.count; ++n)
        {
            expect(read(file, p.widths[n]) == p.values[n], "sequence result");
            expect(std::ftell(file.stream) == p.positions[n], "sequence cursor");
        }
        totals.consumed += static_cast<std::size_t>(std::ftell(file.stream) - p.begin);
        file.verify_and_close();
        finish();
    }
}

void lifetime()
{
    constexpr std::array<unsigned char, 4> patterns{0, 0x35, 0xa5, 0xff};
    for (std::size_t i = 0; i < patterns.size(); ++i)
    {
        begin("constructor", i);
        Storage storage(patterns[i]);
        const Bytes expected = constructed(storage.bytes);
        storage.construct();
        expect(storage.object->flag_74 == 0, "constructor flag74");
        same_members(storage.bytes, expected);
        storage.destroy();
        same_members(storage.bytes, expected);
        release_order(0);
        storage.guards();
        finish();
    }
    constexpr std::array<unsigned int, 16> masks{
        2048, 0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4095, 2725, 4080};
    for (std::size_t i = 0; i < masks.size(); ++i)
    {
        begin("destructor", i);
        Storage storage(0xa5);
        storage.construct();
        initialize(*storage.object);
        owners(*storage.object, masks[i]);
        const Bytes expected = storage.bytes;
        const std::size_t expected_releases = allocation_count;
        storage.destroy();
        release_order(expected_releases);
        same_members(storage.bytes, expected);
        storage.guards();
        finish();
    }
}

Bytes metadata_expected(Bytes expected, const Record& p, const char* name)
{
#define SET(member, value) put(expected, offsetof(Frame, member), value)
    SET(name, name);
    SET(count_0a, p.count);
    SET(plane_count_0c, p.planes);
    SET(width_0e, p.width);
    SET(height_10, p.height);
    SET(unknown_12, p.unknown);
    SET(word_14, p.word);
    SET(file_offset_1c, p.offset);
    SET(data_size_18, p.size);
    SET(offset_count_40, p.offsets);
#undef SET
    return expected;
}

void metadata_call(Storage& storage, File& file, const Record& p)
{
    const Bytes before = storage.bytes;
    const std::size_t id = allocation_count;
    const std::size_t releases_before = totals.releases;
    const long start = std::ftell(file.stream);
    observed_frame = storage.object;
    observed_stream = file.stream;
    {
        Trace trace(Phase::method);
        ++totals.metadata;
        storage.object->ReadMetadata(file.stream);
        ++totals.completed_metadata;
    }
    observed_frame = nullptr;
    observed_stream = nullptr;
    necessary(allocation_count == id + 1, "metadata allocation count");
    Allocation& name = allocations[id];
    expect(name.bytes == std::strlen(p.name) / 2, "name allocation size");
    expect(allocation_position == start + 1, "allocation cursor boundary");
    same_members(allocation_state, before);
    File name_literal;
    name_literal.append_hex(p.name);
    expect(name.bytes == name_literal.size, "literal name size");
    if (name.bytes == name_literal.size)
    {
        expect(std::memcmp(name.raw + guard, name_literal.input.data(), name.bytes) == 0,
            "name bytes");
    }
    expect(storage.object->offset_count_40 == p.offsets, "offset-count mismatch");
    same_members(
        storage.bytes, metadata_expected(before, p, reinterpret_cast<const char*>(name.address)));
    expect(totals.releases == releases_before, "metadata released an old owner");
    expect(std::ftell(file.stream) == start + p.end - p.begin, "metadata cursor");
    totals.consumed += static_cast<std::size_t>(std::ftell(file.stream) - start);
    allocation_guards(name);
    storage.guards();
}

void metadata()
{
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        begin("metadata", i);
        const Record& p = records[i];
        File file;
        file.append_hex(p.prefix);
        file.append_hex(p.encoded);
        file.append_hex("8100ff2a");
        file.open(p.begin);
        Storage storage(0x35);
        storage.construct();
        initialize(*storage.object);
        if (i == 3)
        {
            owners(*storage.object, 4094);
        }
        const std::size_t prior_owners = allocation_count;
        metadata_call(storage, file, p);
        const Bytes expected = storage.bytes;
        storage.destroy();
        expect(release_count == prior_owners + 1, "metadata destructor release count");
        if (release_count)
        {
            expect(release_ids[0] == prior_owners, "name was not released first");
            for (std::size_t n = 1; n < release_count; ++n)
            {
                expect(release_ids[n] == n - 1, "retained owner release order");
            }
        }
        same_members(storage.bytes, expected);
        storage.guards();
        file.verify_and_close();
        finish();
    }
    begin("replacement", 0);
    File file;
    file.append_hex(records[2].encoded);
    file.append_hex(records[4].encoded);
    file.append_hex("8100ff2a");
    file.open(0);
    Storage storage(0xa5);
    storage.construct();
    initialize(*storage.object);
    metadata_call(storage, file, records[2]);
    metadata_call(storage, file, records[4]);
    expect(allocations[0].live && allocations[1].live, "replacement repaired the original leak");
    const Bytes expected = storage.bytes;
    storage.destroy();
    release_order(1, 1);
    expect(allocations[0].live && !allocations[1].live, "replacement ownership after destruction");
    same_members(storage.bytes, expected);
    storage.guards();
    file.verify_and_close();
    finish(); // Real scalar release of the orphan, using the saved allocation receipt.

    begin("allocation-failure", 0);
    File failed;
    failed.append_hex(records[2].encoded);
    failed.append_hex("8100ff2a");
    failed.open(0);
    Storage failure_storage(0x35);
    failure_storage.construct();
    initialize(*failure_storage.object);
    owners(*failure_storage.object, 1);
    const Bytes before = failure_storage.bytes;
    observed_frame = failure_storage.object;
    observed_stream = failed.stream;
    fail_next = true;
    bool caught = false;
    try
    {
        Trace trace(Phase::method);
        ++totals.metadata;
        failure_storage.object->ReadMetadata(failed.stream);
    }
    catch (const std::bad_alloc&)
    {
        caught = true;
        ++totals.exceptions;
    }
    observed_frame = nullptr;
    observed_stream = nullptr;
    expect(caught && !fail_next, "actual bad_alloc did not escape");
    expect(allocation_count == 1 && allocations[0].live, "failure changed prior name ownership");
    expect(std::ftell(failed.stream) == 1 && allocation_position == 1, "failure cursor");
    totals.consumed += static_cast<std::size_t>(std::ftell(failed.stream));
    same_members(allocation_state, before);
    same_members(failure_storage.bytes, before);
    failure_storage.destroy();
    release_order(1);
    same_members(failure_storage.bytes, before);
    failure_storage.guards();
    failed.verify_and_close();
    finish();
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

int main()
{
    readers();
    lifetime();
    metadata();
    necessary(totals.sessions == 58 && totals.constructors == 30 && totals.destructors == 30,
        "session totals");
    necessary(totals.direct_reads == 37 && totals.metadata == 11 && totals.completed_metadata == 10,
        "call totals");
    necessary(totals.consumed == 587 && totals.allocations == 60 && totals.releases == 60,
        "ownership/byte totals");
    necessary(totals.attempts == 61 && totals.exceptions == 1, "allocation attempt totals");
    std::printf(
        "{\"sessions\":%zu,\"constructors\":%zu,\"destructors\":%zu,\"direct_reads\":%zu,"
        "\"metadata_calls\":%zu,\"metadata_complete\":%zu,\"consumed\":%zu,\"allocations\":%zu,"
        "\"releases\":%zu,\"allocation_attempts\":%zu,\"exceptions\":%zu,\"checks\":%zu}\n",
        totals.sessions,
        totals.constructors,
        totals.destructors,
        totals.direct_reads,
        totals.metadata,
        totals.completed_metadata,
        totals.consumed,
        totals.allocations,
        totals.releases,
        totals.attempts,
        totals.exceptions,
        totals.checks);
}

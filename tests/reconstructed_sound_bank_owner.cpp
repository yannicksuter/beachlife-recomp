#include "window_sound_bank.h"

#include <array>
#include <bit>
#include <cerrno>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <type_traits>

#include <fcntl.h>
#include <unistd.h>

namespace
{

struct Failure
{
    const char* message;
};

unsigned long long checks = 0;
unsigned int sample_calls = 0;
unsigned int release_calls = 0;
unsigned int cache_updates = 0;
unsigned int lifetimes = 0;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw Failure{message};
    }
}

static_assert(sizeof(void*) == 8 && sizeof(unsigned int) == 4 && sizeof(int) == 4);
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::digits == 24);
static_assert(sizeof(long double) == 16 && std::numeric_limits<long double>::radix == 2 &&
              std::numeric_limits<long double>::digits == 64 &&
              std::numeric_limits<long double>::max_exponent == 16384);
static_assert(std::is_standard_layout_v<WindowSoundBank>);
static_assert(sizeof(WindowSoundBank) == 48);
static_assert(offsetof(WindowSoundBank, end_index) == 0);
static_assert(offsetof(WindowSoundBank, capacity_04) == 4);
static_assert(offsetof(WindowSoundBank, buckets) == 8);
static_assert(offsetof(WindowSoundBank, unknown_0c) == 16);
static_assert(offsetof(WindowSoundBank, bucket_count) == 20);
static_assert(offsetof(WindowSoundBank, word_14) == 24);
static_assert(offsetof(WindowSoundBank, stream_18) == 32);
static_assert(offsetof(WindowSoundBank, cached_bytes_1c) == 40);
static_assert(sizeof(WindowSoundBucket) == 24);
static_assert(offsetof(WindowSoundBucket, value) == 8);
static_assert(offsetof(WindowSoundBucket, unavailable) == 16);
static_assert(offsetof(WindowSoundBucket, unknown_09) == 17);
static_assert(sizeof(WindowSoundRecord) == 56);
static_assert(offsetof(WindowSoundRecord, bytes_20) == 32);
static_assert(offsetof(WindowSoundRecord, offset_24) == 36);
static_assert(offsetof(WindowSoundRecord, cached_28) == 40);
static_assert(offsetof(WindowSoundRecord, idle_seconds_2c) == 48);
static_assert(offsetof(WindowSoundRecord, references_30) == 52);
static_assert(sizeof(WindowSoundIterator) == 16);

constexpr std::array<unsigned char, 16> guard = {
    0xa1, 0x35, 0x7e, 0xc9, 0x40, 0x82, 0x19, 0x64, 0xb0, 0x0d, 0x53, 0xf8, 0x27, 0x96, 0xea, 0x4b};
constexpr std::array<unsigned char, 16> bank_bytes = {
    0xe0, 0xe1, 0x11, 0x00, 0x82, 0xff, 0xe6, 0xe7, 0xe8, 0x73, 0x24, 0xb5, 0xec, 0xed, 0xee, 0xef};
constexpr std::array<unsigned char, 4> first_payload = {0x11, 0x00, 0x82, 0xff};
constexpr std::array<unsigned char, 3> second_payload = {0x73, 0x24, 0xb5};

struct RecordSpec
{
    const char* name;
    unsigned int offset;
    unsigned int bytes;
};

class File
{
  public:
    explicit File(const std::filesystem::path& path)
        : file_(std::fopen(path.c_str(), "wb"))
    {
        require(file_ != nullptr, "generated file open failed");
    }
    ~File()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
        }
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    void write(std::span<const unsigned char> bytes)
    {
        require(std::fwrite(bytes.data(), 1, bytes.size(), file_) == bytes.size(),
            "generated file write failed");
    }
    void word(unsigned int value)
    {
        const std::array<unsigned char, 4> bytes = {static_cast<unsigned char>(value),
            static_cast<unsigned char>(value >> 8),
            static_cast<unsigned char>(value >> 16),
            static_cast<unsigned char>(value >> 24)};
        write(bytes);
    }
    void close()
    {
        FILE* file = file_;
        file_ = nullptr;
        require(std::fclose(file) == 0, "generated file close failed");
    }

  private:
    FILE* file_;
};

class Inputs
{
  public:
    Inputs(const std::filesystem::path& root, const char* stem)
        : header(root / (std::string(stem) + "-index.bin"))
        , bank(root / (std::string(stem) + "-data.bin"))
    {
        require(!std::filesystem::exists(header) && !std::filesystem::exists(bank),
            "generated input already exists");
    }
    ~Inputs()
    {
        std::error_code error;
        std::filesystem::remove(header, error);
        std::filesystem::remove(bank, error);
    }
    Inputs(const Inputs&) = delete;
    Inputs& operator=(const Inputs&) = delete;

    void write(std::span<const RecordSpec> records, bool trailing_short_offset) const
    {
        File data(bank);
        data.write(bank_bytes);
        data.close();
        File index(header);
        for (const RecordSpec& record : records)
        {
            index.word(record.offset);
            index.word(record.bytes);
            std::array<unsigned char, 32> name{};
            const auto length = std::strlen(record.name);
            require(length < name.size(), "generated name exceeds record");
            std::memcpy(name.data(), record.name, length);
            index.write(name);
        }
        if (trailing_short_offset)
        {
            constexpr std::array<unsigned char, 3> tail = {0x27, 0x00, 0x00};
            index.write(tail);
        }
        index.close();
    }

    std::filesystem::path header;
    std::filesystem::path bank;
};

struct GuardedBank
{
    explicit GuardedBank(const Inputs& inputs)
        : before(guard)
        , bank(inputs.header.c_str(), inputs.bank.c_str())
        , after(guard)
    {
        ++lifetimes;
    }
    GuardedBank(const GuardedBank&) = delete;
    GuardedBank& operator=(const GuardedBank&) = delete;

    void check() const
    {
        require(before == guard && after == guard, "owner outside guard changed");
    }

    std::array<unsigned char, 16> before;
    WindowSoundBank bank;
    std::array<unsigned char, 16> after;
};
static_assert(offsetof(GuardedBank, bank) == 16);
static_assert(offsetof(GuardedBank, after) == 16 + sizeof(WindowSoundBank));

unsigned int open_files_for(const std::filesystem::path& path)
{
    const auto target = std::filesystem::absolute(path);
    unsigned int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
    {
        std::error_code error;
        const auto link = std::filesystem::read_symlink(entry.path(), error);
        if (!error && link == target)
        {
            ++count;
        }
    }
    return count;
}

int check_file_ownership(const Inputs& inputs, const WindowSoundBank& bank)
{
    require(bank.stream_18 != nullptr, "bank FILE missing");
    const int descriptor = fileno(bank.stream_18);
    require(descriptor >= 0 && fcntl(descriptor, F_GETFD) >= 0, "bank descriptor invalid");
    require(open_files_for(inputs.header) == 0, "index FILE remains open");
    require(open_files_for(inputs.bank) == 1, "bank FILE owner count differs");
    return descriptor;
}

void check_closed(int descriptor, const Inputs& inputs)
{
    errno = 0;
    const int result = fcntl(descriptor, F_GETFD);
    require(result == -1 && errno == EBADF, "bank descriptor not closed by destructor");
    require(open_files_for(inputs.bank) == 0 && open_files_for(inputs.header) == 0,
        "FILE survives owner destruction");
}

void seed_opaque(WindowSoundBank& bank)
{
    for (unsigned char& byte : bank.unknown_0c)
    {
        byte = 0x6d;
    }
    for (int index = 0; index < bank.capacity_04; ++index)
    {
        for (unsigned char& byte : bank.buckets[index].unknown_09)
        {
            byte = 0xb7;
        }
    }
}

void check_table(const WindowSoundBank& bank,
    WindowSoundBucket* original_table,
    unsigned int records,
    unsigned int cached_bytes,
    std::span<const int> occupied)
{
    require(bank.end_index == 389 && bank.bucket_count == 389 && bank.capacity_04 == 512,
        "logical and allocation capacities differ");
    require(bank.buckets == original_table && bank.word_14 == records &&
                bank.cached_bytes_1c == cached_bytes,
        "bank state differs");
    for (unsigned char byte : bank.unknown_0c)
    {
        require(byte == 0x6d, "bank opaque byte changed");
    }
    for (int index = 0; index < 512; ++index)
    {
        bool active = false;
        for (int slot : occupied)
        {
            active = active || slot == index;
        }
        const auto& bucket = bank.buckets[index];
        require(active || bucket.key == nullptr, "unpublished bucket key changed");
        if (index < 389)
        {
            require(bucket.unavailable == (active ? 0 : 1), "logical bucket flag differs");
        }
        for (unsigned char byte : bucket.unknown_09)
        {
            require(byte == 0xb7, "bucket opaque byte changed");
        }
        // Spare-slot values and availability, plus ABI padding, are never read.
    }
}

WindowSoundRecord* check_record(
    WindowSoundBank& bank, int index, const char* name, unsigned int bytes, int offset)
{
    auto& bucket = bank.buckets[index];
    require(bucket.unavailable == 0 && bucket.value != nullptr, "published record missing");
    WindowSoundRecord* record = bucket.value;
    require(bucket.key == record->name, "key is not borrowed from its owned record");
    std::array<char, 32> expected_name{};
    std::memcpy(expected_name.data(), name, std::strlen(name));
    require(std::memcmp(record->name, expected_name.data(), expected_name.size()) == 0 &&
                record->bytes_20 == bytes && record->offset_24 == offset &&
                record->cached_28 == nullptr,
        "record metadata differs");
    require(record->references_30 == 0, "initial references differ");
    record->idle_seconds_2c = 1.25f;
    return record;
}

void check_record_timer(const WindowSoundRecord& record)
{
    require(record.idle_seconds_2c == 1.25f, "record timer changed");
}

void* sample(WindowSoundBank& bank, const char* name, unsigned int& bytes)
{
    ++sample_calls;
    bytes = 0xfeed;
    return bank.FindSample(name, &bytes);
}

void release_sample(WindowSoundBank& bank, const char* name)
{
    ++release_calls;
    bank.ReleaseSample(name);
}

void update_cache(WindowSoundBank& bank, float delta)
{
    ++cache_updates;
    bank.UpdateCache(delta);
}

void empty_case(const std::filesystem::path& root)
{
    Inputs inputs(root, "empty");
    inputs.write({}, false);
    int descriptor = -1;
    {
        GuardedBank owner(inputs);
        auto& bank = owner.bank;
        descriptor = check_file_ownership(inputs, bank);
        seed_opaque(bank);
        WindowSoundBucket* table = bank.buckets;
        const auto begin = bank.BeginSound();
        const auto end = bank.EndSound();
        require(begin.bucket == table + 389 && end.bucket == begin.bucket &&
                    begin.lookup == &bank && end.lookup == &bank,
            "empty iterator differs");
        unsigned int bytes = 0;
        require(sample(bank, "ABSENT.WAV", bytes) == nullptr && bytes == 0,
            "empty lookup result differs");
        require(std::ftell(bank.stream_18) == 0, "empty lookup moved stream");
        check_table(bank, table, 0, 0, {});
        owner.check();
    }
    check_closed(descriptor, inputs);
}

void distinct_case(const std::filesystem::path& root)
{
    Inputs inputs(root, "distinct");
    constexpr std::array<RecordSpec, 2> records = {{{"ONE.WAV", 2, 4}, {"TWO.WAV", 9, 3}}};
    inputs.write(records, true);
    int descriptor = -1;
    {
        GuardedBank owner(inputs);
        auto& bank = owner.bank;
        descriptor = check_file_ownership(inputs, bank);
        seed_opaque(bank);
        auto* table = bank.buckets;
        auto* first = check_record(bank, 38, "ONE.WAV", 4, 2);
        auto* second = check_record(bank, 117, "TWO.WAV", 3, 9);
        require(first != second, "two records alias storage");
        const auto begin = bank.BeginSound();
        require(begin.bucket == table + 38 && begin.lookup == &bank, "sparse begin differs");
        unsigned int bytes = 0;
        void* first_data = sample(bank, "one.wav", bytes);
        require(first_data != nullptr && first_data == first->cached_28 && bytes == 4 &&
                    std::memcmp(first_data, first_payload.data(), 4) == 0 &&
                    first->references_30 == 1 && bank.cached_bytes_1c == 4 &&
                    std::ftell(bank.stream_18) == 6,
            "first cold cache differs");
        void* second_data = sample(bank, "TWO.WAV", bytes);
        require(second_data != nullptr && second_data != first_data &&
                    second_data == second->cached_28 && bytes == 3 &&
                    std::memcmp(second_data, second_payload.data(), 3) == 0 &&
                    second->references_30 == 1 && bank.cached_bytes_1c == 7 &&
                    std::ftell(bank.stream_18) == 12,
            "second cold cache differs");
        require(std::fseek(bank.stream_18, 1, SEEK_SET) == 0, "hot-cache cursor setup failed");
        require(sample(bank, "ONE.WAV", bytes) == first_data && bytes == 4 &&
                    first->references_30 == 2 && bank.cached_bytes_1c == 7 &&
                    std::ftell(bank.stream_18) == 1 &&
                    std::memcmp(first_data, first_payload.data(), 4) == 0,
            "hot cache reread or state differs");
        require(sample(bank, "ABSENT.WAV", bytes) == nullptr && bytes == 0 &&
                    bank.cached_bytes_1c == 7 && std::ftell(bank.stream_18) == 1,
            "absent lookup changed cache or cursor");
        constexpr std::array<int, 2> slots = {38, 117};
        check_table(bank, table, 2, 7, slots);
        check_record_timer(*first);
        check_record_timer(*second);
        owner.check();
        // Genuine destruction owns both records and both caches despite live refs.
    }
    check_closed(descriptor, inputs);
}

void duplicate_case(const std::filesystem::path& root)
{
    Inputs inputs(root, "duplicate");
    constexpr std::array<RecordSpec, 2> records = {{{"CLICK8.WAV", 2, 4}, {"CLICK8.WAV", 9, 3}}};
    inputs.write(records, false);
    int descriptor = -1;
    {
        GuardedBank owner(inputs);
        auto& bank = owner.bank;
        descriptor = check_file_ownership(inputs, bank);
        seed_opaque(bank);
        auto* table = bank.buckets;
        auto* first = check_record(bank, 388, "CLICK8.WAV", 4, 2);
        auto* second = check_record(bank, 0, "CLICK8.WAV", 3, 9);
        require(first != second, "duplicate records were replaced or aliased");
        const auto begin = bank.BeginSound();
        const auto found = bank.FindSoundKey(ScreenTextKey("click8.wav"));
        require(begin.bucket == table && begin.lookup == &bank && found.bucket == table + 388 &&
                    found.lookup == &bank,
            "duplicate placement or lookup order differs");
        unsigned int bytes = 0;
        void* data = sample(bank, "click8.wav", bytes);
        require(data != nullptr && data == first->cached_28 && bytes == 4 &&
                    std::memcmp(data, first_payload.data(), 4) == 0 && first->references_30 == 1 &&
                    second->cached_28 == nullptr && second->references_30 == 0,
            "duplicate cold selection differs");
        require(sample(bank, "CLICK8.WAV", bytes) == data && bytes == 4 &&
                    first->references_30 == 2 && std::ftell(bank.stream_18) == 6,
            "duplicate hot selection differs");
        constexpr std::array<int, 2> slots = {0, 388};
        check_table(bank, table, 2, 4, slots);
        check_record_timer(*first);
        check_record_timer(*second);
        owner.check();
        // Both genuine record owners die; the second was never cached or referenced.
    }
    check_closed(descriptor, inputs);
}

void cache_lifecycle_case(const std::filesystem::path& root)
{
    Inputs inputs(root, "lifecycle");
    constexpr std::array<RecordSpec, 2> records = {{{"ONE.WAV", 2, 4}, {"TWO.WAV", 9, 3}}};
    inputs.write(records, false);
    int descriptor = -1;
    {
        GuardedBank owner(inputs);
        auto& bank = owner.bank;
        descriptor = check_file_ownership(inputs, bank);
        seed_opaque(bank);
        auto* table = bank.buckets;
        auto* first = check_record(bank, 38, "ONE.WAV", 4, 2);
        auto* second = check_record(bank, 117, "TWO.WAV", 3, 9);

        update_cache(bank, 10.0f);
        require(first->cached_28 == nullptr && second->cached_28 == nullptr &&
                    first->idle_seconds_2c == 1.25f && second->idle_seconds_2c == 1.25f &&
                    first->references_30 == 0 && second->references_30 == 0 &&
                    bank.cached_bytes_1c == 0 && std::ftell(bank.stream_18) == 0,
            "uncached records were aged");
        release_sample(bank, "ABSENT.WAV");
        require(first->references_30 == 0 && second->references_30 == 0 &&
                    first->idle_seconds_2c == 1.25f && second->idle_seconds_2c == 1.25f &&
                    first->cached_28 == nullptr && second->cached_28 == nullptr &&
                    bank.cached_bytes_1c == 0 && std::ftell(bank.stream_18) == 0,
            "absent release changed records");

        unsigned int bytes = 0;
        void* first_data = sample(bank, "one.wav", bytes);
        require(first_data != nullptr && first->cached_28 == first_data && bytes == 4 &&
                    std::memcmp(first_data, first_payload.data(), 4) == 0 &&
                    first->references_30 == 1 && first->idle_seconds_2c == 1.25f &&
                    bank.cached_bytes_1c == 4 && std::ftell(bank.stream_18) == 6,
            "lifecycle first cold cache differs");
        require(sample(bank, "ONE.WAV", bytes) == first_data && bytes == 4 &&
                    first->references_30 == 2 && first->idle_seconds_2c == 1.25f &&
                    bank.cached_bytes_1c == 4 && std::ftell(bank.stream_18) == 6,
            "lifecycle second reference differs");
        void* second_data = sample(bank, "TWO.WAV", bytes);
        require(second_data != nullptr && second->cached_28 == second_data && bytes == 3 &&
                    std::memcmp(second_data, second_payload.data(), 3) == 0 &&
                    second->references_30 == 1 && second->idle_seconds_2c == 1.25f &&
                    bank.cached_bytes_1c == 7 && std::ftell(bank.stream_18) == 12,
            "lifecycle second cache differs");
        require(std::fseek(bank.stream_18, 1, SEEK_SET) == 0, "lifecycle cursor setup failed");

        release_sample(bank, "one.wav");
        require(first->references_30 == 1 && first->idle_seconds_2c == 1.25f &&
                    first->cached_28 == first_data && second->cached_28 == second_data &&
                    bank.cached_bytes_1c == 7 && std::ftell(bank.stream_18) == 1,
            "two-to-one release changed the timer");
        update_cache(bank, 100.0f);
        require(first->idle_seconds_2c == 1.25f && second->idle_seconds_2c == 1.25f &&
                    first->references_30 == 1 && second->references_30 == 1 &&
                    first->cached_28 == first_data && second->cached_28 == second_data &&
                    bank.cached_bytes_1c == 7 && std::ftell(bank.stream_18) == 1,
            "referenced records were aged");
        release_sample(bank, "ONE.WAV");
        require(first->references_30 == 0 &&
                    std::bit_cast<std::uint32_t>(first->idle_seconds_2c) == 0 &&
                    first->cached_28 == first_data && second->references_30 == 1 &&
                    second->idle_seconds_2c == 1.25f && bank.cached_bytes_1c == 7 &&
                    std::ftell(bank.stream_18) == 1,
            "one-to-zero release did not reset the timer");

        update_cache(bank, 5.0f);
        require(first->idle_seconds_2c == 5.0f && first->cached_28 != nullptr &&
                    first->cached_28 == first_data && first->references_30 == 0 &&
                    second->cached_28 == second_data && second->references_30 == 1 &&
                    second->idle_seconds_2c == 1.25f && bank.cached_bytes_1c == 7 &&
                    std::ftell(bank.stream_18) == 1,
            "cache at exactly five seconds was released");
        update_cache(bank, 0x1p-24f);
        first_data = nullptr;
        require(first->idle_seconds_2c == 5.0f && first->cached_28 == nullptr &&
                    first->references_30 == 0 && second->cached_28 == second_data &&
                    second->references_30 == 1 && second->idle_seconds_2c == 1.25f &&
                    std::memcmp(second_data, second_payload.data(), 3) == 0 &&
                    bank.cached_bytes_1c == 3 && std::ftell(bank.stream_18) == 1,
            "retained sum did not release the cache");

        first_data = sample(bank, "ONE.WAV", bytes);
        require(first_data != nullptr && first->cached_28 == first_data && bytes == 4 &&
                    std::memcmp(first_data, first_payload.data(), 4) == 0 &&
                    first->references_30 == 1 && first->idle_seconds_2c == 5.0f &&
                    bank.cached_bytes_1c == 7 && std::ftell(bank.stream_18) == 6,
            "released cache did not reload");
        release_sample(bank, "ONE.WAV");
        release_sample(bank, "TWO.WAV");
        require(first->references_30 == 0 && second->references_30 == 0 &&
                    std::bit_cast<std::uint32_t>(first->idle_seconds_2c) == 0 &&
                    std::bit_cast<std::uint32_t>(second->idle_seconds_2c) == 0,
            "reloaded references did not reset timers");
        update_cache(bank, 5.25f);
        first_data = nullptr;
        second_data = nullptr;
        require(first->cached_28 == nullptr && second->cached_28 == nullptr &&
                    first->idle_seconds_2c == 5.25f && second->idle_seconds_2c == 5.25f &&
                    bank.cached_bytes_1c == 0 && std::ftell(bank.stream_18) == 6,
            "expired caches or accounting differ");
        release_sample(bank, "TWO.WAV");
        require(second->references_30 == std::numeric_limits<unsigned int>::max() &&
                    second->idle_seconds_2c == 5.25f && first->references_30 == 0 &&
                    first->idle_seconds_2c == 5.25f && bank.cached_bytes_1c == 0 &&
                    std::ftell(bank.stream_18) == 6,
            "zero reference underflow was repaired");
        update_cache(bank, 1.0f);
        require(first->cached_28 == nullptr && second->cached_28 == nullptr &&
                    first->idle_seconds_2c == 5.25f && second->idle_seconds_2c == 5.25f &&
                    second->references_30 == std::numeric_limits<unsigned int>::max() &&
                    std::ftell(bank.stream_18) == 6,
            "uncached expired records were aged");
        constexpr std::array<int, 2> slots = {38, 117};
        check_table(bank, table, 2, 0, slots);
        owner.check();
        // The genuine destructor still owns both records, including the wrapped reference count.
    }
    check_closed(descriptor, inputs);
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "expected generated-directory argument");
        require(std::setlocale(LC_CTYPE, "C") != nullptr, "C locale unavailable");
        const std::filesystem::path root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        empty_case(root);
        distinct_case(root);
        duplicate_case(root);
        cache_lifecycle_case(root);
        std::printf("{\"groups\":4,\"bank_lifetimes\":%u,\"sample_calls\":%u,"
                    "\"release_calls\":%u,\"cache_updates\":%u,\"long_double_bytes\":%zu,"
                    "\"long_double_digits\":%d,\"long_double_max_exponent\":%d,\"checks\":%llu}\n",
            lifetimes,
            sample_calls,
            release_calls,
            cache_updates,
            sizeof(long double),
            std::numeric_limits<long double>::digits,
            std::numeric_limits<long double>::max_exponent,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "sound bank failure: %s\n", failure.message);
        return 2;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "sound bank setup failure: %s\n", failure.what());
        return 3;
    }
}

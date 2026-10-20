#include "data/folder_source.h"
#include "pool_chunk_acquire.h"
#include "pool_lock_guard.h"
#include "renderer_queue_test_state.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"
#include "startup_screen_palette_modifiers.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
namespace data = beachlife::data;
namespace files = beachlife::runtime::resource_files;
using Lookup = ScreenDialogResourceLookup;
using Node = ScreenDialogResourceNode;

static_assert(CHAR_BIT == 8 && sizeof(unsigned int) == 4 && sizeof(unsigned short) == 2);
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(void*) == 8 && sizeof(Node) == 24 && sizeof(Lookup) == 456);
static_assert(offsetof(Lookup, record_count) == 24 && offsetof(Lookup, records) == 32);
static_assert(offsetof(Lookup, loaded_resources) == 40 && offsetof(Lookup, recent_names) == 56);

struct Failure
{
    const char* message;
};
unsigned checks = 0, owners = 0, releases = 0, selected_frames = 0;
const char* group = "setup";

void check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw Failure{message};
    }
}
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void word(std::vector<unsigned char>& bytes, unsigned value)
{
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
}
void dword(std::vector<unsigned char>& bytes, unsigned value)
{
    word(bytes, value);
    word(bytes, value >> 16);
}
void name(std::vector<unsigned char>& bytes, const char* text)
{
    const auto length = std::strlen(text) + 1;
    require(length < 256, "generated name is too long");
    bytes.push_back(static_cast<unsigned char>(length));
    bytes.insert(bytes.end(), text, text + length);
}

constexpr std::array<std::array<unsigned char, 3>, 4> colours{
    {{248, 0, 0}, {0, 252, 0}, {0, 0, 248}, {248, 252, 248}}};
constexpr std::array<unsigned short, 4> rgb565{0xf800, 0x07e0, 0x001f, 0xffff};
constexpr std::array<unsigned short, 4> rgb555{0x7c00, 0x03e0, 0x001f, 0x7fff};

class Generated
{
  public:
    Generated(fs::path root, unsigned records)
        : root_(std::move(root))
    {
        require(!fs::exists(root_), "generated owner directory already exists");
        fs::create_directories(root_ / "Data/Sprites");
        std::vector<unsigned char> metadata(20, 0x42);
        dword(metadata, 0x12345678);
        dword(metadata, records);
        for (unsigned i = 0; i < records; ++i)
        {
            name(metadata, i == 0 ? "alpha" : "dummy");
            dword(metadata, 1); // Actual format-1 palette and Frame metadata.
            dword(metadata, 1);
            for (unsigned c = 0; c < 256; ++c)
            {
                const auto rgb = c < colours.size() ? colours[c] : std::array<unsigned char, 3>{};
                metadata.insert(metadata.end(), rgb.begin(), rgb.end());
            }
            const bool frame = records == 2 && i == 0;
            dword(metadata, frame ? 1 : 0);
            if (frame)
            {
                name(metadata, "idle");
                word(metadata, 2);
                metadata.push_back(1);
                word(metadata, 2);
                word(metadata, 1);
                word(metadata, 0x1234);
                word(metadata, 0xfffe);
                dword(metadata, 24);
                dword(metadata, 4);
                word(metadata, 2);
            }
        }
        std::vector<unsigned char> bank(20, 0x21);
        dword(bank, 0xaabbccdd);
        word(bank, 0);
        word(bank, 1);
        dword(bank, 0);
        dword(bank, 2);
        bank.insert(bank.end(), {0x81, 0x82, 0x93, 0x94});
        std::vector<unsigned char> modifiers;
        for (unsigned i = 0; i < 12 * 64; ++i)
        {
            const auto& rgb = colours[i % colours.size()];
            modifiers.insert(modifiers.end(), rgb.begin(), rgb.end());
        }
        add("Data/Sprites/BLSprite.hdr", std::move(metadata));
        add("Data/Sprites/BLSprite.Dat", std::move(bank));
        add("Data/Sprites/PaletteModifiers.bin", std::move(modifiers));
        add("held.bin", {0x5a, 0xc3, 0x17});
    }
    ~Generated()
    {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    std::shared_ptr<const data::FileSource> mount() const
    {
        return std::make_shared<data::FolderSource>(root_, entries_);
    }
    void unchanged() const
    {
        for (const auto& input : inputs_)
        {
            check(fs::file_size(root_ / input.first) == input.second.size(), "input size changed");
            std::vector<unsigned char> actual(input.second.size());
            std::ifstream stream(root_ / input.first, std::ios::binary);
            stream.read(reinterpret_cast<char*>(actual.data()),
                static_cast<std::streamsize>(actual.size()));
            require(stream.gcount() == static_cast<std::streamsize>(actual.size()),
                "cannot reread generated owner input");
            check(actual == input.second, "input contents changed");
        }
    }

  private:
    void add(const char* path, std::vector<unsigned char> bytes)
    {
        std::ofstream stream(root_ / path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        stream.close();
        require(bool(stream), "cannot write generated owner input");
        entries_.push_back({path, bytes.size()});
        inputs_.emplace_back(path, std::move(bytes));
    }
    fs::path root_;
    std::vector<data::SourceEntry> entries_;
    std::vector<std::pair<std::string, std::vector<unsigned char>>> inputs_;
};

// Real AllocatePoolBytes consumes one live PoolChunkLink, then the real ring
// constructor begins Node lifetime. ReturnPool begins PoolChunkLink lifetime
// again. No old Node read follows owner destruction, and no refill is needed.
struct PoolBacking
{
    std::array<unsigned char, 16> before{};
    alignas(Node) unsigned char storage[(sizeof(Node) + 7u) & ~7u]{};
    std::array<unsigned char, 16> after{};
    static constexpr unsigned bucket = (sizeof(storage) - 1u) / 8u;

    PoolBacking()
    {
        check(pool_lock_word == 0 && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                  pool_heap_bytes == 0,
            "pool must start without a heap span or lock");
        for (auto* entry : pool_chunk_buckets)
        {
            check(entry == nullptr, "pool has an existing free root");
        }
        before.fill(0x39);
        after.fill(0xc6);
        pool_chunk_buckets[bucket] = ::new (static_cast<void*>(storage)) PoolChunkLink{nullptr};
    }
    ~PoolBacking()
    {
        // The genuine ring has already ended. No free-list traversal is
        // needed to release this separately owned fixture backing.
        pool_chunk_buckets[bucket] = nullptr;
    }
    void restored() const
    {
        auto* link = pool_chunk_buckets[bucket];
        check(static_cast<const void*>(link) == storage && link->next == nullptr,
            "real ring did not return its sentinel");
        check(pool_lock_word == 0 && pool_chunk_begin == nullptr && pool_chunk_end == nullptr &&
                  pool_heap_bytes == 0,
            "bounded ring operation changed heap state or retained lock");
        for (unsigned i = 0; i < 16; ++i)
        {
            check(before[i] == 0x39 && after[i] == 0xc6, "pool backing guard changed");
            if (i != bucket)
            {
                check(pool_chunk_buckets[i] == nullptr, "another pool bucket changed");
            }
        }
    }
};

struct Environment
{
    ScreenRequestRenderer renderer{}, saved{};
    explicit Environment(unsigned format)
    {
        check(screen_dialog_resources == nullptr && screen_dialog_palette_modifiers == nullptr &&
                  screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0 &&
                  screen_request_renderer == nullptr,
            "another resource environment is active");
        renderer_queue_test::initialize(renderer, 31);
        renderer.format_44 = format;
        saved = renderer;
        screen_request_renderer = &renderer;
    }
    ~Environment()
    {
        // Fixture ownership ends after the real game owner and resource Session.
        // The game destructor intentionally leaves the file/cursor globals alone.
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
        screen_request_renderer = nullptr;
    }
    void unchanged() const
    {
        check(renderer_queue_test::equal(renderer, saved), "borrowed typed renderer changed");
    }
};

struct Owner
{
    std::array<unsigned char, 16> before = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
    Lookup value;
    std::array<unsigned char, 16> after = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};

    Owner()
    {
        ++owners;
    }
    void guards() const
    {
        for (unsigned i = 0; i < before.size(); ++i)
        {
            check(before[i] == 7 && after[i] == 9, "genuine owner guard changed");
        }
    }
};

struct Handle
{
    Lookup& owner;
    ScreenDialogResource* value;
    ~Handle()
    {
        if (value)
        {
            owner.Release(value);
            ++releases;
        }
    }
};

void check_graph(Lookup& owner, unsigned count, unsigned format)
{
    // This is the first control oracle. Its failure unwinds the complete owner.
    check(owner.word_1c == 0, "constructor state word differs");
    check(screen_dialog_resources == &owner, "resource registration did not publish this owner");
    check(owner.record_count == static_cast<int>(count), "real parser record count differs");
    check(owner.records != nullptr, "real record table was not allocated");
    check(owner.loaded_resources.head->next == owner.loaded_resources.head &&
              owner.loaded_resources.head->previous == owner.loaded_resources.head,
        "real ring is not empty");
    for (const auto& recent : owner.recent_names)
    {
        check(recent[0] == '\0', "audited initial name was not empty");
    }
    check(owner.metadata_file != nullptr && files::close_stream(owner.metadata_file) == -1,
        "Load did not retain an already-closed metadata token");
    const auto& expected = format == 0 ? rgb565 : rgb555;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& record = *owner.records[i];
        check(std::strcmp(record.name, i == 0 ? "alpha" : "dummy") == 0,
            "real owned record name differs");
        check(record.reference_count == 0 && record.format_18 == 1 && record.palette_count == 1 &&
                  record.word_1c == 0,
            "real record scalar state differs");
        for (unsigned c = 0; c < 256; ++c)
        {
            check(record.palettes[0][c] == (c < expected.size() ? expected[c] : 0),
                "record palette literal differs");
        }
    }
    check(owner.palette_modifiers == screen_dialog_palette_modifiers,
        "palette registration/owned pointer differ");
    for (const auto& bank : owner.palette_modifiers->colours)
    {
        for (unsigned c = 0; c < 64; ++c)
        {
            check(bank[c] == expected[c % expected.size()], "modifier palette literal differs");
        }
    }
}

void run_group(const fs::path& root, unsigned index)
{
    const unsigned count = index == 0 ? 2 : index == 1 ? 1 : 0;
    const unsigned format = index == 1 ? 7 : 0;
    Generated generated(root, count);
    Environment environment(format);
    auto mount = generated.mount();
    std::weak_ptr<const data::FileSource> borrowed_mount = mount;
    {
        files::Session session(mount, index == 2 ? 0 : 3);
        mount.reset();
        PoolBacking pool;
        int held = -1;
        if (index == 1)
        {
            held = files::open_descriptor("held.bin", 0);
            check(held == 3, "real held descriptor allocation differs");
            screen_dialog_frame_file = held;
        }
        files::Stream* closed_metadata = nullptr;
        int descriptor = -1;
        unsigned expected_cursor = 24;
        {
            Owner owner;
            check_graph(owner.value, count, format);
            descriptor = owner.value.data_file;
            closed_metadata = owner.value.metadata_file;
            check(descriptor == (index == 2      ? 0
                                    : index == 1 ? 4
                                                 : 3),
                "real owner descriptor differs");
            check(screen_dialog_frame_file == (held >= 0 ? held : descriptor) &&
                      screen_dialog_frame_cursor == 24,
                "Load descriptor/cursor publication differs");
            if (index == 0)
            {
                {
                    Handle direct{owner.value, owner.value.Lookup("ALPHA")};
                    Handle fallback{owner.value, owner.value.Find("not-present")};
                    check(direct.value && fallback.value, "real lookup did not allocate a handle");
                    check(direct.value->record == owner.value.records[0] &&
                              fallback.value->record == owner.value.records[1] &&
                              owner.value.records[0]->reference_count == 1 &&
                              owner.value.records[1]->reference_count == 1,
                        "real lookup/fallback retained the wrong record");
                }
                check(owner.value.records[0]->reference_count == 0 &&
                          owner.value.records[1]->reference_count == 0,
                    "real Release did not balance both handles");
                auto& frame = *owner.value.records[0]->frames[0];
                check(std::strcmp(frame.name, "idle") == 0 && frame.count_0a == 2 &&
                          frame.plane_count_0c == 1 && frame.width_0e == 2 &&
                          frame.height_10 == 1 && frame.word_14 == -2 &&
                          frame.file_offset_1c == 24 && frame.data_size_18 == 4 &&
                          frame.offset_count_40 == 2,
                    "real Frame metadata differs");
                const unsigned char* selected = frame.SelectData(99, 99);
                ++selected_frames;
                check(frame.loaded_08 == 1 && frame.whole_data_48 == 1 &&
                          frame.indices_44[0] == 0 && frame.indices_44[1] == 1 &&
                          frame.offsets_70[0] == 0 && frame.offsets_70[1] == 2 &&
                          selected == frame.data_4c + 2 && selected[0] == 0x93 &&
                          selected[1] == 0x94 && frame.data_4c[0] == 0x81 &&
                          frame.data_4c[1] == 0x82,
                    "real lazy frame selected different mounted data");
                expected_cursor = 40;
                check(screen_dialog_frame_cursor == expected_cursor, "lazy-load cursor differs");
            }
            owner.guards();
            environment.unchanged();
        }
        check(screen_dialog_resources == nullptr && screen_dialog_palette_modifiers == nullptr,
            "real owner teardown left a registration");
        check(files::close_stream(closed_metadata) == -1,
            "retained closed token became a different stream");
        check(screen_dialog_frame_file == (held >= 0 ? held : descriptor) &&
                  screen_dialog_frame_cursor == expected_cursor,
            "destructor repaired the original retained file globals");
        std::array<unsigned char, 3> probe{0xa9, 0xcc, 0x9a};
        const int result = files::read_descriptor(descriptor, probe.data() + 1, 1);
        if (index == 2)
        {
            check(result == 1 && probe == std::array<unsigned char, 3>{0xa9, 0, 0x9a},
                "descriptor-zero destructor branch did not leave the stream open");
            // The real owning Session, not a game-source repair, closes this descriptor.
        }
        else
        {
            check(result == -1 && probe == std::array<unsigned char, 3>{0xa9, 0xcc, 0x9a},
                "nonzero owner descriptor was not closed");
        }
        if (held >= 0)
        {
            check(files::read_descriptor(held, probe.data() + 1, 1) == 1 && probe[1] == 0x5a,
                "Load consumed or teardown closed the preexisting global descriptor");
            check(files::close_descriptor(held) == 0, "fixture held descriptor close differs");
        }
        pool.restored();
        environment.unchanged();
    }
    check(borrowed_mount.expired(), "Session retained the real mounted source after teardown");
    generated.unchanged();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 3;
    }
    try
    {
        constexpr std::array<const char*, 3> groups{
            "owned-graph", "held-descriptor", "zero-descriptor"};
        for (unsigned index = 0; index < groups.size(); ++index)
        {
            group = groups[index];
            run_group(fs::path(argv[1]) / group, index);
        }
        std::printf("{\"groups\":3,\"owners\":%u,\"releases\":%u,\"selected_frames\":%u,"
                    "\"checks\":%u,\"owner_size\":%zu}\n",
            owners,
            releases,
            selected_frames,
            checks,
            sizeof(Lookup));
    }
    catch (const Failure& error)
    {
        std::fprintf(stderr, "%s: %s\n", group, error.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s: %s\n", group, error.what());
        return 3;
    }
}

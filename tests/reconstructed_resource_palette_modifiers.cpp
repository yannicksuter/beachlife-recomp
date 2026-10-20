#include "renderer_queue_test_state.h"
#include "startup_screen_palette_modifiers.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
using Palette = ScreenDialogPaletteModifiers;
using Bytes = std::vector<unsigned char>;
constexpr std::size_t palette_bytes = 12 * 64 * 2;
constexpr std::size_t opaque_bytes = 128;
constexpr std::size_t file_bytes = 12 * 64 * 3;
constexpr unsigned char storage_pattern = 0x36;
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<Palette>);
static_assert(offsetof(Palette, colours) == 0);
static_assert(sizeof(Palette::colours) == palette_bytes);
static_assert(offsetof(Palette, unknown_600) == palette_bytes);
static_assert(sizeof(Palette::unknown_600) == opaque_bytes);
static_assert(sizeof(Palette) == palette_bytes + opaque_bytes);
static_assert(std::is_same_v<decltype(screen_dialog_palette_modifiers), Palette*>);
static_assert(!std::is_trivially_copyable_v<ScreenRequestRenderer>);

unsigned int cases = 0;
unsigned int constructors = 0;
unsigned int destructors = 0;
unsigned int explicit_loads = 0;
unsigned long long checks = 0;
unsigned long long byte_checks = 0;

struct FixtureFailure
{
    unsigned int case_number;
    const char* message;
};

[[noreturn]] void fail(const char* message)
{
    throw FixtureFailure{cases, message};
}

void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        fail(message);
    }
}

void compare_bytes(const unsigned char* actual, const unsigned char* expected, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        ++byte_checks;
        require(actual[index] == expected[index], "backing or file byte changed");
    }
}

void pattern(const unsigned char* actual, std::size_t size, unsigned char expected)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        ++byte_checks;
        require(actual[index] == expected, "opaque or guard byte changed");
    }
}

// Input triples and both expected tables are the accepted independent literals.
constexpr unsigned char bank_rgb[12][3] = {{0, 0, 0},
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 255},
    {128, 4, 8},
    {7, 3, 7},
    {8, 4, 8},
    {16, 8, 24},
    {31, 63, 95},
    {123, 45, 67},
    {248, 252, 248}};
constexpr unsigned short words_565[12] = {
    0x0000, 0xf800, 0x07e0, 0x001f, 0xffff, 0x8021, 0x0000, 0x0821, 0x1043, 0x19eb, 0x7968, 0xffff};
constexpr unsigned short words_555[12] = {
    0x0000, 0x7c00, 0x03e0, 0x001f, 0x7fff, 0x4001, 0x0000, 0x0401, 0x0823, 0x0ceb, 0x3ca8, 0x7fff};

Bytes generated_file(bool alternate)
{
    Bytes bytes;
    bytes.reserve(file_bytes);
    for (std::size_t bank = 0; bank < 12; ++bank)
    {
        for (std::size_t colour = 0; colour < 64; ++colour)
        {
            const std::size_t selected = alternate && colour % 2 != 0 ? (bank + 1) % 12 : bank;
            bytes.insert(bytes.end(), std::begin(bank_rgb[selected]), std::end(bank_rgb[selected]));
        }
    }
    return bytes;
}

void write_file(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    require(bool(stream), "generated file write failed");
}

void verify_file(const std::filesystem::path& path, const Bytes& expected)
{
    std::ifstream stream(path, std::ios::binary);
    const Bytes actual{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    require(!stream.bad() && actual.size() == expected.size(), "generated file extent changed");
    compare_bytes(actual.data(), expected.data(), expected.size());
}

struct Files
{
    std::filesystem::path uniform_file;
    std::filesystem::path alternating_file;
    std::filesystem::path trailing_file;
    std::string uniform;
    std::string alternating;
    std::string trailing;
    std::string missing;
    Bytes uniform_bytes;
    Bytes alternating_bytes;
    Bytes trailing_bytes;

    explicit Files(const std::filesystem::path& root)
        : uniform_file(root / "r142-uniform.bin")
        , alternating_file(root / "r142-alternating.bin")
        , trailing_file(root / "r142-trailing.bin")
        , uniform(uniform_file.string())
        , alternating(alternating_file.string())
        , trailing(trailing_file.string())
        , missing((root / "r142-absent.bin").string())
        , uniform_bytes(generated_file(false))
        , alternating_bytes(generated_file(true))
        , trailing_bytes(uniform_bytes)
    {
        std::filesystem::create_directories(root);
        // Only the task's generated absent-path sentinel is removed.
        std::filesystem::remove(std::filesystem::path(missing));
        require(!std::filesystem::exists(std::filesystem::path(missing)), "missing path exists");
        trailing_bytes.insert(trailing_bytes.end(), {91, 82, 73, 64, 55, 46, 37, 28, 19});
        write_file(uniform_file, uniform_bytes);
        write_file(alternating_file, alternating_bytes);
        write_file(trailing_file, trailing_bytes);
        require(uniform_bytes.size() == 2304 && alternating_bytes.size() == 2304 &&
                    trailing_bytes.size() == 2313,
            "literal file extent differs");
    }

    void verify() const
    {
        verify_file(uniform_file, uniform_bytes);
        verify_file(alternating_file, alternating_bytes);
        verify_file(trailing_file, trailing_bytes);
        require(
            !std::filesystem::exists(std::filesystem::path(missing)), "missing path was created");
    }
};

struct RendererFrame
{
    std::array<unsigned char, 16> before;
    ScreenRequestRenderer value{};
    std::array<unsigned char, 16> after;

    RendererFrame()
    {
        before.fill(0xa7);
        after.fill(0x5d);
        renderer_queue_test::initialize(value, 0x4b);
    }
};

struct Environment
{
    const char* saved_path = screen_dialog_palette_file_path;
    ScreenRequestRenderer* saved_renderer = screen_request_renderer;
    RendererFrame renderer;

    Environment()
    {
        require(screen_dialog_palette_modifiers == nullptr, "registration was not initially null");
        require(std::strcmp(saved_path, "Data\\Sprites\\PaletteModifiers.bin") == 0,
            "actual path initializer differs");
    }

    ~Environment()
    {
        screen_dialog_palette_file_path = saved_path;
        screen_request_renderer = saved_renderer;
    }

    void select(const std::string& path, bool present, unsigned int format)
    {
        renderer.value.format_44 = format;
        screen_dialog_palette_file_path = path.c_str();
        screen_request_renderer = present ? &renderer.value : nullptr;
    }

    void unchanged(const ScreenRequestRenderer& snapshot,
        const char* path,
        ScreenRequestRenderer* selected) const
    {
        require(screen_dialog_palette_file_path == path, "path pointer changed");
        require(screen_request_renderer == selected, "renderer pointer changed");
        require(
            renderer_queue_test::equal(renderer.value, snapshot), "typed renderer state changed");
        pattern(renderer.before.data(), renderer.before.size(), 0xa7);
        pattern(renderer.after.data(), renderer.after.size(), 0x5d);
    }
};

struct Slot
{
    std::array<unsigned char, 16> before;
    alignas(Palette) unsigned char storage[sizeof(Palette)];
    std::array<unsigned char, 16> after;
    Palette* live = nullptr;

    Slot()
    {
        before.fill(0xb3);
        after.fill(0x7c);
        // These observable stores precede placement construction. No live object,
        // pointer, padding or compiler-created vptr is overwritten.
        volatile unsigned char* bytes = storage;
        for (std::size_t index = 0; index < sizeof(storage); ++index)
        {
            bytes[index] = storage_pattern;
        }
    }

    ~Slot()
    {
        if (live)
        {
            live->~Palette();
        }
    }

    void preserved() const
    {
        pattern(before.data(), before.size(), 0xb3);
        pattern(after.data(), after.size(), 0x7c);
        pattern(storage + palette_bytes, opaque_bytes, storage_pattern);
    }

    void construct(Environment& environment)
    {
        require(live == nullptr, "slot already live");
        const ScreenRequestRenderer snapshot = environment.renderer.value;
        const char* path = screen_dialog_palette_file_path;
        ScreenRequestRenderer* selected = screen_request_renderer;
        ++constructors;
        live = ::new (static_cast<void*>(storage)) Palette;
        require(live == reinterpret_cast<Palette*>(storage), "constructor storage differs");
        require(screen_dialog_palette_modifiers == live, "constructor registration differs");
        environment.unchanged(snapshot, path, selected);
        preserved();
    }

    void load(Environment& environment)
    {
        require(live != nullptr, "load without live owner");
        const ScreenRequestRenderer snapshot = environment.renderer.value;
        const char* path = screen_dialog_palette_file_path;
        ScreenRequestRenderer* selected = screen_request_renderer;
        ++explicit_loads;
        live->Load();
        require(screen_dialog_palette_modifiers == live, "load changed registration");
        environment.unchanged(snapshot, path, selected);
        preserved();
    }

    void destroy(Environment& environment)
    {
        require(live != nullptr, "destruction without live owner");
        std::array<unsigned char, sizeof(Palette)> snapshot;
        std::memcpy(snapshot.data(), static_cast<const void*>(storage), snapshot.size());
        const ScreenRequestRenderer renderer_snapshot = environment.renderer.value;
        const char* path = screen_dialog_palette_file_path;
        ScreenRequestRenderer* selected = screen_request_renderer;
        ++destructors;
        live->~Palette();
        live = nullptr;
        require(screen_dialog_palette_modifiers == nullptr, "destructor registration differs");
        // The backing array and external guards remain alive after the object.
        compare_bytes(storage, snapshot.data(), snapshot.size());
        preserved();
        environment.unchanged(renderer_snapshot, path, selected);
    }
};
static_assert(offsetof(Slot, storage) == sizeof(Slot::before));
static_assert(offsetof(Slot, after) == offsetof(Slot, storage) + sizeof(Palette));

void expect_words(const Slot& slot, const unsigned short (&expected)[12], bool alternate = false)
{
    require(slot.live != nullptr, "palette observation without live owner");
    for (std::size_t bank = 0; bank < 12; ++bank)
    {
        for (std::size_t colour = 0; colour < 64; ++colour)
        {
            const std::size_t selected = alternate && colour % 2 != 0 ? (bank + 1) % 12 : bank;
            require(
                slot.live->colours[bank][colour] == expected[selected], "palette literal differs");
        }
    }
}

enum class LoadCase
{
    Missing,
    Zero,
    One,
    Other,
    High,
    Alternating,
    Trailing,
    Repeat,
    MissingAfter
};
constexpr LoadCase load_cases[] = {LoadCase::Missing,
    LoadCase::Zero,
    LoadCase::One,
    LoadCase::Other,
    LoadCase::High,
    LoadCase::Alternating,
    LoadCase::Trailing,
    LoadCase::Repeat,
    LoadCase::MissingAfter};

void load_case(LoadCase which, Environment& environment, const Files& files)
{
    Slot slot;
    environment.select(files.missing, false, 0);
    slot.construct(environment);
    // No unsigned-short value is read from the uninitialized palette here.
    pattern(slot.storage, palette_bytes, storage_pattern);
    if (which == LoadCase::Missing)
    {
        slot.load(environment);
        pattern(slot.storage, palette_bytes, storage_pattern);
    }
    else
    {
        const bool alternate = which == LoadCase::Alternating;
        const std::string& path = alternate                     ? files.alternating
                                  : which == LoadCase::Trailing ? files.trailing
                                                                : files.uniform;
        const unsigned int format = which == LoadCase::One     ? 1U
                                    : which == LoadCase::Other ? 2U
                                    : which == LoadCase::High  ? 0xffffffffU
                                                               : 0U;
        environment.select(path, true, format);
        slot.load(environment);
        expect_words(slot, format == 0 ? words_565 : words_555, alternate);
        if (which == LoadCase::Repeat)
        {
            environment.select(files.uniform, true, 1);
            slot.load(environment);
            expect_words(slot, words_555);
        }
        if (which == LoadCase::MissingAfter)
        {
            environment.select(files.missing, false, 0);
            slot.load(environment);
            expect_words(slot, words_565);
        }
    }
    slot.destroy(environment);
    ++cases;
}

void single_lifetime(unsigned int mode, Environment& environment, const Files& files)
{
    Slot slot;
    environment.select(mode == 0 ? files.missing : files.uniform, mode != 0, mode == 2 ? 2U : 0U);
    slot.construct(environment);
    if (mode == 0)
    {
        pattern(slot.storage, palette_bytes, storage_pattern);
    }
    else
    {
        expect_words(slot, mode == 1 ? words_565 : words_555);
    }
    slot.destroy(environment);
    ++cases;
}

void paired_lifetime(bool older_first, Environment& environment, const Files& files)
{
    Slot a;
    Slot b;
    environment.select(files.missing, false, 0);
    a.construct(environment);
    b.construct(environment);
    pattern(a.storage, palette_bytes, storage_pattern);
    pattern(b.storage, palette_bytes, storage_pattern);
    if (older_first)
    {
        a.destroy(environment);
        require(b.live != nullptr, "newer owner lifetime ended early");
        b.destroy(environment);
    }
    else
    {
        b.destroy(environment);
        require(a.live != nullptr, "older owner lifetime ended early");
        a.destroy(environment);
    }
    ++cases;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "expected generated file directory");
        Files files{std::filesystem::path(argv[1])};
        const char* saved_path = screen_dialog_palette_file_path;
        ScreenRequestRenderer* saved_renderer = screen_request_renderer;
        {
            Environment environment;
            for (LoadCase item : load_cases)
            {
                load_case(item, environment, files);
            }
            for (unsigned int mode = 0; mode < 3; ++mode)
            {
                single_lifetime(mode, environment, files);
            }
            paired_lifetime(true, environment, files);
            paired_lifetime(false, environment, files);
            require(screen_dialog_palette_modifiers == nullptr, "registration remained live");
        }
        require(screen_dialog_palette_file_path == saved_path, "path owner was not restored");
        require(screen_request_renderer == saved_renderer, "renderer owner was not restored");
        files.verify();
        require(cases == 14 && constructors == 16 && destructors == 16 && explicit_loads == 11,
            "literal lifetime or call count differs");
        std::printf("Reconstructed palette modifiers passed: %u cases, %u constructors, "
                    "%u loads, %u destructors, %llu checks, %llu byte checks\n",
            cases,
            constructors,
            explicit_loads,
            destructors,
            checks,
            byte_checks);
        return 0;
    }
    catch (const FixtureFailure& failure)
    {
        std::fprintf(stderr, "case %u: %s\n", failure.case_number, failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "fixture host failure: %s\n", error.what());
        return 3;
    }
}

#include "data/folder_source.h"
#include "runtime/resource_files.h"
#include "startup_screen_dialog_frame_file.h"
#include "startup_screen_dialog_resource.h"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <vector>

using Frame = ScreenDialogResourceFrame;
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && sizeof(float) == 4);
static_assert(std::endian::native == std::endian::little);

namespace
{
namespace fs = std::filesystem;
namespace files = beachlife::runtime::resource_files;
namespace data = beachlife::data;
fs::path generated_root;
struct Totals
{
    unsigned sessions = 0, calls = 0, constructors = 0, destructors = 0, files = 0;
    std::size_t checks = 0, byte_checks = 0;
} totals;
const char* current_case = "startup";
const char* failed_case = nullptr;
const char* failure = nullptr;

void expect(bool condition, const char* reason)
{
    ++totals.checks;
    if (!condition && !failure)
    {
        failed_case = current_case;
        failure = reason;
    }
}

void require(bool condition, const char* reason)
{
    if (!condition)
    {
        std::fprintf(stderr, "resource frame select setup: %s\n", reason);
        std::exit(3);
    }
}

template <class T, std::size_t N> T* owned_copy(const std::array<T, N>& values)
{
    // The real Frame destructor uses scalar operator delete for every owner.
    void* storage = ::operator new(sizeof(T) * N);
    std::memcpy(storage, values.data(), sizeof(T) * N);
    return static_cast<T*>(storage);
}

template <class T, std::size_t N> void unchanged(const T* actual, const std::array<T, N>& expected)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        expect(actual[i] == expected[i], "owned table changed");
    }
}

void bytes(const unsigned char* actual, std::size_t count, unsigned first)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        ++totals.byte_checks;
        expect(actual[i] == static_cast<unsigned char>(first + i), "owned byte changed");
    }
}

struct FrameOwner
{
    std::array<unsigned char, 16> before{};
    alignas(Frame) std::array<unsigned char, sizeof(Frame)> storage{};
    std::array<unsigned char, 16> after{};
    Frame* frame;

    FrameOwner()
        : frame(::new (storage.data()) Frame)
    {
        ++totals.constructors;
        before.fill(0xa7);
        after.fill(0x5c);
        frame->name = owned_copy(std::array<char, 6>{'f', 'r', 'a', 'm', 'e', '\0'});
        frame->time_04 = 2.5F;
        frame->unknown_09 = 0x91;
        frame->unknown_0d = 0x92;
        frame->width_0e = 19;
        frame->height_10 = 23;
        frame->unknown_12[0] = 0x93;
        frame->unknown_12[1] = 0x94;
        frame->word_14 = -7;
        frame->unknown_17 = 0x95;
        frame->unknown_42[0] = 0x96;
        frame->unknown_42[1] = 0x97;
        frame->unknown_49[0] = 0x98;
        frame->unknown_49[1] = 0x99;
        frame->unknown_49[2] = 0x9a;
        frame->flag_74 = 0x9b;
    }

    FrameOwner(const FrameOwner&) = delete;
    FrameOwner& operator=(const FrameOwner&) = delete;

    void guards() const
    {
        for (unsigned char value : before)
        {
            ++totals.byte_checks;
            expect(value == 0xa7, "leading Frame guard changed");
        }
        for (unsigned char value : after)
        {
            ++totals.byte_checks;
            expect(value == 0x5c, "trailing Frame guard changed");
        }
    }

    ~FrameOwner()
    {
        expect(std::strcmp(frame->name, "frame") == 0, "owned name changed");
        expect(frame->time_04 == 2.5F && frame->width_0e == 19 && frame->height_10 == 23 &&
                   frame->word_14 == -7 && frame->flag_74 == 0x9b,
            "unrelated Frame values changed");
        expect(frame->unknown_09 == 0x91 && frame->unknown_0d == 0x92 &&
                   frame->unknown_12[0] == 0x93 && frame->unknown_12[1] == 0x94 &&
                   frame->unknown_17 == 0x95 && frame->unknown_42[0] == 0x96 &&
                   frame->unknown_42[1] == 0x97 && frame->unknown_49[0] == 0x98 &&
                   frame->unknown_49[1] == 0x99 && frame->unknown_49[2] == 0x9a,
            "opaque Frame bytes changed");
        guards();
        frame->~Frame();
        ++totals.destructors;
        // Only the enclosing live guards are read after real Frame destruction.
        guards();
    }
};

long long file_position()
{
    return files::seek_descriptor(screen_dialog_frame_file, 0, SEEK_CUR);
}

struct GeneratedFile
{
    fs::path path;
    std::unique_ptr<files::Session> session;
    std::vector<unsigned char> expected;

    GeneratedFile(const std::array<unsigned int, 4>& offsets, std::size_t data_size, bool large)
        : path(generated_root / "raw.bin")
        , expected(32 + data_size, 0xcc)
    {
        // Eight prefix bytes, four LE16 indices, four LE32 offsets, then data.
        constexpr std::array<unsigned char, 8> indices{0, 0, 1, 0, 2, 0, 3, 0};
        std::memcpy(expected.data() + 8, indices.data(), indices.size());
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            for (unsigned byte = 0; byte < 4; ++byte)
            {
                expected[16 + 4 * i + byte] = static_cast<unsigned char>(offsets[i] >> (8 * byte));
            }
        }
        for (std::size_t i = 0; i < data_size; ++i)
        {
            expected[32 + i] = large ? static_cast<unsigned char>((13 * i + 7) % 251)
                                     : static_cast<unsigned char>(0x60 + i);
        }
        fs::create_directories(generated_root);
        require(!fs::exists(path), "generated raw path already exists");
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(expected.data()),
            static_cast<std::streamsize>(expected.size()));
        output.close();
        require(bool(output), "generated file write");
        auto source = std::make_shared<data::FolderSource>(
            generated_root, std::vector<data::SourceEntry>{{"raw.bin", expected.size()}});
        session = std::make_unique<files::Session>(source);
        screen_dialog_frame_file = files::open_descriptor("raw.bin", 0x8000);
        require(screen_dialog_frame_file >= 0, "mounted file descriptor");
        require(file_position() == 0, "generated file positioning");
        screen_dialog_frame_cursor = 0;
        ++totals.files;
    }

    GeneratedFile(const GeneratedFile&) = delete;
    GeneratedFile& operator=(const GeneratedFile&) = delete;

    ~GeneratedFile()
    {
        // No selector or loader call follows this independent stream read.
        std::ifstream input(path, std::ios::binary);
        expect(bool(input), "file verification open");
        for (unsigned char value : expected)
        {
            ++totals.byte_checks;
            expect(input.get() == value, "generated file content changed");
        }
        expect(input.get() == EOF, "generated file length changed");
        input.close();
        expect(files::close_descriptor(screen_dialog_frame_file) == 0, "generated file close");
        screen_dialog_frame_file = -1;
        screen_dialog_frame_cursor = 0;
        session.reset();
        require(fs::remove(path), "generated raw file removal");
    }
};

void select(Frame& frame, int number, int plane, const unsigned char* expected, unsigned byte)
{
    const unsigned char* selected = frame.SelectData(number, plane);
    ++totals.calls;
    expect(selected == expected, "selected byte address");
    if (selected == expected)
    {
        ++totals.byte_checks;
        expect(*selected == byte, "selected byte value");
    }
}

void common_metadata(Frame& frame, unsigned short count, unsigned char planes, int size)
{
    frame.count_0a = count;
    frame.plane_count_0c = planes;
    frame.offset_count_40 = 4;
    frame.data_size_18 = size;
    frame.file_offset_1c = 8;
}

void preserved_metadata(const Frame& frame, unsigned short count, int size, unsigned short offsets)
{
    expect(frame.count_0a == count && frame.plane_count_0c == 2 && frame.data_size_18 == size &&
               frame.offset_count_40 == offsets && frame.file_offset_1c == 8,
        "selection changed metadata");
}

void resident_whole()
{
    current_case = "resident whole upper clamps";
    ++totals.sessions;
    FrameOwner owner;
    Frame& frame = *owner.frame;
    common_metadata(frame, 3, 2, 24);
    frame.offset_count_40 = 6;
    constexpr std::array<unsigned short, 6> indices{2, 0, 1, 4, 5, 3};
    constexpr std::array<unsigned int, 6> offsets{1, 4, 7, 10, 13, 16};
    std::array<unsigned char, 24> data{};
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        data[i] = static_cast<unsigned char>(0x40 + i);
    }
    frame.indices_44 = owned_copy(indices);
    frame.offsets_70 = owned_copy(offsets);
    frame.data_4c = owned_copy(data);
    frame.loaded_08 = 3;
    frame.whole_data_48 = 2;
    screen_dialog_frame_cursor = 77;

    // Equality upper-clamps both arguments. The control returns data+13.
    select(frame, 3, 2, frame.data_4c + 10, 0x4a);
    select(frame, 1, 0, frame.data_4c + 1, 0x41);
    select(frame, 0, 1, frame.data_4c + 13, 0x4d);
    expect(frame.loaded_08 == 3 && frame.whole_data_48 == 2 && frame.index_ready_16 == 0,
        "resident whole flags changed");
    expect(screen_dialog_frame_cursor == 77 && screen_dialog_frame_file == -1,
        "resident whole touched file state");
    unchanged(frame.indices_44, indices);
    unchanged(frame.offsets_70, offsets);
    preserved_metadata(frame, 3, 24, 6);
    bytes(frame.data_4c, data.size(), 0x40);
    for (unsigned i = 0; i < 8; ++i)
    {
        expect(frame.plane_data_50[i] == nullptr && frame.plane_offsets_20[i] == 0,
            "resident whole touched plane banks");
    }
    screen_dialog_frame_cursor = 0;
}

void resident_planes()
{
    current_case = "resident planes";
    ++totals.sessions;
    FrameOwner owner;
    Frame& frame = *owner.frame;
    common_metadata(frame, 2, 2, 52);
    constexpr std::array<unsigned short, 4> indices{1, 0, 3, 2};
    constexpr std::array<unsigned int, 4> offsets{10, 13, 40, 45};
    constexpr std::array<unsigned char, 8> first{0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7};
    constexpr std::array<unsigned char, 12> second{
        0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb};
    frame.indices_44 = owned_copy(indices);
    frame.offsets_70 = owned_copy(offsets);
    frame.plane_data_50[0] = owned_copy(first);
    frame.plane_data_50[1] = owned_copy(second);
    frame.plane_offsets_20[0] = 10;
    frame.plane_offsets_20[1] = 40;
    frame.index_ready_16 = 1;
    frame.loaded_08 = 1;

    select(frame, 2, 2, frame.plane_data_50[1], 0xb0);
    select(frame, 0, 1, frame.plane_data_50[1] + 5, 0xb5);
    select(frame, 0, 0, frame.plane_data_50[0] + 3, 0xa3);
    expect(frame.loaded_08 == 1 && frame.whole_data_48 == 0 && frame.index_ready_16 == 1 &&
               frame.data_4c == nullptr && screen_dialog_frame_cursor == 0 &&
               screen_dialog_frame_file == -1,
        "resident plane state changed");
    expect(frame.plane_offsets_20[0] == 10 && frame.plane_offsets_20[1] == 40,
        "resident bases changed");
    unchanged(frame.indices_44, indices);
    unchanged(frame.offsets_70, offsets);
    preserved_metadata(frame, 2, 52, 4);
    bytes(frame.plane_data_50[0], first.size(), 0xa0);
    bytes(frame.plane_data_50[1], second.size(), 0xb0);
    for (unsigned i = 2; i < 8; ++i)
    {
        expect(frame.plane_data_50[i] == nullptr && frame.plane_offsets_20[i] == 0,
            "resident unused bank changed");
    }
}

void lazy_whole()
{
    current_case = "lazy small whole";
    ++totals.sessions;
    constexpr std::array<unsigned int, 4> offsets{0, 3, 6, 9};
    GeneratedFile file(offsets, 12, false);
    FrameOwner owner;
    Frame& frame = *owner.frame;
    common_metadata(frame, 2, 2, 12);
    const unsigned char* selected = frame.SelectData(2, 2);
    ++totals.calls;
    expect(frame.data_4c != nullptr && frame.indices_44 != nullptr && frame.offsets_70 != nullptr,
        "whole loader publications missing");
    expect(selected == frame.data_4c + 9 && *selected == 0x69, "lazy whole selected byte");
    expect(frame.loaded_08 == 1 && frame.whole_data_48 == 1 && frame.index_ready_16 == 0,
        "lazy whole flags");
    expect(screen_dialog_frame_cursor == 44 && file_position() == 44, "lazy whole cursor");
    const auto* data = frame.data_4c;
    const auto* indices = frame.indices_44;
    const auto* words = frame.offsets_70;
    select(frame, 0, 0, data, 0x60);
    expect(frame.data_4c == data && frame.indices_44 == indices && frame.offsets_70 == words &&
               screen_dialog_frame_cursor == 44 && file_position() == 44,
        "whole cache reloaded");
    unchanged(frame.indices_44, std::array<unsigned short, 4>{0, 1, 2, 3});
    unchanged(frame.offsets_70, offsets);
    preserved_metadata(frame, 2, 12, 4);
    bytes(frame.data_4c, 12, 0x60);
    for (unsigned i = 0; i < 8; ++i)
    {
        expect(frame.plane_data_50[i] == nullptr && frame.plane_offsets_20[i] == 0,
            "whole loader touched plane bank");
    }
}

void lazy_planes()
{
    current_case = "lazy threshold planes";
    ++totals.sessions;
    constexpr std::array<unsigned int, 4> offsets{0, 3, 64000, 64004};
    GeneratedFile file(offsets, 128000, true);
    FrameOwner owner;
    Frame& frame = *owner.frame;
    common_metadata(frame, 2, 2, 128000);
    const unsigned char* selected = frame.SelectData(1, 0);
    ++totals.calls;
    expect(frame.plane_data_50[0] != nullptr && frame.plane_data_50[1] == nullptr &&
               frame.data_4c == nullptr,
        "first plane publications");
    expect(selected == frame.plane_data_50[0] + 3 && *selected == 46, "first plane selected byte");
    expect(frame.loaded_08 == 1 && frame.whole_data_48 == 0 && frame.index_ready_16 == 1,
        "first plane flags");
    expect(frame.plane_offsets_20[0] == 0 && frame.plane_offsets_20[1] == 64000,
        "computed plane bases");
    expect(screen_dialog_frame_cursor == 64032 && file_position() == 64032, "first plane cursor");
    const auto* first = frame.plane_data_50[0];
    const auto* indices = frame.indices_44;
    const auto* words = frame.offsets_70;

    selected = frame.SelectData(99, 99);
    ++totals.calls;
    expect(frame.plane_data_50[1] != nullptr && frame.plane_data_50[0] == first &&
               frame.indices_44 == indices && frame.offsets_70 == words,
        "second plane reused metadata");
    expect(
        selected == frame.plane_data_50[1] + 4 && *selected == 245, "second plane selected byte");
    expect(
        screen_dialog_frame_cursor == 128032 && file_position() == 128032, "second plane cursor");
    const auto* second = frame.plane_data_50[1];
    select(frame, 0, 0, first, 7);
    select(frame, 1, 1, second + 4, 245);
    expect(frame.plane_data_50[0] == first && frame.plane_data_50[1] == second &&
               frame.indices_44 == indices && frame.offsets_70 == words &&
               screen_dialog_frame_cursor == 128032 && file_position() == 128032,
        "plane cache reloaded");
    unchanged(frame.indices_44, std::array<unsigned short, 4>{0, 1, 2, 3});
    unchanged(frame.offsets_70, offsets);
    preserved_metadata(frame, 2, 128000, 4);
    for (std::size_t i = 0; i < 64000; ++i)
    {
        totals.byte_checks += 2;
        expect(first[i] == file.expected[32 + i], "first plane data changed");
        expect(second[i] == file.expected[64032 + i], "second plane data changed");
    }
    for (unsigned i = 2; i < 8; ++i)
    {
        expect(frame.plane_data_50[i] == nullptr && frame.plane_offsets_20[i] == 0,
            "lazy unused plane bank changed");
    }
}
} // namespace

int main(int argc, char** argv)
{
    require(argc == 2, "expected generated file directory");
    generated_root = fs::path(argv[1]);
    expect(screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0,
        "actual global initializers");
    resident_whole();
    resident_planes();
    lazy_whole();
    lazy_planes();
    expect(totals.sessions == 4 && totals.calls == 12 && totals.constructors == 4 &&
               totals.destructors == 4 && totals.files == 2,
        "fixture protocol");
    expect(screen_dialog_frame_file == -1 && screen_dialog_frame_cursor == 0, "file state cleanup");
    if (failure)
    {
        std::fprintf(stderr, "resource frame select %s: %s\n", failed_case, failure);
        return 2;
    }
    std::printf(
        "resource frame select: %u sessions, %u calls, %u constructors, %u destructors, %u files, "
        "%zu checks, %zu byte checks\n",
        totals.sessions,
        totals.calls,
        totals.constructors,
        totals.destructors,
        totals.files,
        totals.checks,
        totals.byte_checks);
    return 0;
}

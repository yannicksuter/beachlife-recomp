#include "data/folder_source.h"
#include "runtime/retail_files.h"
#include "startup_screen_text.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
namespace fs = std::filesystem;
namespace data = beachlife::data;
namespace files = beachlife::runtime::retail_files;
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(int) == 4);
static_assert(std::endian::native == std::endian::little);
// Initial qualification is Linux x86-64. These are host sizes, not x86 retail sizes.
static_assert(sizeof(void*) == 8 && sizeof(ScreenTextValue) == 16);
static_assert(sizeof(ScreenTextBucket) == 24 && sizeof(ScreenTextTable) == 32);
static_assert(sizeof(ScreenTextLookup) == 72056 && alignof(ScreenTextLookup) == 8);

std::size_t checks = 0;
std::size_t lifetimes = 0;
std::size_t destructions = 0;
std::size_t lookup_calls = 0;
std::size_t find_calls = 0;

void expect(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class GeneratedLanguage
{
  public:
    explicit GeneratedLanguage(const fs::path& root)
        : root_(root)
    {
        expect(!fs::exists(root_), "generated language directory already exists");
        fs::create_directories(root_);
        add("empty.ble", u"\ufeff");
        // Three records, including a duplicate key and UTF-16 text that must not
        // pass through the ASCII/ACP key conversion. The leading/trailing and
        // inter-record unquoted units exercise the actual cursor loop.
        add("records.ble",
            u"\ufeffignored\n\"A\" \"first\"\n\"A\" \"second\"\n"
            u"\"B\" \"\u03a9\U0001f642\"tail");
    }
    ~GeneratedLanguage()
    {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    GeneratedLanguage(const GeneratedLanguage&) = delete;
    GeneratedLanguage& operator=(const GeneratedLanguage&) = delete;

    std::shared_ptr<const data::FileSource> mount() const
    {
        return std::make_shared<data::FolderSource>(root_, entries_);
    }

  private:
    void add(const char* name, std::u16string_view decoded)
    {
        std::vector<unsigned char> encoded;
        for (char16_t unit : decoded)
        {
            encoded.push_back(static_cast<unsigned char>(~static_cast<unsigned>(unit)));
            encoded.push_back(static_cast<unsigned char>(~(static_cast<unsigned>(unit) >> 8)));
        }
        std::ofstream output(root_ / name, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size()));
        output.close();
        expect(static_cast<bool>(output), "cannot write generated language file");
        entries_.push_back({name, encoded.size()});
    }
    fs::path root_;
    std::vector<data::SourceEntry> entries_;
};

constexpr std::uint64_t guard = 0x52d4186bb00ff77aULL;
constexpr unsigned char preserved = 0x6d;

// Real complete lookup lifetime in guarded storage. Only pre-lifetime bytes are
// initialized; neither a live owner nor a vptr is overwritten. The root's future
// optimized setup gate must bind this volatile loop and the real constructor.
class Owner
{
  public:
    explicit Owner(const char* name)
    {
        before_.fill(guard);
        after_.fill(guard);
        volatile unsigned char* observable = storage_.data();
        for (std::size_t index = 0; index < storage_.size(); ++index)
        {
            observable[index] = preserved;
        }
        value_ = ::new (static_cast<void*>(storage_.data())) ScreenTextLookup(name);
        ++lifetimes;
    }
    ~Owner()
    {
        destroy();
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;

    ScreenTextLookup& get()
    {
        return *value_;
    }
    void destroy()
    {
        if (value_ != nullptr)
        {
            value_->~ScreenTextLookup();
            value_ = nullptr;
            ++destructions;
        }
    }
    void check_guards() const
    {
        for (auto value : before_)
        {
            expect(value == guard, "lookup leading guard changed");
        }
        for (auto value : after_)
        {
            expect(value == guard, "lookup trailing guard changed");
        }
    }
    void check_layout()
    {
        auto& value = get();
        const auto base = reinterpret_cast<std::uintptr_t>(&value);
        const auto offset = [base](const void* member)
        {
            return reinterpret_cast<std::uintptr_t>(member) - base;
        };
        expect(offset(&value.end_index) == 0 && offset(&value.capacity_04) == 4,
            "native bucket-array scalar offsets differ");
        expect(offset(&value.buckets) == 8 && offset(value.unknown_0c) == 16,
            "native bucket pointer/opaque offsets differ");
        expect(offset(&value.bucket_count) == 20 && offset(&value.used_14) == 24,
            "native table count offsets differ");
        expect(offset(&value.file_18) == 32 && offset(&value.index_1c) == 40,
            "native complete-owner prefix offsets differ");
        expect(offset(value.values) == 48 && offset(&value.path_8cc0) == 72048,
            "native value-array/path offsets differ");
        expect(reinterpret_cast<void*>(static_cast<ScreenTextTable*>(&value)) == &value,
            "native table subobject no longer starts at zero");
        for (unsigned char byte : value.unknown_0c)
        {
            expect(byte == preserved, "constructor changed preserved opaque table member");
        }
        // file_18 deliberately retains the freed BLE-array representation. Do
        // not evaluate or dereference that invalid pointer after construction.
        check_guards();
    }

  private:
    std::array<std::uint64_t, 2> before_{};
    alignas(ScreenTextLookup) std::array<unsigned char, sizeof(ScreenTextLookup)> storage_{};
    std::array<std::uint64_t, 2> after_{};
    ScreenTextLookup* value_ = nullptr;
};

const unsigned short* lookup(ScreenTextLookup& owner, const char* key)
{
    ++lookup_calls;
    return owner.LookupText(key);
}

void text_equals(const unsigned short* actual, std::span<const unsigned short> expected)
{
    expect(actual != nullptr, "lookup text is null");
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        expect(actual[index] == expected[index], "UTF-16 text differs from literal");
    }
}

void check_table(ScreenTextLookup& owner, bool populated)
{
    expect(owner.end_index == 6151 && owner.bucket_count == 6151 && owner.capacity_04 == 8192,
        "real prime selection/table growth differs");
    expect(owner.used_14 == (populated ? 3U : 0U) && owner.index_1c == (populated ? 3 : 0),
        "parsed value/table counts differ");
    expect(std::strcmp(owner.path_8cc0, populated ? "records.ble" : "empty.ble") == 0,
        "owned language extension/path differs");
    for (int index = 0; index < 8192; ++index)
    {
        const bool occupied = populated && index >= 65 && index <= 67;
        if (occupied)
        {
            const auto record = index - 65;
            expect(owner.buckets[index].key == owner.values[record].key &&
                       owner.buckets[index].value == &owner.values[record],
                "literal hash/probe publication differs");
        }
        else
        {
            expect(owner.buckets[index].key == nullptr, "unused bucket key was not constructed");
        }
        if (index < 6151)
        {
            expect(owner.buckets[index].unavailable == (occupied ? 0 : 1),
                "live bucket availability differs");
        }
        // Unused value pointers and availability outside end_index were never
        // initialized by retail construction, so they are not read here.
    }
    for (int index = populated ? 3 : 0; index < 4500; ++index)
    {
        expect(owner.values[index].key == nullptr && owner.values[index].text == nullptr,
            "unused owner value was not cleared");
    }
}

constexpr unsigned short first[] = {'f', 'i', 'r', 's', 't', 0};
constexpr unsigned short second[] = {'s', 'e', 'c', 'o', 'n', 'd', 0};
constexpr unsigned short unicode[] = {0x03a9, 0xd83d, 0xde42, 0};

void run_empty()
{
    Owner owner("empty.txt");
    expect(screen_text_lookup == &owner.get(), "language owner publication differs");
    owner.check_layout();
    check_table(owner.get(), false);
    expect(lookup(owner.get(), nullptr) == screen_text_empty, "null lookup fallback differs");
    expect(lookup(owner.get(), "absent") == screen_text_empty, "missing lookup fallback differs");
    owner.destroy();
    expect(screen_text_lookup == nullptr, "empty owner did not unpublish");
    owner.check_guards();
}

void run_records()
{
    Owner owner("records.txt");
    expect(screen_text_lookup == &owner.get(), "populated owner publication differs");
    owner.check_layout();
    auto& value = owner.get();
    check_table(value, true);
    expect(std::strcmp(value.values[0].key, "A") == 0 &&
               std::strcmp(value.values[1].key, "A") == 0 &&
               std::strcmp(value.values[2].key, "B") == 0,
        "ASCII key conversion differs");
    expect(
        value.values[0].key != value.values[1].key && value.values[0].text != value.values[1].text,
        "duplicate entries unexpectedly share owned storage");
    text_equals(value.values[1].text, second);
    text_equals(lookup(value, "A"), first);
    text_equals(lookup(value, "B"), unicode);
    text_equals(lookup(value, "a"), first);
    expect(lookup(value, "C") == screen_text_empty, "missing populated lookup differs");
    ++find_calls;
    const auto found_a = value.FindText(ScreenTextKey("A"));
    ++find_calls;
    const auto found_b = value.FindText(ScreenTextKey("B"));
    expect(found_a.bucket == value.buckets + 65 && found_a.lookup == &value,
        "duplicate key did not select first literal bucket");
    expect(found_b.bucket == value.buckets + 67 && found_b.lookup == &value,
        "colliding B key did not select third literal bucket");
    owner.destroy();
    expect(screen_text_lookup == nullptr, "populated owner did not unpublish");
    owner.check_guards();
}

void run_registration_order()
{
    Owner older("empty.txt");
    older.check_layout();
    Owner younger("records.txt");
    younger.check_layout();
    expect(screen_text_lookup == &younger.get(), "newer owner publication differs");
    older.destroy();
    expect(screen_text_lookup == nullptr, "older owner no longer clears newer registration");
    text_equals(lookup(younger.get(), "A"), first);
    younger.destroy();
    expect(screen_text_lookup == nullptr, "newer owner final registration differs");
    older.check_guards();
    younger.check_guards();
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        expect(argc == 2, "usage: reconstructed_text_lookup_owner_tests GENERATED_DIRECTORY");
        expect(screen_text_lookup == nullptr, "language global is not initially null");
        GeneratedLanguage generated(argv[1]);
        files::Session session(generated.mount());
        run_empty();
        run_records();
        run_registration_order();
        expect(lifetimes == 4 && destructions == 4, "genuine owner lifetimes did not balance");
        expect(lookup_calls == 7 && find_calls == 2, "focused lookup call counts differ");
        std::cout << "{\"groups\":3,\"owners\":" << lifetimes
                  << ",\"destructions\":" << destructions << ",\"lookup_calls\":" << lookup_calls
                  << ",\"find_calls\":" << find_calls << ",\"checks\":" << checks << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}

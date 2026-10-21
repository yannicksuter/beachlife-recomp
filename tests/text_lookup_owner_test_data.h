#pragma once

#include "data/folder_source.h"
#include "runtime/retail_files.h"
#include "startup_screen_text.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace text_owner_test
{
namespace fs = std::filesystem;
namespace files = beachlife::runtime::retail_files;

// Declare this member before the real ScreenTextLookup value. Its nested VFS
// binding exists only during actual construction and restores an outer font or
// resource mount before the fixture starts exercising the existing methods.
class Bootstrap
{
  public:
    Bootstrap()
        : previous_(screen_text_lookup)
    {
        static std::atomic<unsigned long long> serial{0};
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root_ = fs::temp_directory_path() / ("beachlife-language-" + std::to_string(stamp) +
                                                    "-" + std::to_string(serial.fetch_add(1)));
            if (fs::create_directory(root_))
            {
                created_ = true;
                break;
            }
        }
        if (!created_)
        {
            throw std::runtime_error("cannot create generated language directory");
        }
        // Complemented little-endian FEFF, with no quoted records.
        const char encoded[2] = {0, 1};
        std::ofstream file(root_ / "owner.ble", std::ios::binary | std::ios::trunc);
        file.write(encoded, sizeof(encoded));
        file.close();
        if (!file)
        {
            cleanup();
            throw std::runtime_error("cannot write generated language bootstrap");
        }
        auto source = std::make_shared<beachlife::data::FolderSource>(
            root_, std::vector<beachlife::data::SourceEntry>{{"owner.ble", sizeof(encoded)}});
        session_ = std::make_unique<files::Session>(std::move(source));
    }
    ~Bootstrap()
    {
        cleanup();
        // Fixture binding restoration is separate from the real destructor's
        // unconditional clear, which has already run by member destruction order.
        screen_text_lookup = previous_;
    }
    Bootstrap(const Bootstrap&) = delete;
    Bootstrap& operator=(const Bootstrap&) = delete;

    void finish(ScreenTextLookup& value)
    {
        if (screen_text_lookup != &value || value.index_1c != 0 || value.used_14 != 0 ||
            value.end_index != 6151 || value.bucket_count != 6151 || value.capacity_04 != 8192)
        {
            throw std::runtime_error("genuine empty language owner bootstrap differs");
        }
        cleanup();
        screen_text_lookup = previous_;
    }

  private:
    void cleanup() noexcept
    {
        session_.reset();
        if (created_)
        {
            std::error_code ignored;
            fs::remove_all(root_, ignored);
            created_ = false;
        }
    }
    ScreenTextLookup* previous_;
    fs::path root_;
    bool created_ = false;
    std::unique_ptr<files::Session> session_;
};

// Declare after the real lookup (and its trailing guard). Existing method tests
// may install a borrowed table view, but this member restores the genuine owned
// array before the real lookup/table destructors run. No payload is fabricated
// or freed by the fixture; the constructor's empty values and owned path remain.
class RestoreOwnedTable
{
  public:
    RestoreOwnedTable(ScreenTextLookup& value, Bootstrap& bootstrap)
        : value_(value)
        , end_(value.end_index)
        , capacity_(value.capacity_04)
        , buckets_(value.buckets)
        , count_(value.bucket_count)
        , used_(value.used_14)
        , index_(value.index_1c)
    {
        std::fill(std::begin(value.unknown_0c), std::end(value.unknown_0c), 0);
        bootstrap.finish(value);
    }
    ~RestoreOwnedTable()
    {
        value_.end_index = end_;
        value_.capacity_04 = capacity_;
        value_.buckets = buckets_;
        value_.bucket_count = count_;
        value_.used_14 = used_;
        value_.index_1c = index_;
    }
    RestoreOwnedTable(const RestoreOwnedTable&) = delete;
    RestoreOwnedTable& operator=(const RestoreOwnedTable&) = delete;

  private:
    ScreenTextLookup& value_;
    int end_;
    int capacity_;
    ScreenTextBucket* buckets_;
    int count_;
    unsigned used_;
    int index_;
};

inline ScreenTextBucket bucket(const char* key, ScreenTextValue* value, unsigned char unavailable)
{
    ScreenTextBucket result;
    result.key = key;
    result.value = value;
    result.unavailable = unavailable;
    return result;
}

// Only the explicitly borrowed read view is copied; never copy an owning lookup.
inline void copy_view(ScreenTextLookup& to, const ScreenTextLookup& from)
{
    to.end_index = from.end_index;
    to.capacity_04 = from.capacity_04;
    to.buckets = from.buckets;
    std::copy(std::begin(from.unknown_0c), std::end(from.unknown_0c), to.unknown_0c);
    to.bucket_count = from.bucket_count;
    to.used_14 = from.used_14;
}

// A separate non-owning type captures every initialized member. The freed BLE
// pointer is captured only as bytes; neither snapshots nor comparisons evaluate
// that invalid pointer value, and no ABI padding or uninitialized bucket is read.
struct Snapshot
{
    int end_index;
    int capacity_04;
    ScreenTextBucket* buckets;
    std::array<unsigned char, 4> unknown_0c;
    int bucket_count;
    unsigned used_14;
    std::array<unsigned char, sizeof(char*)> file_bytes;
    int index_1c;
    std::array<ScreenTextValue, 4500> values;
    char* path_8cc0;
    std::string path;

    explicit Snapshot(const ScreenTextLookup& value)
        : end_index(value.end_index)
        , capacity_04(value.capacity_04)
        , buckets(value.buckets)
        , bucket_count(value.bucket_count)
        , used_14(value.used_14)
        , index_1c(value.index_1c)
        , path_8cc0(value.path_8cc0)
        , path(value.path_8cc0)
    {
        std::copy(std::begin(value.unknown_0c), std::end(value.unknown_0c), unknown_0c.begin());
        std::memcpy(file_bytes.data(), &value.file_18, file_bytes.size());
        std::copy(std::begin(value.values), std::end(value.values), values.begin());
    }
};

inline bool equal(const ScreenTextLookup& value, const Snapshot& before)
{
    std::array<unsigned char, sizeof(char*)> file_bytes;
    std::memcpy(file_bytes.data(), &value.file_18, file_bytes.size());
    if (value.end_index != before.end_index || value.capacity_04 != before.capacity_04 ||
        value.buckets != before.buckets || value.bucket_count != before.bucket_count ||
        value.used_14 != before.used_14 || value.index_1c != before.index_1c ||
        value.path_8cc0 != before.path_8cc0 || before.path != value.path_8cc0 ||
        file_bytes != before.file_bytes ||
        !std::equal(
            std::begin(value.unknown_0c), std::end(value.unknown_0c), before.unknown_0c.begin()))
    {
        return false;
    }
    for (std::size_t i = 0; i < before.values.size(); ++i)
    {
        if (value.values[i].key != before.values[i].key ||
            value.values[i].text != before.values[i].text)
        {
            return false;
        }
    }
    return true;
}

template <class Guard> struct FrameSnapshot
{
    Guard before;
    Snapshot value;
    Guard after;

    template <class Frame>
    explicit FrameSnapshot(const Frame& frame)
        : before(frame.before)
        , value(frame.value)
        , after(frame.after)
    {
    }
};
} // namespace text_owner_test

#include "startup_screen_dialog_resource.h"

#include <array>
#include <climits>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

namespace
{
static_assert(sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<ScreenDialogResourceRecord>);
static_assert(std::is_standard_layout_v<ScreenDialogResourceLookup>);
static_assert(!std::is_polymorphic_v<ScreenDialogResource>);
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8);
static_assert(sizeof(ScreenDialogResourceRecord) == (sizeof(void*) == 8 ? 56 : 36));
static_assert(sizeof(ScreenDialogResourceLookup) == (sizeof(void*) == 8 ? 32 : 20));
static_assert(offsetof(ScreenDialogResourceLookup, record_count) == (sizeof(void*) == 8 ? 16 : 12));
static_assert(offsetof(ScreenDialogResourceLookup, records) == (sizeof(void*) == 8 ? 24 : 16));

constexpr std::array<std::uint32_t, 4> before_guard{
    0x12345678U, 0x9abcdef0U, 0x16543210U, 0xfedcba98U};
constexpr std::array<std::uint32_t, 4> after_guard{
    0x87654321U, 0x0fedcba9U, 0x01234561U, 0x89abcdefU};

template <class T> struct Guarded
{
    std::array<std::uint32_t, 4> before = before_guard;
    T value{};
    std::array<std::uint32_t, 4> after = after_guard;

    bool intact() const
    {
        return before == before_guard && after == after_guard;
    }
};

using Name = std::array<char, 32>;

struct Call
{
    const char* request;
    bool through_lookup;
    std::size_t expected_record;
    std::array<unsigned int, 3> expected_counts;
    const char* expected_name;
};

struct Case
{
    const char* label;
    std::array<const char*, 3> names;
    std::array<unsigned int, 3> counts;
    std::array<std::size_t, 3> record_indices;
    int count;
    std::array<Call, 2> calls;
    std::size_t call_count;
};

// Fixed literals from D228. No scanner, search or allocation oracle is implemented.
constexpr std::array<Case, 7> cases{{
    {"exact-first",
        {"Alpha", "Beta", "unused"},
        {20, 7, 90},
        {0, 1, 2},
        2,
        {{{"Alpha", true, 0, {21, 7, 90}, "Alpha"}, {}}},
        1},
    {"first-duplicate-casefold",
        {"Alpha", "aLPHa", "dummy"},
        {3, 8, 4},
        {0, 1, 2},
        3,
        {{{"ALPHA", false, 0, {4, 8, 4}, "ALPHA"}, {}}},
        1},
    {"later-casefold",
        {"skip", "Target", "unused"},
        {9, 12, 90},
        {0, 1, 2},
        2,
        {{{"tArGeT", true, 1, {9, 13, 90}, "tArGeT"}, {}}},
        1},
    {"recursive-fallback",
        {"other", "dummy", "unused"},
        {1, 3, 90},
        {0, 1, 2},
        2,
        {{{"missing", false, 1, {1, 4, 90}, "dummy"}, {}}},
        1},
    {"empty-request",
        {"", "dummy", "unused"},
        {0, 4, 90},
        {0, 1, 2},
        2,
        {{{"", true, 0, {1, 4, 90}, ""}, {}}},
        1},
    {"same-record-two-calls",
        {"shared", "unused", "unused"},
        {5, 90, 91},
        {0, 0, 2},
        2,
        {{{"shared", false, 0, {6, 90, 91}, "shared"}, {"SHARED", true, 0, {7, 90, 91}, "SHARED"}}},
        2},
    {"count-wrap",
        {"wrap", "unused", "unused"},
        {UINT_MAX, 90, 91},
        {0, 1, 2},
        1,
        {{{"wrap", false, 0, {0, 90, 91}, "wrap"}, {}}},
        1},
}};

std::size_t checks = 0;
std::size_t calls = 0;
std::size_t direct_calls = 0;
std::size_t lookup_calls = 0;
std::size_t record_lifetimes = 0;
std::size_t owned_names = 0;

bool check(bool value)
{
    ++checks;
    return value;
}

void set_name(Name& destination, const char* source)
{
    destination.fill('\x6d');
    const auto count = std::strlen(source) + 1;
    std::memcpy(destination.data(), source, count);
}

bool initialized_members(const ScreenDialogResource& resource)
{
    // Read only the constructor's initialized typed members. Unknown bytes and
    // ABI padding are not inspected, and no pre-lifetime pattern is assumed.
    return resource.word_04 == 0 && resource.word_08 == 0 && resource.word_0c == 18 &&
           resource.word_10 == 0 && resource.word_14 == 0 && resource.word_18 == 0 &&
           resource.word_1c == 0 && resource.flag_20 == 0 && resource.word_24 == 0 &&
           resource.flag_28 == 0 && resource.flag_29 == 0 && resource.flag_2a == 0 &&
           resource.word_2c == 0 && resource.flag_34 == 0;
}

bool record_members(const ScreenDialogResourceRecord& record)
{
    return record.frame_count == 0 && record.frames == nullptr && record.palette_count == 0 &&
           record.palettes == nullptr && record.format_18 == 1 && record.word_1c == 0;
}

bool run_case(const Case& example)
{
    std::array<Guarded<Name>, 3> names;
    std::array<Guarded<Name>, 2> requests;
    std::array<Guarded<ScreenDialogResourceRecord>, 3> records;
    std::array<const char*, 3> record_names{};
    Guarded<std::array<ScreenDialogResourceRecord*, 3>> entries;
    Guarded<ScreenDialogResourceLookup> lookup;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        set_name(names[i].value, example.names[i]);
        // Guarded::value invokes the real Record constructor. Each record owns
        // a distinct scalar name, even when the literal string contents agree.
        char* owned = static_cast<char*>(::operator new(names[i].value.size()));
        std::memcpy(owned, names[i].value.data(), names[i].value.size());
        records[i].value.name = record_names[i] = owned;
        records[i].value.reference_count = example.counts[i];
        for (std::size_t n = 0; n < sizeof(records[i].value.unknown_20); ++n)
        {
            records[i].value.unknown_20[n] = static_cast<unsigned char>(0x70 + i + n);
        }
        ++record_lifetimes;
        ++owned_names;
        entries.value[i] = &records[example.record_indices[i]].value;
    }
    for (std::size_t i = 0; i < example.call_count; ++i)
    {
        set_name(requests[i].value, example.calls[i].request);
    }
    lookup.value.metadata_file = nullptr;
    lookup.value.data_file = -1;
    std::array<unsigned char, 4> opaque;
    for (std::size_t i = 0; i < opaque.size(); ++i)
    {
        lookup.value.unknown_08[i] = opaque[i] = static_cast<unsigned char>(0x30 + i);
    }
    lookup.value.record_count = example.count;
    lookup.value.records = entries.value.data();
    const auto saved_names = names;
    const auto saved_requests = requests;
    const auto saved_entries = entries.value;
    std::array<ScreenDialogResource*, 2> results{};
    bool okay = true;
    for (std::size_t i = 0; i < example.call_count; ++i)
    {
        const auto& expected = example.calls[i];
        char* request = requests[i].value.data();
        if (expected.through_lookup)
        {
            ++lookup_calls;
            results[i] = lookup.value.Lookup(request);
        }
        else
        {
            ++direct_calls;
            results[i] = lookup.value.Find(request);
        }
        ++calls;
        ScreenDialogResource* result = results[i];
        okay &= check(result != nullptr);
        if (result)
        {
            okay &= check(result->record == &records[expected.expected_record].value);
            okay &= check(initialized_members(*result));
            okay &= check(result->name != nullptr);
            if (result->name)
            {
                okay &= check(std::strcmp(result->name, expected.expected_name) == 0);
                okay &= check(result->name != request);
                for (const auto& name : names)
                {
                    okay &= check(result->name != name.value.data());
                }
                for (const char* name : record_names)
                {
                    okay &= check(result->name != name);
                }
            }
        }
        for (std::size_t n = 0; n < records.size(); ++n)
        {
            okay &= check(records[n].value.reference_count == expected.expected_counts[n]);
            okay &= check(records[n].value.name == record_names[n]);
            okay &= check(records[n].intact());
            okay &= check(names[n].intact() && names[n].value == saved_names[n].value);
            okay &= check(std::memcmp(record_names[n],
                              saved_names[n].value.data(),
                              saved_names[n].value.size()) == 0);
            okay &= check(record_members(records[n].value));
            for (std::size_t b = 0; b < sizeof(records[n].value.unknown_20); ++b)
            {
                okay &= check(
                    records[n].value.unknown_20[b] == static_cast<unsigned char>(0x70 + n + b));
            }
        }
        for (std::size_t n = 0; n < requests.size(); ++n)
        {
            okay &= check(requests[n].intact() && requests[n].value == saved_requests[n].value);
        }
        okay &= check(lookup.intact() && entries.intact());
        okay &= check(lookup.value.record_count == example.count);
        okay &= check(lookup.value.metadata_file == nullptr && lookup.value.data_file == -1);
        okay &=
            check(lookup.value.records == entries.value.data() && entries.value == saved_entries);
        for (std::size_t n = 0; n < opaque.size(); ++n)
        {
            okay &= check(lookup.value.unknown_08[n] == opaque[n]);
        }
    }
    if (example.call_count == 2 && results[0] && results[1])
    {
        okay &= check(results[0] != results[1]);
        okay &= check(results[0]->name != results[1]->name);
        okay &= check(results[0]->record == results[1]->record);
    }
    // Both wrappers stay live until all pointer-identity observations finish.
    // The real destructor frees its scalar name; delete also releases the wrapper.
    // It deliberately does not decrement the borrowed record's count.
    for (ScreenDialogResource* resource : results)
    {
        delete resource;
    }
    const auto& final_counts = example.calls[example.call_count - 1].expected_counts;
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        okay &= check(records[i].value.reference_count == final_counts[i]);
        okay &= check(records[i].value.name == record_names[i]);
        okay &= check(records[i].intact() && names[i].intact());
        okay &= check(names[i].value == saved_names[i].value);
        okay &= check(
            std::memcmp(
                record_names[i], saved_names[i].value.data(), saved_names[i].value.size()) == 0);
        okay &= check(record_members(records[i].value));
        for (std::size_t b = 0; b < sizeof(records[i].value.unknown_20); ++b)
        {
            okay &=
                check(records[i].value.unknown_20[b] == static_cast<unsigned char>(0x70 + i + b));
        }
    }
    okay &= check(lookup.intact() && entries.intact());
    okay &= check(lookup.value.metadata_file == nullptr && lookup.value.data_file == -1);
    // Real Record destructors now release the independently owned record names.
    // All borrowed wrapper observations ended above; no record/name read follows
    // destruction, and the preserved nonzero reference counts do not block it.
    return okay;
}
} // namespace

int main()
{
    if (!std::setlocale(LC_CTYPE, "C"))
    {
        return 3;
    }
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        if (!run_case(cases[i]))
        {
            std::fprintf(
                stderr, "case %zu (%s): resource lookup result differs\n", i, cases[i].label);
            return 2;
        }
    }
    std::printf(
        "{\"sessions\":%zu,\"calls\":%zu,\"direct_find\":%zu,\"lookup\":%zu,"
        "\"record_lifetimes\":%zu,\"owned_names\":%zu,\"checks\":%zu,"
        "\"record_size\":%zu,\"lookup_size\":%zu,\"count_offset\":%zu,\"records_offset\":%zu}\n",
        cases.size(),
        calls,
        direct_calls,
        lookup_calls,
        record_lifetimes,
        owned_names,
        checks,
        sizeof(ScreenDialogResourceRecord),
        sizeof(ScreenDialogResourceLookup),
        offsetof(ScreenDialogResourceLookup, record_count),
        offsetof(ScreenDialogResourceLookup, records));
}

#include "startup_screen_dialog_resource.h"

#include <array>
#include <climits>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace
{
static_assert(sizeof(unsigned int) == 4);
static_assert(std::is_standard_layout_v<ScreenDialogResourceRecord>);
static_assert(std::is_standard_layout_v<ScreenDialogResourceLookup>);
static_assert(!std::is_polymorphic_v<ScreenDialogResource>);

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

bool run_case(const Case& example)
{
    std::array<Guarded<Name>, 3> names;
    std::array<Guarded<Name>, 2> requests;
    std::array<Guarded<ScreenDialogResourceRecord>, 3> records;
    Guarded<std::array<ScreenDialogResourceRecord*, 3>> entries;
    Guarded<ScreenDialogResourceLookup> lookup;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        set_name(names[i].value, example.names[i]);
        records[i].value.name = names[i].value.data();
        records[i].value.reference_count = example.counts[i];
        entries.value[i] = &records[example.record_indices[i]].value;
    }
    for (std::size_t i = 0; i < example.call_count; ++i)
    {
        set_name(requests[i].value, example.calls[i].request);
    }
    std::array<unsigned char, 12> opaque;
    for (std::size_t i = 0; i < opaque.size(); ++i)
    {
        lookup.value.unknown_00[i] = opaque[i] = static_cast<unsigned char>(0x30 + i);
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
            }
        }
        for (std::size_t n = 0; n < records.size(); ++n)
        {
            okay &= check(records[n].value.reference_count == expected.expected_counts[n]);
            okay &= check(records[n].value.name == names[n].value.data());
            okay &= check(records[n].intact());
            okay &= check(names[n].intact() && names[n].value == saved_names[n].value);
        }
        for (std::size_t n = 0; n < requests.size(); ++n)
        {
            okay &= check(requests[n].intact() && requests[n].value == saved_requests[n].value);
        }
        okay &= check(lookup.intact() && entries.intact());
        okay &= check(lookup.value.record_count == example.count);
        okay &=
            check(lookup.value.records == entries.value.data() && entries.value == saved_entries);
        for (std::size_t n = 0; n < opaque.size(); ++n)
        {
            okay &= check(lookup.value.unknown_00[n] == opaque[n]);
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
        okay &= check(records[i].value.name == names[i].value.data());
        okay &= check(records[i].intact() && names[i].intact());
        okay &= check(names[i].value == saved_names[i].value);
    }
    okay &= check(lookup.intact() && entries.intact());
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
    std::printf("{\"sessions\":%zu,\"calls\":%zu,\"direct_find\":%zu,\"lookup\":%zu,"
                "\"checks\":%zu}\n",
        cases.size(),
        calls,
        direct_calls,
        lookup_calls,
        checks);
}

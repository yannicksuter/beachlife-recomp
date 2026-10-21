#include "startup_screen_text.h"
#include "text_lookup_owner_test_data.h"
#include <algorithm>
#include <array>
#include <climits>
#include <clocale>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(unsigned short) == 2 && sizeof(unsigned int) == 4);
static_assert(sizeof(int) == 4 && INT_MAX == 2147483647);
// Raw cleanup snapshots cover exactly initialized pointer members, with no ABI gaps.
static_assert(std::is_standard_layout_v<ScreenTextKey>);
static_assert(sizeof(ScreenTextKey) == sizeof(const char*));
static_assert(offsetof(ScreenTextKey, text) == 0);
static_assert(std::is_standard_layout_v<ScreenTextIterator>);
static_assert(offsetof(ScreenTextIterator, bucket) == 0);
static_assert(offsetof(ScreenTextIterator, lookup) == sizeof(ScreenTextBucket*));
static_assert(sizeof(ScreenTextIterator) == sizeof(ScreenTextBucket*) + sizeof(ScreenTextLookup*));
static_assert(std::is_same_v<decltype(&ScreenTextLookup::LookupText),
    const unsigned short* (ScreenTextLookup::*)(const char*)>);
static_assert(std::is_same_v<decltype(&ScreenTextLookup::FindText),
    ScreenTextIterator (ScreenTextLookup::*)(const ScreenTextKey&)>);
static_assert(std::is_same_v<decltype(&ScreenTextLookup::EndText),
    ScreenTextIterator (ScreenTextLookup::*)()>);

constexpr std::size_t capacity = 7;
constexpr std::size_t key_capacity = 16;
using Guard = std::array<unsigned char, 16>;
struct Failure
{
};
struct HostUnwind
{
};
const char* category = "setup";
const char* case_name = "initial";
std::size_t case_index = 0;
unsigned int checks = 0;
unsigned int sessions = 0;
unsigned int lookup_calls = 0;
unsigned int find_calls = 0;
unsigned int end_calls = 0;
unsigned int cleanup_scopes = 0;

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr,
            "screen-localization: %s case %zu %s: %s\n",
            category,
            case_index,
            case_name,
            reason);
        throw Failure{};
    }
}

Guard guard(unsigned int seed)
{
    Guard bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] = static_cast<unsigned char>(seed + 17U * i);
    }
    return bytes;
}

template <class T> struct Frame
{
    Guard before = guard(0x17);
    T value{};
    Guard after = guard(0xe3);
};

// A genuine owner with a scoped borrowed table view for these method cases.
template <> struct Frame<ScreenTextLookup>
{
    text_owner_test::Bootstrap bootstrap;
    Guard before = guard(0x17);
    ScreenTextLookup value{"owner.txt"};
    Guard after = guard(0xe3);
    text_owner_test::RestoreOwnedTable restore{value, bootstrap};
};

template <class A, class B> void verify_guards(const A& actual, const B& before)
{
    require(actual.before == before.before, "leading guard changed");
    require(actual.after == before.after, "trailing guard changed");
}

bool same_lookup(const ScreenTextLookup& a, const text_owner_test::Snapshot& b)
{
    return text_owner_test::equal(a, b);
}

bool same_bucket(const ScreenTextBucket& a, const ScreenTextBucket& b)
{
    return a.key == b.key && a.value == b.value && a.unavailable == b.unavailable;
}

struct Environment
{
    Frame<ScreenTextLookup> lookup;
    Frame<ScreenTextLookup> decoy;
    Frame<std::array<ScreenTextBucket, capacity>> buckets;
    Frame<std::array<ScreenTextValue, capacity>> values;
    Frame<std::array<std::array<char, key_capacity>, capacity>> keys;
    Frame<std::array<std::array<unsigned short, 4>, capacity>> texts;
    Frame<std::array<char, key_capacity>> query;
};

struct EnvironmentSnapshot
{
    text_owner_test::FrameSnapshot<Guard> lookup;
    text_owner_test::FrameSnapshot<Guard> decoy;
    decltype(Environment::buckets) buckets;
    decltype(Environment::values) values;
    decltype(Environment::keys) keys;
    decltype(Environment::texts) texts;
    decltype(Environment::query) query;

    explicit EnvironmentSnapshot(const Environment& env)
        : lookup(env.lookup)
        , decoy(env.decoy)
        , buckets(env.buckets)
        , values(env.values)
        , keys(env.keys)
        , texts(env.texts)
        , query(env.query)
    {
    }
};

void pattern_lookup(ScreenTextLookup& lookup)
{
    lookup.capacity_04 = static_cast<int>(capacity);
    std::fill(std::begin(lookup.unknown_0c), std::end(lookup.unknown_0c), 0x81);
    lookup.used_14 = 0xeeU;
}

void verify_environment(const Environment& actual, const EnvironmentSnapshot& before)
{
    verify_guards(actual.lookup, before.lookup);
    verify_guards(actual.decoy, before.decoy);
    verify_guards(actual.buckets, before.buckets);
    verify_guards(actual.values, before.values);
    verify_guards(actual.keys, before.keys);
    verify_guards(actual.texts, before.texts);
    verify_guards(actual.query, before.query);
    require(same_lookup(actual.lookup.value, before.lookup.value), "receiver changed");
    require(same_lookup(actual.decoy.value, before.decoy.value), "global decoy changed");
    for (std::size_t i = 0; i < capacity; ++i)
    {
        require(same_bucket(actual.buckets.value[i], before.buckets.value[i]), "bucket changed");
        require(actual.values.value[i].key == before.values.value[i].key &&
                    actual.values.value[i].text == before.values.value[i].text,
            "borrowed value changed");
    }
    require(actual.keys.value == before.keys.value, "stored key bytes changed");
    require(actual.texts.value == before.texts.value, "borrowed text changed");
    require(actual.query.value == before.query.value, "query bytes changed");
    require(screen_text_lookup == &actual.decoy.value, "global lookup owner changed");
    require(screen_text_empty[0] == 0, "static empty storage changed");
}

struct GlobalSelection
{
    ScreenTextLookup* previous = screen_text_lookup;
    explicit GlobalSelection(ScreenTextLookup* value)
    {
        screen_text_lookup = value;
    }
    ~GlobalSelection()
    {
        screen_text_lookup = previous;
    }
};

struct Slot
{
    std::size_t index;
    std::initializer_list<unsigned char> key;
    bool live_value;
    bool null_text;
    unsigned char unavailable;
};

struct LookupCase
{
    const char* name;
    std::initializer_list<unsigned char> query;
    bool null_query;
    int bucket_count;
    int end_index;
    std::initializer_list<Slot> slots;
    int found_index;
    int text_index; // -1 is the genuine static empty result; -2 is a null hit.
    bool aliases_query;
};

// Literal pointer outcomes; the fixture has no hash or probe implementation.
const LookupCase cases[] = {
    {"null_query", {}, true, 0, 0, {}, 0, -1, false},
    {"empty_key", {0}, false, 1, 1, {{0, {0}, true, false, 0}}, 0, 0, false},
    {"all_unavailable", {65, 0}, false, 3, 3, {}, 3, -1, false},
    {"case_insensitive_hit",
        {65, 98, 0},
        false,
        5,
        5,
        {{3, {97, 98, 0}, true, false, 0}},
        3,
        3,
        false},
    {"wrap_after_collisions",
        {66, 0},
        false,
        5,
        5,
        {{1, {88, 0}, true, false, 0},
            {3, {67, 0}, true, false, 0},
            {4, {68, 0}, true, false, 0},
            {0, {98, 0}, true, false, 0}},
        0,
        0,
        false},
    {"nonzero_flags_skip_null",
        {65, 0},
        false,
        3,
        3,
        {{2, {}, false, false, 255}, {0, {}, false, false, 128}, {1, {97, 0}, true, false, 0}},
        1,
        1,
        false},
    {"full_active_miss",
        {90, 0},
        false,
        3,
        3,
        {{0, {97, 0}, true, false, 0}, {1, {98, 0}, true, false, 0}, {2, {99, 0}, true, false, 0}},
        3,
        -1,
        false},
    {"hash_multiplier_control",
        {65, 66, 0},
        false,
        7,
        7,
        {{4, {65, 66, 0}, true, false, 0}, {6, {97, 98, 0}, true, false, 0}},
        6,
        6,
        false},
    {"same_equivalence_different_hash",
        {97, 98, 0},
        false,
        7,
        7,
        {{2, {65, 66, 0}, true, false, 0}, {6, {97, 98, 0}, true, false, 0}},
        2,
        2,
        false},
    {"borrowed_null_text", {65, 0}, false, 1, 1, {{0, {65, 0}, true, true, 0}}, 0, -2, false},
    {"aliased_key_storage", {65, 0}, false, 1, 1, {{0, {65, 0}, true, false, 0}}, 0, 0, true},
    {"signed_high_byte", {128, 1, 0}, false, 7, 7, {}, 7, -1, false},
    {"signed_minus_one", {255, 0}, false, 3, 3, {}, 3, -1, false},
    {"hash_modulo_wrap",
        {122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 0},
        false,
        7,
        7,
        {},
        7,
        -1,
        false},
    {"end_bucket_is_match", {65, 0}, false, 3, 2, {{2, {97, 0}, true, false, 0}}, 2, -1, false},
};

void copy_bytes(
    std::array<char, key_capacity>& destination, std::initializer_list<unsigned char> source)
{
    std::fill(destination.begin(), destination.end(), static_cast<char>(0x6d));
    std::size_t i = 0;
    for (unsigned char byte : source)
    {
        // Preserve high-byte object representations independently of plain-char signedness.
        std::memcpy(&destination[i], &byte, 1);
        ++i;
    }
}

void initialize(Environment& env, const LookupCase& test)
{
    pattern_lookup(env.lookup.value);
    pattern_lookup(env.decoy.value);
    env.lookup.value.end_index = test.end_index;
    env.lookup.value.bucket_count = test.bucket_count;
    env.lookup.value.buckets = test.null_query ? nullptr : env.buckets.value.data();
    copy_bytes(env.query.value, test.query);
    for (std::size_t i = 0; i < capacity; ++i)
    {
        copy_bytes(env.keys.value[i], {0});
        env.texts.value[i] = {static_cast<unsigned short>(0x1200U + i),
            static_cast<unsigned short>(0x3100U + i),
            0,
            0x6a6a};
        auto& bucket = env.buckets.value[i];
        bucket = text_owner_test::bucket(nullptr, nullptr, 1);
    }
    for (const auto& slot : test.slots)
    {
        auto& bucket = env.buckets.value[slot.index];
        auto& value = env.values.value[slot.index];
        bucket.unavailable = slot.unavailable;
        if (slot.key.size() != 0)
        {
            copy_bytes(env.keys.value[slot.index], slot.key);
            bucket.key =
                test.aliases_query ? env.query.value.data() : env.keys.value[slot.index].data();
        }
        if (slot.live_value)
        {
            value.key = bucket.key;
            value.text = slot.null_text ? nullptr : env.texts.value[slot.index].data();
            bucket.value = &value;
        }
    }
}

void run_lookup(std::size_t index)
{
    category = "lookup";
    case_index = index;
    const auto& test = cases[index];
    case_name = test.name;
    ++sessions;
    Environment env;
    initialize(env, test);
    const EnvironmentSnapshot before(env);
    GlobalSelection selection(&env.decoy.value);
    const char* query = test.null_query ? nullptr : env.query.value.data();
    if (!test.null_query)
    {
        {
            ScreenTextKey key(query);
            const ScreenTextIterator found = env.lookup.value.FindText(key);
            ++find_calls;
            require(found.bucket == env.buckets.value.data() + test.found_index,
                "FindText bucket differs");
            require(found.lookup == &env.lookup.value, "FindText owner differs");
            require(key.text == query, "borrowed key changed");
        }
        verify_environment(env, before);
    }
    const unsigned short* expected =
        test.text_index == -1   ? screen_text_empty
        : test.text_index == -2 ? nullptr
                                : env.texts.value[static_cast<std::size_t>(test.text_index)].data();
    const unsigned short* actual = env.lookup.value.LookupText(query);
    ++lookup_calls;
    require(actual == expected, "LookupText borrowed pointer differs");
    verify_environment(env, before);
    if (!test.null_query)
    {
        {
            const ScreenTextIterator end = env.lookup.value.EndText();
            ++end_calls;
            require(
                end.bucket == env.buckets.value.data() + test.end_index, "EndText bucket differs");
            require(end.lookup == &env.lookup.value, "EndText owner differs");
        }
        verify_environment(env, before);
    }
}

void run_end(std::size_t index, int expected_index)
{
    category = "end";
    case_index = index;
    case_name = "literal-end-field";
    ++sessions;
    Frame<std::array<ScreenTextBucket, 5>> buckets;
    Frame<ScreenTextLookup> lookup;
    pattern_lookup(lookup.value);
    lookup.value.end_index = expected_index;
    lookup.value.bucket_count = 5;
    lookup.value.buckets = buckets.value.data();
    for (auto& bucket : buckets.value)
    {
        bucket = text_owner_test::bucket(nullptr, nullptr, 0);
    }
    const auto old_buckets = buckets;
    const text_owner_test::FrameSnapshot<Guard> old_lookup(lookup);
    {
        const ScreenTextIterator end = lookup.value.EndText();
        ++end_calls;
        require(end.bucket == buckets.value.data() + expected_index, "EndText bucket differs");
        require(end.lookup == &lookup.value, "EndText owner differs");
    }
    verify_guards(buckets, old_buckets);
    verify_guards(lookup, old_lookup);
    require(same_lookup(lookup.value, old_lookup.value), "receiver changed");
    for (std::size_t i = 0; i < buckets.value.size(); ++i)
    {
        require(same_bucket(buckets.value[i], old_buckets.value[i]), "bucket changed");
    }
    require(screen_text_lookup == nullptr, "global owner was not restored");
    require(screen_text_empty[0] == 0, "static empty storage changed");
}

template <class T> struct RawSlot
{
    Guard before = guard(0x29);
    alignas(T) std::array<unsigned char, sizeof(T)> storage{};
    Guard after = guard(0xd7);
};

// The host scope owns only these two real empty-cleanup lifetimes, not their payloads.
struct CleanupScope
{
    ScreenTextKey* key;
    ScreenTextIterator* iterator;
    ~CleanupScope()
    {
        iterator->~ScreenTextIterator();
        key->~ScreenTextKey();
    }
};

void run_cleanup(bool unwind)
{
    category = "cleanup";
    case_index = unwind ? 1 : 0;
    case_name = unwind ? "host-unwind" : "normal";
    ++sessions;
    Environment env;
    initialize(env, cases[10]); // One live aliased query/key/value domain.
    const EnvironmentSnapshot before(env);
    GlobalSelection selection(&env.decoy.value);
    RawSlot<ScreenTextKey> key_slot;
    RawSlot<ScreenTextIterator> iterator_slot;
    auto* key =
        new (static_cast<void*>(key_slot.storage.data())) ScreenTextKey(env.query.value.data());
    auto* iterator = new (static_cast<void*>(iterator_slot.storage.data()))
        ScreenTextIterator{env.buckets.value.data(), &env.lookup.value};
    const auto key_bytes = key_slot.storage;
    const auto iterator_bytes = iterator_slot.storage;
    bool caught = false;
    try
    {
        CleanupScope scope{key, iterator};
        ++cleanup_scopes;
        require(key->text == env.query.value.data(), "live key pointer differs");
        require(iterator->bucket == env.buckets.value.data(), "live iterator bucket differs");
        require(iterator->lookup == &env.lookup.value, "live iterator owner differs");
        if (unwind)
        {
            throw HostUnwind{};
        }
    }
    catch (const HostUnwind&)
    {
        caught = true;
    }
    // Only the surviving unsigned-char storage is read after both object lifetimes end.
    require(caught == unwind, "host unwind outcome differs");
    require(key_slot.storage == key_bytes, "empty key cleanup changed storage");
    require(iterator_slot.storage == iterator_bytes, "empty iterator cleanup changed storage");
    require(key_slot.before == guard(0x29), "key leading guard changed");
    require(key_slot.after == guard(0xd7), "key trailing guard changed");
    require(iterator_slot.before == guard(0x29), "iterator leading guard changed");
    require(iterator_slot.after == guard(0xd7), "iterator trailing guard changed");
    verify_environment(env, before);
}
} // namespace

int main()
{
    try
    {
        const char* locale = std::setlocale(LC_CTYPE, "C");
        require(locale != nullptr && std::strcmp(locale, "C") == 0, "C locale unavailable");
        require(screen_text_lookup == nullptr, "global lookup owner was not zero initialized");
        require(screen_text_empty[0] == 0, "static empty owner was not zero initialized");
        for (std::size_t i = 0; i < std::size(cases); ++i)
        {
            run_lookup(i);
        }
        run_end(0, 0);
        run_end(1, 2);
        run_end(2, 5);
        run_cleanup(false);
        run_cleanup(true);
        require(screen_text_lookup == nullptr, "global lookup owner was not finally restored");
        require(screen_text_empty[0] == 0, "static empty owner was not finally retained");
        std::printf("screen-localization: %u sessions, %u LookupText, %u FindText, %u EndText, %u "
                    "cleanup scopes, %u checks\n",
            sessions,
            lookup_calls,
            find_calls,
            end_calls,
            cleanup_scopes,
            checks);
        return 0;
    }
    catch (const Failure&)
    {
        return 2;
    }
}

#include "startup_list_adaptor.h"
#include "startup_screen_dialog.h"
#include "startup_screen_item.h"
#include "startup_screen_name_search.h"
#include "startup_screen_placement.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <typeinfo>

namespace
{
std::uint64_t cases;
std::uint64_t checks;
std::uint64_t byte_checks;
std::uint64_t release_calls;
std::uint64_t destruction_cases;
std::uint64_t construction_cases;
std::uint64_t retained_cases;
std::uint64_t virtual_calls;
std::uint64_t current_case;

[[noreturn]] void fail(const char* reason)
{
    std::fprintf(stderr, "case %llu: %s\n", static_cast<unsigned long long>(current_case), reason);
    std::exit(2);
}

void require(bool value, const char* reason)
{
    ++checks;
    if (!value)
    {
        fail(reason);
    }
}

struct GeneratedScreenItem : ScreenItem
{
    explicit GeneratedScreenItem(ScreenRectangle initial)
        : ScreenItem(initial)
    {
        // Only fields left untouched by the actual reconstructed constructor.
        kind_28 = 0;
        word_2c = 0;
        std::memset(unknown_31, 0, sizeof(unknown_31));
    }
#if defined(BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM)
    void Unresolved04(float) override
#else
    void Unresolved04() override
#endif
    {
        fail("unresolved slot invoked");
    }
    void Unresolved08() override
    {
        fail("unresolved slot invoked");
    }
    void Unresolved0c() override
    {
        fail("unresolved slot invoked");
    }
    void Unresolved10(ScreenItem*) override
    {
        fail("unresolved slot invoked");
    }
    void Unresolved1c() override
    {
        fail("unresolved slot invoked");
    }
#if defined(BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM)
    unsigned char Unresolved20() override
#else
    void Unresolved20() override
#endif
    {
        fail("unresolved slot invoked");
    }
#if defined(BEACHLIFE_DECOMP_SCREEN_ACTIVATION)
    void Unresolved24(unsigned char) override
#else
    void Unresolved24() override
#endif
    {
        fail("unresolved slot invoked");
    }
    void Unresolved28() override
    {
        fail("unresolved slot invoked");
    }
};

struct GeneratedNamedDialog final : ScreenDialog
{
    ScreenChildNode sentinel;

    explicit GeneratedNamedDialog(ScreenRectangle initial)
        : ScreenDialog(initial)
    {
        // Explicit test-object facility reaches the real base constructor.
        // Derived tail fields are generated borrowed data, not retail lifetime.
        kind_28 = 4;
        word_2c = 0;
        std::memset(unknown_31, 0, sizeof(unknown_31));
        child_44 = nullptr;
        name = "generated";
        std::memset(unknown_4c, 0, sizeof(unknown_4c));
        sentinel = {&sentinel, &sentinel, nullptr};
        children.head = &sentinel;
    }
#if defined(BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM)
    void Unresolved04(float) override
#else
    void Unresolved04() override
#endif
    {
        fail("unresolved slot invoked");
    }
    void Unresolved08() override
    {
        fail("unresolved slot invoked");
    }
    void Unresolved0c() override
    {
        fail("unresolved slot invoked");
    }
    void Unresolved10(ScreenItem*) override
    {
        fail("unresolved slot invoked");
    }
    void Unresolved1c() override
    {
        fail("unresolved slot invoked");
    }
#if defined(BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM)
    unsigned char Unresolved20() override
#else
    void Unresolved20() override
#endif
    {
        fail("unresolved slot invoked");
    }
#if defined(BEACHLIFE_DECOMP_SCREEN_ACTIVATION)
    void Unresolved24(unsigned char) override
#else
    void Unresolved24() override
#endif
    {
        fail("unresolved slot invoked");
    }
    void Unresolved28() override
    {
        fail("unresolved slot invoked");
    }
};

struct Frame
{
    unsigned char before[32];
    alignas(GeneratedScreenItem) unsigned char storage[sizeof(GeneratedScreenItem)];
    unsigned char after[32];
};
using Bytes = std::array<unsigned char, sizeof(Frame)>;
using Corners = std::array<int, 4>;
static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4);
static_assert(sizeof(ScreenRectangle) == 16);

struct ReleaseObservation
{
    ScreenItem* receiver;
    void* allocation;
    std::size_t allocation_size;
    bool released;
    std::size_t calls;
    const Frame* frame;
    Bytes before;
    std::size_t vptr_offset;
};
ReleaseObservation* active;

Bytes read_frame(const Frame& frame)
{
    Bytes bytes{};
    std::memcpy(bytes.data(), &frame, bytes.size());
    return bytes;
}

std::size_t offset(const Frame& frame, const void* member)
{
    const auto start = reinterpret_cast<std::uintptr_t>(&frame);
    const auto location = reinterpret_cast<std::uintptr_t>(member);
    require(location >= start && location < start + sizeof(frame), "member outside live frame");
    return static_cast<std::size_t>(location - start);
}

void same_except_vptr(const Frame& frame, const Bytes& expected, std::size_t vptr)
{
    const Bytes actual = read_frame(frame);
    bool same = true;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        if (i >= vptr && i < vptr + sizeof(void*))
        {
            continue;
        }
        ++byte_checks;
        same = same && actual[i] == expected[i];
    }
    require(same, "destructor changed non-vptr bytes");
}

GeneratedScreenItem* construct(Frame& frame, unsigned int seed, unsigned int flag)
{
    // Frame contains only live unsigned-byte arrays. Pattern it before constructing
    // the real polymorphic subtype; never overwrite a live vptr or pointer value.
    std::memset(&frame, static_cast<int>((seed * 53 + 17) & 255), sizeof(frame));
    auto* item = ::new (static_cast<void*>(frame.storage))
        GeneratedScreenItem(ScreenRectangle{7, -11, 103, 219});
    item->rectangle = {static_cast<int>(seed), -37, 71, -53};
    item->saved_rectangle = {-19, 23, -101, 211};
    // Newly evidenced fields are written through their real scalar types.
    item->elapsed_24 = static_cast<float>(seed % 4096);
    item->state_30 = static_cast<unsigned char>(seed + 5);
    item->last_x = -static_cast<int>(seed);
    item->last_y = static_cast<int>(seed + 7);
    item->kind_28 = static_cast<int>(seed % 65536) - 32768;
    // Form the same four bytes as before, then initialize the real unsigned word.
    std::array<unsigned char, sizeof(item->word_2c)> word_bytes{};
    for (std::size_t i = 0; i < word_bytes.size(); ++i)
    {
        word_bytes[i] = static_cast<unsigned char>(seed + i * 79);
    }
    item->word_2c = std::bit_cast<unsigned int>(word_bytes);
    for (std::size_t i = 0; i < sizeof(item->unknown_31); ++i)
    {
        item->unknown_31[i] = static_cast<unsigned char>(seed + i * 37);
    }
    item->state_40 = static_cast<unsigned char>(seed + 11);
    item->state_41 = static_cast<unsigned char>(seed + 13);
    item->state_42 = static_cast<unsigned char>(seed + 17);
    item->preparation_flag = static_cast<unsigned char>(flag);
    require(typeid(*item) == typeid(GeneratedScreenItem), "invalid generated subtype");
    return item;
}

void construction_matrix()
{
    // The literal Cartesian product includes extrema, inversion, equality,
    // signs and zero. No coordinate arithmetic or invalid pointer is needed.
    constexpr int values[] = {std::numeric_limits<int>::min(),
        -65536,
        -1,
        0,
        1,
        17,
        65535,
        std::numeric_limits<int>::max()};
    for (int left : values)
    {
        for (int top : values)
        {
            for (int right : values)
            {
                for (int bottom : values)
                {
                    current_case = cases;
                    struct Argument
                    {
                        unsigned int before;
                        ScreenRectangle rectangle;
                        unsigned int after;
                    } argument{0x12345678U, {left, top, right, bottom}, 0x87654321U};
                    Frame frame;
                    const unsigned char guard = static_cast<unsigned char>(cases * 53 + 17);
                    std::memset(&frame, guard, sizeof(frame));
                    auto* item = ::new (static_cast<void*>(frame.storage))
                        GeneratedScreenItem(argument.rectangle);
                    require(item->preparation_flag == 1, "constructor preparation flag differs");
                    require(item->rectangle.left == left && item->rectangle.top == top &&
                                item->rectangle.right == right && item->rectangle.bottom == bottom,
                        "constructor current rectangle differs");
                    require(item->saved_rectangle.left == left &&
                                item->saved_rectangle.top == top &&
                                item->saved_rectangle.right == right &&
                                item->saved_rectangle.bottom == bottom,
                        "constructor saved rectangle differs");
                    require(std::bit_cast<std::uint32_t>(item->elapsed_24) == 0U,
                        "constructor elapsed is not positive zero");
                    require(item->state_30 == 0 && item->state_40 == 0 && item->state_41 == 0 &&
                                item->state_42 == 0,
                        "constructor byte state differs");
                    require(item->last_x == 0 && item->last_y == 0 && item->owned_text == nullptr,
                        "constructor coordinate or ownership state differs");
                    require(argument.before == 0x12345678U && argument.after == 0x87654321U &&
                                argument.rectangle.left == left && argument.rectangle.top == top &&
                                argument.rectangle.right == right &&
                                argument.rectangle.bottom == bottom,
                        "constructor changed source argument");
                    require(typeid(*item) == typeid(GeneratedScreenItem),
                        "constructor invalid dynamic type");
                    // The generated subtype initializes the untouched members to valid values.
                    // Their preconstruction storage contents and dead stores are not a modern
                    // compiler preservation claim; retail's untouched offsets are audited
                    // statically.
                    require(item->kind_28 == 0 && item->word_2c == 0,
                        "generated discriminator initialization differs");
                    for (unsigned char byte : frame.before)
                    {
                        require(byte == guard, "constructor damaged leading guard");
                    }
                    for (unsigned char byte : frame.after)
                    {
                        require(byte == guard, "constructor damaged trailing guard");
                    }
                    std::destroy_at(item);
                    for (unsigned char byte : frame.before)
                    {
                        require(byte == guard, "destructor damaged leading guard");
                    }
                    for (unsigned char byte : frame.after)
                    {
                        require(byte == guard, "destructor damaged trailing guard");
                    }
                    ++construction_cases;
                    ++cases;
                }
            }
        }
    }
}

void destroy_case(unsigned int flag, std::size_t length, unsigned int repetition)
{
    current_case = cases;
    Frame frame;
    auto* item = construct(frame, static_cast<unsigned int>(cases + repetition), flag);
    void* allocation = nullptr;
    if (length != 0)
    {
        allocation = ::operator new(length);
        char* text = ::new (allocation) char[length];
        require(static_cast<void*>(text) == allocation, "placement char array changed allocation");
        for (std::size_t i = 0; i + 1 < length; ++i)
        {
            text[i] = static_cast<char>('a' + i % 26);
        }
        text[length - 1] = '\0';
        item->owned_text = text;
    }
    ScreenItem* base = item;
    ReleaseObservation observation{
        base, allocation, length, false, 0, &frame, read_frame(frame), offset(frame, base)};
    active = &observation;
    std::destroy_at(item); // Real generated subtype -> actual reconstructed base destructor.
    active = nullptr;
    // The underlying byte arrays remain alive after the nested object lifetime.
    // Do not dereference the ended ScreenItem or interpret its dangling pointer.
    same_except_vptr(frame, observation.before, observation.vptr_offset);
    if (length != 0 && !observation.released)
    {
        // Preserve an ordinary, diagnostic control failure without a secondary leak.
        ::operator delete(allocation);
        fail("owned storage not released exactly once");
    }
    require(observation.calls == (length == 0 ? 0U : 1U), "wrong explicit-null release count");
    ++destruction_cases;
    ++cases;
}

Corners translated(Corners input, int dx, int dy)
{
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const std::int64_t value = static_cast<std::int64_t>(input[i]) + (i % 2 == 0 ? dx : dy);
        require(
            value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max(),
            "generated arithmetic outside supported domain");
        input[i] = static_cast<int>(value);
    }
    return input;
}

void observe_rectangle(Frame& frame, GeneratedScreenItem& item, Bytes& expected, Corners corners)
{
    std::memcpy(
        expected.data() + offset(frame, &item.rectangle), corners.data(), sizeof(ScreenRectangle));
    const auto actual = read_frame(frame);
    byte_checks += actual.size();
    require(actual == expected, "retained whole-object rectangle oracle differs");
    const auto value = item.GetRectangle();
    require(value.left == corners[0] && value.top == corners[1] && value.right == corners[2] &&
                value.bottom == corners[3],
        "retained rectangle getter differs");
    require(read_frame(frame) == expected, "getter mutated object representation");
}

void retained_fields(unsigned int flag)
{
    current_case = cases;
    Frame frame;
    auto* item = construct(frame, flag, flag);
    const Corners saved = {-19, 23, -101, 211};
    Bytes expected = read_frame(frame);
    ScreenItem* volatile receiver = item;
    receiver->MarkPrepared();
    ++virtual_calls;
    expected[offset(frame, &item->preparation_flag)] = 1;
    require(read_frame(frame) == expected, "preparation flag changed other state");
    receiver->MarkPrepared();
    ++virtual_calls;
    require(read_frame(frame) == expected, "repeated preparation differs");
    const int dx = static_cast<int>(flag) - 113;
    const int dy = 7 - static_cast<int>(flag);
    receiver->ApplyPosition(dx, dy);
    ++virtual_calls;
    const Corners positioned = translated(saved, dx, dy);
    observe_rectangle(frame, *item, expected, positioned);
    receiver->ApplyPosition(dx, dy);
    ++virtual_calls;
    observe_rectangle(frame, *item, expected, positioned);
    receiver->MoveRectangle(dy, dx);
    ++virtual_calls;
    observe_rectangle(frame, *item, expected, translated(positioned, dy, dx));
    receiver->ApplyPosition(0, 0);
    ++virtual_calls;
    observe_rectangle(frame, *item, expected, saved);
    std::destroy_at(item);
    ++retained_cases;
    ++cases;
}

void retained_lists()
{
    // Independent literal ordering/case/duplicate expectations, using only live
    // typed nodes and actual constructed subtype objects from the common header.
    constexpr const char* names[] = {"Alpha", "alphabet", "Alpha", "", "Long-shared-prefix-X"};
    constexpr const char* queries[] = {
        "Alpha", "alphabet", "alpha", "", "Long-shared-prefix-X", "missing"};
    constexpr int expected_index[] = {0, 1, -1, 3, 4, -1};
    GeneratedNamedDialog items[5] = {GeneratedNamedDialog(ScreenRectangle{1, 2, 3, 4}),
        GeneratedNamedDialog(ScreenRectangle{-1, -2, -3, -4}),
        GeneratedNamedDialog(ScreenRectangle{0, 0, 0, 0}),
        GeneratedNamedDialog(ScreenRectangle{9, 7, 5, 3}),
        GeneratedNamedDialog(ScreenRectangle{-21, 13, 17, -9})};
    ActiveScreenEntry entries[5]{};
    CircularListValueNode nodes[5]{};
    CircularListNode sentinel{};
    CircularList list;
    list.head = &sentinel;
    for (unsigned int count = 0; count <= 5; ++count)
    {
        sentinel.next = count ? &nodes[0] : &sentinel;
        sentinel.previous = count ? &nodes[count - 1] : &sentinel;
        for (unsigned int i = 0; i < count; ++i)
        {
            items[i].name = names[i];
            entries[i] = {&items[i], static_cast<int>(i), -static_cast<int>(i), nullptr};
            nodes[i].entry = &entries[i];
            nodes[i].previous = i ? &nodes[i - 1] : &sentinel;
            nodes[i].next = i + 1 < count ? &nodes[i + 1] : &sentinel;
        }
        const auto begin = list.Begin();
        const auto end = list.End();
        require(begin.node == sentinel.next && end.node == &sentinel,
            "retained iterator endpoints differ");
        CircularListIteratorAdaptor adaptor(begin);
        require(adaptor.current.node == begin.node, "retained adaptor loses identity");
        for (unsigned int q = 0; q != 6; ++q)
        {
            current_case = cases;
            CircularListIterator result;
            result.node = nullptr;
            CircularListNode* expected =
                expected_index[q] >= 0 && static_cast<unsigned int>(expected_index[q]) < count
                    ? &nodes[expected_index[q]]
                    : &sentinel;
            std::array<std::array<unsigned char, sizeof(GeneratedNamedDialog)>, 5> before{};
            for (unsigned int i = 0; i != 5; ++i)
            {
                std::memcpy(
                    before[i].data(), static_cast<const void*>(&items[i]), sizeof(items[i]));
            }
            std::array<unsigned char, sizeof(entries)> entries_before{};
            std::array<unsigned char, sizeof(nodes)> nodes_before{};
            std::array<unsigned char, sizeof(sentinel)> sentinel_before{};
            std::memcpy(entries_before.data(), entries, sizeof(entries));
            std::memcpy(nodes_before.data(), nodes, sizeof(nodes));
            std::memcpy(sentinel_before.data(), &sentinel, sizeof(sentinel));
            require(FindScreenByName(&result, begin, end, queries[q]) == &result &&
                        result.node == expected,
                "retained first-name lookup differs");
            for (unsigned int i = 0; i != 5; ++i)
            {
                byte_checks += sizeof(items[i]);
                require(std::memcmp(before[i].data(),
                            static_cast<const void*>(&items[i]),
                            sizeof(items[i])) == 0,
                    "name lookup modified polymorphic item");
            }
            require(std::memcmp(entries_before.data(), entries, sizeof(entries)) == 0 &&
                        std::memcmp(nodes_before.data(), nodes, sizeof(nodes)) == 0 &&
                        std::memcmp(sentinel_before.data(), &sentinel, sizeof(sentinel)) == 0,
                "name lookup modified typed entry graph");
            byte_checks += sizeof(entries) + sizeof(nodes) + sizeof(sentinel);
            ++retained_cases;
            ++cases;
        }
    }
    SavedScreenPlacement values[] = {{11, 13, 7}, {-17, 19, 8}, {23, -29, 7}};
    SavedScreenPlacementNode positions[3];
    ScreenPlacementStore store{};
    store.saved.head = &sentinel;
    for (unsigned int count = 0; count <= 3; ++count)
    {
        sentinel.next = count ? &positions[0] : &sentinel;
        sentinel.previous = count ? &positions[count - 1] : &sentinel;
        for (unsigned int i = 0; i < count; ++i)
        {
            positions[i].placement = &values[i];
            positions[i].previous = i ? &positions[i - 1] : &sentinel;
            positions[i].next = i + 1 < count ? &positions[i + 1] : &sentinel;
        }
        for (int group : {7, 8, 9})
        {
            current_case = cases;
            const int index = group == 7 && count >= 1 ? 0 : (group == 8 && count >= 2 ? 1 : -1);
            int x = -111;
            int y = 222;
            require(store.FindPlacement(group, &x, &y) == (index >= 0),
                "retained placement presence differs");
            require(x == (index >= 0 ? values[index].x : -111) &&
                        y == (index >= 0 ? values[index].y : 222),
                "retained first-placement output differs");
            ++retained_cases;
            ++cases;
        }
    }
}
} // namespace

// Test-executable-only paired host scalar allocation boundary. Actual prepared
// constructor/destructor and callback bodies remain unchanged. No VC6 release
// support or free-symbol substitution is linked into this executable.
void* operator new(std::size_t size)
{
    for (;;)
    {
        if (void* memory = std::malloc(size == 0 ? 1 : size))
        {
            return memory;
        }
        if (const auto handler = std::get_new_handler())
        {
            handler();
        }
        else
        {
            throw std::bad_alloc();
        }
    }
}

void operator delete(void* memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* memory) noexcept
{
    if (active != nullptr)
    {
        ++active->calls;
        ++release_calls;
        require(!active->released, "duplicate release");
        require(memory == active->allocation && memory != nullptr, "wrong released allocation");
        require(typeid(*active->receiver) == typeid(ScreenItem),
            "base destructor vptr transition absent");
        same_except_vptr(*active->frame, active->before, active->vptr_offset);
        const auto* text = static_cast<const char*>(memory);
        require(text[active->allocation_size - 1] == '\0', "owned text changed before release");
        active->released = true;
    }
    std::free(memory);
}

namespace
{
std::uint64_t provider_cases;
std::uint64_t provider_checks;

void provider_require(bool value, const char* reason)
{
    ++provider_checks;
    if (!value)
    {
        std::fprintf(stderr,
            "provider case %llu: %s\n",
            static_cast<unsigned long long>(provider_cases),
            reason);
        std::exit(2);
    }
}

void check_host_provider()
{
    // Explicit scalar calls; no dependency on array/aligned/nothrow delegation.
    // Volatile pointer loads keep these tiny probes bound to the real providers.
    void* (*volatile allocate)(std::size_t) = &::operator new;
    void (*volatile release)(void*) noexcept = &::operator delete;
    void (*volatile release_sized)(void*, std::size_t) noexcept = &::operator delete;
    for (std::size_t size : {0U, 1U, 31U, 257U})
    {
        for (bool sized : {false, true})
        {
            provider_require(active == nullptr, "release observation already active");
            void* memory = allocate(size);
            provider_require(memory != nullptr, "scalar allocation returned null");
            provider_require(
                reinterpret_cast<std::uintptr_t>(memory) % alignof(std::max_align_t) == 0,
                "scalar allocation is not fundamentally aligned");
            // The provider reserves one writable byte for a zero-size request.
            const std::size_t extent = size == 0 ? 1 : size;
            auto* bytes = static_cast<unsigned char*>(memory);
            for (std::size_t i = 0; i < extent; ++i)
            {
                bytes[i] = static_cast<unsigned char>(i * 37 + provider_cases);
            }
            bool same = true;
            for (std::size_t i = 0; i < extent; ++i)
            {
                same = same && bytes[i] == static_cast<unsigned char>(i * 37 + provider_cases);
            }
            provider_require(same, "scalar allocation is not writable");
            if (sized)
            {
                release_sized(memory, size);
            }
            else
            {
                release(memory);
            }
            provider_require(active == nullptr, "provider captured unobserved release");
            ++provider_cases;
        }
    }
    release(nullptr);
    provider_require(active == nullptr, "null scalar release entered observation");
    ++provider_cases;
    release_sized(nullptr, 0);
    provider_require(active == nullptr, "null sized release entered observation");
    ++provider_cases;
}
} // namespace

int main()
{
    construction_matrix();
    // Retain the prior ownership oracle after exercising the actual constructor.
    destroy_case(1, 1, 0);
    for (unsigned int flag = 0; flag != 256; ++flag)
    {
        for (std::size_t size : {0U, 1U, 2U, 31U, 64U, 257U})
        {
            destroy_case(flag, size, 0);
            destroy_case(flag, size, 1);
        }
        retained_fields(flag);
    }
    retained_lists();
    require(active == nullptr, "release observation escaped scope");
    check_host_provider();
    std::printf("{\"cases\":%llu,\"checks\":%llu,\"byte_checks\":%llu,\"destruction_cases\":%llu,"
                "\"retained_cases\":%llu,\"release_calls\":%llu,\"virtual_calls\":%llu,"
                "\"construction_cases\":%llu,\"provider_cases\":%llu,"
                "\"provider_checks\":%llu}\n",
        static_cast<unsigned long long>(cases),
        static_cast<unsigned long long>(checks),
        static_cast<unsigned long long>(byte_checks),
        static_cast<unsigned long long>(destruction_cases),
        static_cast<unsigned long long>(retained_cases),
        static_cast<unsigned long long>(release_calls),
        static_cast<unsigned long long>(virtual_calls),
        static_cast<unsigned long long>(construction_cases),
        static_cast<unsigned long long>(provider_cases),
        static_cast<unsigned long long>(provider_checks));
}

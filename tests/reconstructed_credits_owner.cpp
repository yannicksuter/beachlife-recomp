#include "pool_allocate.h"
#include "pool_chunk_acquire.h"
#include "pool_return.h"
#include "startup_screen_manager_owner190.h"
#include "startup_screen_manager_owner190_entry.h"
#include "startup_screen_text_item.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace
{
using Owner = ScreenManagerOwner190;
using Node = ScreenManagerOwner190Node;
using Entry = ScreenManagerOwner190Entry;
using Guard = std::array<unsigned char, 16>;
unsigned int checks = 0;
const char* current_case = "setup";

struct Failure
{
    const char* message;
};

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw Failure{message};
    }
}

bool filled(const Guard& guard, unsigned char value)
{
    return std::all_of(guard.begin(),
        guard.end(),
        [value](unsigned char byte)
        {
            return byte == value;
        });
}

static_assert(sizeof(void*) == 8 && sizeof(Owner) == 32 && sizeof(Node) == 24);
static_assert(std::is_standard_layout_v<Owner> && std::is_standard_layout_v<Node>);
static_assert(offsetof(Owner, index_00) == 0 && offsetof(Owner, entries) == 8);
static_assert(offsetof(Owner, word_08) == 16 && offsetof(Owner, finish_delay_0c) == 20);
static_assert(offsetof(Owner, finishing_10) == 24);
static_assert(
    offsetof(Node, next) == 0 && offsetof(Node, previous) == 8 && offsetof(Node, entry) == 16);
static_assert(!std::is_copy_constructible_v<Owner> && !std::is_move_constructible_v<Owner>);

struct Slot
{
    Guard before;
    alignas(Owner) std::array<std::byte, sizeof(Owner)> storage;
    Guard after;

    Slot()
    {
        before.fill(0x37);
        after.fill(0xb9);
    }
};
static_assert(offsetof(Slot, storage) == 16);
static_assert(offsetof(Slot, after) == 16 + sizeof(Owner));

struct ConstructedOwner
{
    Slot slot;
    Owner* value = ::new (static_cast<void*>(slot.storage.data())) Owner;

    ~ConstructedOwner()
    {
        if (value)
        {
            value->~Owner();
        }
    }

    void destroy()
    {
        auto* live = std::exchange(value, nullptr);
        live->~Owner();
    }

    void verify_guards()
    {
        require(
            filled(slot.before, 0x37) && filled(slot.after, 0xb9), "owner outside guards changed");
    }
};

// Observe the real allocator's LIFO bucket, without replacing it. A failed
// sentinel-return control leaves that live storage for this fixture to return.
void verify_sentinel_return(Node* sentinel)
{
    constexpr auto bucket = (sizeof(Node) - 1) / 8;
    auto* expected = reinterpret_cast<PoolChunkLink*>(sentinel);
    const bool returned = pool_chunk_buckets[bucket] == expected;
    if (!returned)
    {
        ReturnPool(sentinel, sizeof(Node));
    }
    require(returned, "sentinel storage was not returned to the real pool");
}

void verify_constructed(Owner& owner)
{
    require(owner.index_00 == 0 && owner.finish_delay_0c == 0.0f && owner.finishing_10 == 0,
        "real constructor fields differ");
    auto* head = owner.entries.head;
    require(head && head->next == head && head->previous == head, "real sentinel is not circular");
    // The original constructor deliberately leaves word_08 uninitialized.
    // Establish its first defined value through the real Reset before reading.
    owner.Reset();
    require(owner.index_00 == 0 && owner.word_08 == 0, "initial Reset differs");
    owner.index_00 = 17;
    owner.word_08 = 39;
    owner.finish_delay_0c = 1.5f;
    owner.finishing_10 = 1;
    owner.Reset();
    require(owner.index_00 == 0 && owner.word_08 == 0, "Reset fields differ");
    require(owner.entries.head == head && head->next == head && head->previous == head &&
                owner.finish_delay_0c == 1.5f && owner.finishing_10 == 1,
        "Reset changed unrelated owner state");
}

struct BorrowedText
{
    Guard before{};
    std::array<unsigned short, 3> text{65, 66, 0};
    Guard after{};

    BorrowedText()
    {
        before.fill(0x51);
        after.fill(0xa3);
    }

    void verify() const
    {
        require(text == std::array<unsigned short, 3>{65, 66, 0} && filled(before, 0x51) &&
                    filled(after, 0xa3),
            "borrowed text or its guards changed");
    }
};

void set_text(ScreenTextItem& item, const BorrowedText& borrowed)
{
    constexpr std::array<unsigned short, 3> owned{67, 68, 0};
    auto* text = static_cast<unsigned short*>(::operator new(sizeof(owned)));
    std::memcpy(text, owned.data(), sizeof(owned));
    item.owned_text_48 = text;
    item.SetOwnedText("credits-owner-fixture");
    item.borrowed_text = borrowed.text.data();
}

struct Observation
{
    unsigned int count = 0;
    std::array<unsigned int, 2> order{};
    bool records_live = true;
};

// Real Text/base construction and cleanup remain in use. This test subtype
// observes callback order while its real entry is still alive.
struct ObservedText final : ScreenTextItem
{
    Observation& observation;
    unsigned int id;
    Entry* entry = nullptr;

    ObservedText(Observation& observed, unsigned int value)
        : ScreenTextItem({1, 2, 11, 13}, nullptr, 0, 7, 0)
        , observation(observed)
        , id(value)
    {
    }

    ~ObservedText() override
    {
        observation.records_live = observation.records_live && entry && entry->item == this &&
                                   entry->word_04 == id && owned_text && owned_text_48 &&
                                   borrowed_text;
        if (observation.count < observation.order.size())
        {
            observation.order[observation.count] = id;
        }
        ++observation.count;
    }
};

void append_generated_entry(Owner& owner,
    std::unique_ptr<ScreenTextItem> text,
    unsigned int word,
    ObservedText* observed = nullptr)
{
    auto entry = std::make_unique<Entry>();
    entry->item = text.get();
    entry->word_04 = word;
    if (observed)
    {
        observed->entry = entry.get();
    }
    auto* head = owner.entries.head;
    auto* node = ::new (AllocatePoolBytes(sizeof(Node))) Node{head, head->previous, entry.get()};
    head->previous->next = node;
    head->previous = node;
    entry.release();
    text.release();
    // This typed generated list is fixture setup, not the game Insert method.
}

void empty_case()
{
    current_case = "empty";
    ConstructedOwner owner;
    verify_constructed(*owner.value);
    auto* sentinel = owner.value->entries.head;
    owner.destroy();
    owner.verify_guards();
    verify_sentinel_return(sentinel);
}

void nonempty_case()
{
    current_case = "nonempty";
    BorrowedText borrowed;
    Observation observation;
    ConstructedOwner owner;
    verify_constructed(*owner.value);
    auto* sentinel = owner.value->entries.head;
    for (unsigned int id : {1U, 2U})
    {
        auto text = std::make_unique<ObservedText>(observation, id);
        set_text(*text, borrowed);
        auto* observed = text.get();
        append_generated_entry(*owner.value, std::move(text), id, observed);
    }
    auto plain = std::make_unique<ScreenTextItem>(ScreenRectangle{3, 4, 17, 19}, nullptr, 0, 9, 0);
    set_text(*plain, borrowed);
    append_generated_entry(*owner.value, std::move(plain), 3);
    auto* first = sentinel->next;
    require(first != sentinel && first->previous == sentinel && first->next->previous == first &&
                first->next->next == sentinel->previous && sentinel->previous->next == sentinel,
        "generated real-node links differ");
    owner.destroy();
    require(observation.count == 2 && observation.records_live &&
                observation.order == std::array<unsigned int, 2>{1, 2},
        "real Text cleanup order or live entry ownership differs");
    borrowed.verify();
    owner.verify_guards();
    verify_sentinel_return(sentinel);
}
} // namespace

int main()
{
    try
    {
        empty_case();
        nonempty_case();
        std::printf(
            "{\"owners\":2,\"entries\":3,\"plain_texts\":1,\"observed_texts\":2,\"checks\":%u}\n",
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "credits-owner %s: %s\n", current_case, failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "credits-owner host failure: %s\n", error.what());
        return 3;
    }
}

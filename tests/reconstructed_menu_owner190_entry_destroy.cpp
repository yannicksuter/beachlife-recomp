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

namespace
{
using Guard = std::array<unsigned char, 16>;
constexpr std::array<unsigned short, 6> borrowed_literal{65, 66, 0, 0x1234, 0x5678, 0x9abc};
constexpr std::array<unsigned short, 5> owned_literal{67, 68, 0, 0x3456, 0x789a};
constexpr unsigned int entry_word = 0x17539badU;
const char* current_case = "setup";
unsigned int checks = 0;
unsigned int calls = 0;

struct Failure
{
    const char* reason;
};

void require(bool condition, const char* reason)
{
    ++checks;
    if (!condition)
    {
        throw Failure{reason};
    }
}

bool filled(const Guard& bytes, unsigned char value)
{
    return std::all_of(bytes.begin(),
        bytes.end(),
        [value](unsigned char byte)
        {
            return byte == value;
        });
}

struct BorrowedText
{
    Guard before{};
    std::array<unsigned short, 6> text = borrowed_literal;
    Guard after{};

    BorrowedText()
    {
        before.fill(0x53);
        after.fill(0xa7);
    }
    void verify() const
    {
        require(text == borrowed_literal && filled(before, 0x53) && filled(after, 0xa7),
            "borrowed text or outside guard changed");
    }
};

// This trivial scalar allocation begins with the real entry. The callback
// receives the allocation start and releases it with the real unsized scalar
// operator delete. The observer reads the tail only while the entry is live.
struct RecordStorage
{
    ScreenManagerOwner190Entry entry{};
    Guard after{};
};
static_assert(std::is_standard_layout_v<RecordStorage>);
static_assert(std::is_trivially_destructible_v<RecordStorage>);
static_assert(offsetof(RecordStorage, entry) == 0);
static_assert(offsetof(RecordStorage, after) == sizeof(ScreenManagerOwner190Entry));
static_assert(std::is_standard_layout_v<ScreenManagerOwner190Entry>);
static_assert(offsetof(ScreenManagerOwner190Entry, item) == 0);
static_assert(offsetof(ScreenManagerOwner190Entry, word_04) == sizeof(ScreenItem*));
static_assert(sizeof(unsigned int) == 4);
static_assert(std::has_virtual_destructor_v<ScreenTextItem>);

std::unique_ptr<RecordStorage> record(ScreenItem* item)
{
    auto result = std::make_unique<RecordStorage>();
    result->entry = {item, entry_word};
    result->after.fill(0xb6);
    return result;
}

void set_text_ownership(ScreenTextItem& item, const BorrowedText& borrowed)
{
    require(item.kind_28 == 12 && item.owned_text == nullptr && item.owned_text_48 == nullptr &&
                item.borrowed_text == nullptr,
        "real Text/base construction differs");
    // These are the exact scalar families used by the genuine destructors.
    // C++20 memcpy starts the implicit unsigned-short array lifetime.
    auto* allocation = static_cast<unsigned short*>(::operator new(sizeof(owned_literal)));
    std::memcpy(allocation, owned_literal.data(), sizeof(owned_literal));
    item.owned_text_48 = allocation;
    item.SetOwnedText("base-owned");
    item.borrowed_text = borrowed.text.data();
    require(std::strcmp(item.owned_text, "base-owned") == 0 &&
                std::equal(owned_literal.begin(), owned_literal.end(), item.owned_text_48),
        "real owned-text setup differs");
}

enum class Phase
{
    setup,
    callback,
    returned,
    failure_cleanup
};
struct Observation
{
    Phase phase = Phase::setup;
    RecordStorage* record = nullptr;
    unsigned int destructions = 0;
    unsigned int callback_destructions = 0;
    bool entry_live_and_intact = false;
    bool text_live_and_intact = false;
    std::array<unsigned int, 3> order{};
};

// This is a genuine constructed test subtype, solely an order observer. The
// separate plain-Text case below exercises the retail-derived class/table.
struct ObservedText final : ScreenTextItem
{
    Observation& observation;
    const BorrowedText& borrowed;
    Guard guard{};

    ObservedText(Observation& value, const BorrowedText& text)
        : ScreenTextItem({2, 3, 11, 13}, nullptr, 0, 7, 0)
        , observation(value)
        , borrowed(text)
    {
        guard.fill(0xc9);
    }
    ~ObservedText() override
    {
        ++observation.destructions;
        if (observation.phase == Phase::callback)
        {
            ++observation.callback_destructions;
            observation.order[1] = 20;
            const auto& owner = *observation.record;
            observation.entry_live_and_intact = owner.entry.item == this &&
                                                owner.entry.word_04 == entry_word &&
                                                filled(owner.after, 0xb6);
            observation.text_live_and_intact =
                filled(guard, 0xc9) && borrowed_text == borrowed.text.data() &&
                owned_text != nullptr && std::strcmp(owned_text, "base-owned") == 0 &&
                owned_text_48 != nullptr &&
                std::equal(owned_literal.begin(), owned_literal.end(), owned_text_48);
        }
        // C++ then invokes the real Text and base destructors. No assertions
        // throw here, and failure cleanup never reads the already-freed entry.
    }
};

struct PayloadReceipt
{
    ObservedText* item;
    Observation& observation;
    ~PayloadReceipt()
    {
        if (observation.destructions == 0)
        {
            observation.phase = Phase::failure_cleanup;
            observation.record = nullptr;
            delete item;
        }
    }
};

void null_group()
{
    current_case = "null";
    DestroyManagerOwner190Entry(nullptr);
    ++calls;
    auto owner = record(nullptr);
    require(owner->entry.item == nullptr && owner->entry.word_04 == entry_word &&
                filled(owner->after, 0xb6),
        "null payload setup differs");
    DestroyManagerOwner190Entry(&owner.release()->entry);
    ++calls;
    // The record lifetime has ended. Do not read its bytes or free it again.
}

void observer_group()
{
    current_case = "observer";
    BorrowedText borrowed;
    Observation observation;
    auto item = std::make_unique<ObservedText>(observation, borrowed);
    set_text_ownership(*item, borrowed);
    auto owner = record(item.get());
    PayloadReceipt receipt{item.release(), observation};
    observation.record = owner.get();
    observation.order[0] = 10;
    observation.phase = Phase::callback;
    DestroyManagerOwner190Entry(&owner.release()->entry);
    ++calls;
    observation.record = nullptr;
    observation.phase = Phase::returned;
    observation.order[2] = 30;
    require(observation.callback_destructions == 1, "owned payload destructor was skipped");
    require(observation.destructions == 1 && observation.entry_live_and_intact &&
                observation.text_live_and_intact &&
                observation.order == std::array<unsigned int, 3>{10, 20, 30},
        "virtual destruction order or live guards differ");
    borrowed.verify();
}

void plain_text_group()
{
    current_case = "plain-Text";
    BorrowedText borrowed;
    // Deliberately the actual retail-derived class, not the observer subtype.
    auto item = std::make_unique<ScreenTextItem>(ScreenRectangle{4, 5, 17, 19}, nullptr, 0, 9, 0);
    set_text_ownership(*item, borrowed);
    auto owner = record(item.get());
    require(owner->entry.item == item.get() && owner->entry.word_04 == entry_word,
        "plain Text ownership differs");
    item.release();
    DestroyManagerOwner190Entry(&owner.release()->entry);
    ++calls;
    borrowed.verify();
    // Actual selected Text/base destructors and leak sanitization establish
    // release; no dead typed read, replacement allocator or destructor provider.
}
} // namespace

int main()
{
    try
    {
        null_group();
        observer_group();
        // The skipped-delete control fails and cleans its live observer before
        // this separate plain payload is ever allocated.
        plain_text_group();
        std::printf(
            "{\"groups\":3,\"calls\":%u,\"plain_texts\":1,\"observer_texts\":1,\"checks\":%u}\n",
            calls,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "entry-destroy %s: %s\n", current_case, failure.reason);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "entry-destroy host failure: %s\n", error.what());
        return 3;
    }
}

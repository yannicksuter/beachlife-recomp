#include "child_ring_seed.h"
#include "startup_gui_defined_item.h"
#include "startup_gui_defined_item_globals.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <memory>
#include <new>

namespace
{
unsigned group = 0;
unsigned long long checks = 0;
unsigned long long guard_bytes = 0;
unsigned exchanges = 0;
unsigned owners = 0;
unsigned owner_ends = 0;
unsigned observer_ends = 0;
unsigned derived_calls = 0;
unsigned inherited_text_calls = 0;
unsigned callbacks = 0;
bool pool_phase = false;

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

bool same_rectangle(const ScreenRectangle& a, const ScreenRectangle& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// Only declared initialized values are compared. No vptr or ABI padding is read.
struct State
{
    ScreenRectangle rectangle;
    ScreenRectangle saved_rectangle;
    float elapsed;
    int kind;
    unsigned word;
    unsigned char state;
    std::array<unsigned char, 3> opaque;
    int x;
    int y;
    char* text;
    std::array<unsigned char, 4> flags;

    explicit State(const ScreenItem& item)
        : rectangle(item.rectangle)
        , saved_rectangle(item.saved_rectangle)
        , elapsed(item.elapsed_24)
        , kind(item.kind_28)
        , word(item.word_2c)
        , state(item.state_30)
        , opaque{item.unknown_31[0], item.unknown_31[1], item.unknown_31[2]}
        , x(item.last_x)
        , y(item.last_y)
        , text(item.owned_text)
        , flags{item.state_40, item.state_41, item.state_42, item.preparation_flag}
    {
    }

    void check(const ScreenItem& item) const
    {
        require(same_rectangle(item.rectangle, rectangle), "receiver rectangle changed");
        require(same_rectangle(item.saved_rectangle, saved_rectangle), "saved rectangle changed");
        require(item.elapsed_24 == elapsed && item.kind_28 == kind && item.word_2c == word,
            "receiver scalar state changed");
        require(item.state_30 == state && item.unknown_31[0] == opaque[0] &&
                    item.unknown_31[1] == opaque[1] && item.unknown_31[2] == opaque[2],
            "receiver byte state changed");
        require(item.last_x == x && item.last_y == y && item.owned_text == text,
            "receiver coordinates or owned text changed");
        require(item.state_40 == flags[0] && item.state_41 == flags[1] &&
                    item.state_42 == flags[2] && item.preparation_flag == flags[3],
            "receiver flags changed");
    }
};

void initialize_untouched(ScreenItem& item)
{
    item.word_2c = 0x13579bdfU;
    item.unknown_31[0] = 0x29;
    item.unknown_31[1] = 0x83;
    item.unknown_31[2] = 0xc1;
}

struct RectangleScope
{
    ScreenRectangle saved = gui_defined_item_rectangle;
    ~RectangleScope()
    {
        gui_defined_item_rectangle = saved;
    }
};

struct Storage
{
    std::array<unsigned char, 16> before;
    alignas(GuiDefinedItem) unsigned char bytes[sizeof(GuiDefinedItem)];
    std::array<unsigned char, 16> after;

    Storage()
    {
        before.fill(0x5a);
        after.fill(0xc3);
    }

    void check() const
    {
        for (unsigned i = 0; i != before.size(); ++i)
        {
            require(before[i] == 0x5a && after[i] == 0xc3, "defined owner guard changed");
            guard_bytes += 2;
        }
    }
};

struct Owner
{
    GuiDefinedItem* live;

    Owner(Storage& storage, const char* name)
        : live(::new (static_cast<void*>(storage.bytes)) GuiDefinedItem(name))
    {
        ++owners;
    }
    ~Owner()
    {
        finish();
    }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;

    void finish()
    {
        if (live)
        {
            GuiDefinedItem* ending = live;
            live = nullptr;
            ending->~GuiDefinedItem();
            ++owner_ends;
        }
    }
};

using Seed = child_ring_fixture::Seed<6>;
struct PoolSession
{
    Seed seed{require};
    PoolSession()
    {
        require(!pool_phase, "nested defined-item pool session");
        pool_phase = true;
    }
    ~PoolSession()
    {
        pool_phase = false;
    }
    void finish(std::initializer_list<unsigned> free_order)
    {
        seed.check_free(free_order);
        guard_bytes += seed.checked_bytes();
    }
};

void check_constructor(const GuiDefinedItem& item,
    const ScreenRectangle& expected,
    const char* borrowed_name,
    const char* expected_name)
{
    require(item.kind_28 == 3, "kind");
    require(same_rectangle(item.rectangle, expected), "constructor rectangle");
    require(same_rectangle(item.saved_rectangle, expected), "constructor saved rectangle");
    require(item.identifier != nullptr && item.identifier != borrowed_name, "name ownership");
    require(std::strcmp(item.identifier, expected_name) == 0, "copied name");
    require(item.elapsed_24 == 0.0f && item.state_30 == 0 && item.last_x == 0 && item.last_y == 0,
        "real base initial state");
    require(item.owned_text == nullptr && item.state_40 == 0 && item.state_41 == 0 &&
                item.state_42 == 0 && item.preparation_flag == 1,
        "real base ownership or flags");
    const ScreenChildNode* head = item.children.head;
    require(head != nullptr && head->next == head && head->previous == head,
        "real empty ring construction");
    // The sentinel's payload is intentionally never read.
}

void attach(ScreenItem& owner, ScreenItem& child)
{
    owner.Unresolved10(&child);
    ++derived_calls;
}

enum class Slot
{
    draw,
    update,
    prepare08,
    position,
    prepared,
    event1c,
    enable,
    move,
    timed,
    word
};

struct Event
{
    unsigned child;
    Slot slot;
    int x = 0;
    int y = 0;
    int value = 0;
    unsigned word = 0;
    float elapsed = 0.0f;
    bool operator==(const Event&) const = default;
};

struct History
{
    std::array<Event, 32> events{};
    unsigned used = 0;
    bool overflow = false;

    void add(Event event) noexcept
    {
        if (used == events.size())
        {
            overflow = true;
            return;
        }
        events[used++] = event;
        ++callbacks;
    }
    void expect(std::initializer_list<Event> expected)
    {
        require(!overflow && used == expected.size(), "callback count");
        unsigned i = 0;
        for (const Event& event : expected)
        {
            require(events[i++] == event, "callback order or argument");
        }
        used = 0;
    }
};

// These are generated observers with genuine base lifetimes. They record only
// the calls being forwarded; they are not implementations of retail widgets.
struct Observer final : ScreenItem
{
    History& history;
    unsigned id;
    bool handles = false;
    GuiDefinedItem* append_owner = nullptr;
    ScreenItem* append_child = nullptr;

    Observer(History& output, unsigned identity)
        : ScreenItem(ScreenRectangle{1, 2, 31, 42})
        , history(output)
        , id(identity)
    {
        kind_28 = 0;
        initialize_untouched(*this);
    }
    ~Observer() override
    {
        ++observer_ends;
    }
    void Unresolved0c() override
    {
        history.add({id, Slot::draw});
        if (append_owner)
        {
            GuiDefinedItem* target = append_owner;
            append_owner = nullptr;
            attach(*target, *append_child);
        }
    }
    void Unresolved04(float value) override
    {
        history.add({id, Slot::update, 0, 0, 0, 0, value});
    }
    void Unresolved08() override
    {
        history.add({id, Slot::prepare08});
    }
    void ApplyPosition(int x, int y) override
    {
        history.add({id, Slot::position, x, y});
    }
    void MarkPrepared() override
    {
        history.add({id, Slot::prepared});
    }
    void Unresolved1c() override
    {
        history.add({id, Slot::event1c});
    }
    void Unresolved24(unsigned char value) override
    {
        history.add({id, Slot::enable, 0, 0, value});
    }
    void MoveRectangle(int x, int y) override
    {
        history.add({id, Slot::move, x, y});
    }
    bool HandleTimedCoordinates(int x, int y, int value) override
    {
        history.add({id, Slot::timed, x, y, value});
        return handles;
    }
    void Unresolved4c(unsigned value) override
    {
        history.add({id, Slot::word, 0, 0, 0, value});
    }
};

void construction()
{
    group = 0;
    RectangleScope rectangle_scope;
    require(same_rectangle(gui_defined_item_rectangle, {0, 0, 0, 0}),
        "actual rectangle owner initial zero");
    PoolSession pool;
    Storage first_storage;
    Storage second_storage;
    char name[] = "named";
    gui_defined_item_rectangle = {-7, 11, 31, 43};
    {
        Owner first(first_storage, name);
        check_constructor(*first.live, {-7, 11, 31, 43}, name, "named");
        initialize_untouched(*first.live);
        const State first_state(*first.live);
        name[0] = 'X';
        gui_defined_item_rectangle = {20, -9, 60, 2};
        {
            const char empty[] = "";
            Owner second(second_storage, empty);
            check_constructor(*second.live, {20, -9, 60, 2}, empty, "");
            require(
                std::strcmp(first.live->identifier, "named") == 0, "borrowed name was retained");
            first_state.check(*first.live);
        }
        pool.seed.check_free({1, 2, 3, 4, 5});
        ScreenItem& item = *first.live;
        item.Unresolved0c();
        item.Unresolved04(0.25f);
        item.Unresolved08();
        item.ApplyPosition(7, -3);
        item.MarkPrepared();
        item.Unresolved1c();
        item.Unresolved24(0x80);
        item.MoveRectangle(-2, 4);
        require(!item.HandleTimedCoordinates(5, -6, 7), "empty timed result");
        item.Unresolved4c(0xf1234567U);
        derived_calls += 10;
        first_state.check(item);
    }
    first_storage.check();
    second_storage.check();
    pool.finish({0, 1, 2, 3, 4, 5});
}

void forwarding()
{
    group = 1;
    History history;
    Observer a(history, 1);
    Observer b(history, 2);
    Observer c(history, 3);
    const State a_state(a), b_state(b), c_state(c);
    const unsigned ended = observer_ends;
    PoolSession pool;
    Storage storage;
    {
        Owner owner(storage, "forward");
        initialize_untouched(*owner.live);
        const State parent_state(*owner.live);
        ScreenItem& item = *owner.live;
        attach(item, a);
        a.append_owner = owner.live;
        a.append_child = &b;
        item.Unresolved0c();
        ++derived_calls;
        history.expect({{1, Slot::draw}, {2, Slot::draw}});
        require(owner.live->children.head->next->item == &a &&
                    owner.live->children.head->previous->item == &b,
            "actual reentrant append");

        item.Unresolved04(0.25f);
        history.expect(
            {{1, Slot::update, 0, 0, 0, 0, 0.25f}, {2, Slot::update, 0, 0, 0, 0, 0.25f}});
        item.Unresolved08();
        history.expect({{1, Slot::prepare08}, {2, Slot::prepare08}});
        item.ApplyPosition(7, -3);
        history.expect({{1, Slot::position, 7, -3}, {2, Slot::position, 7, -3}});
        item.MarkPrepared();
        history.expect({{1, Slot::prepared}, {2, Slot::prepared}});
        item.Unresolved1c();
        history.expect({{1, Slot::event1c}, {2, Slot::event1c}});
        item.Unresolved24(0x80);
        history.expect({{1, Slot::enable, 0, 0, 0x80}, {2, Slot::enable, 0, 0, 0x80}});
        item.MoveRectangle(-2, 4);
        history.expect({{1, Slot::move, -2, 4}, {2, Slot::move, -2, 4}});
        item.Unresolved4c(0xf1234567U);
        history.expect(
            {{1, Slot::word, 0, 0, 0, 0xf1234567U}, {2, Slot::word, 0, 0, 0, 0xf1234567U}});
        derived_calls += 8;

        attach(item, c);
        b.handles = true;
        c.handles = true;
        require(item.HandleTimedCoordinates(5, -6, 7), "first true timed result");
        history.expect({{1, Slot::timed, 5, -6, 7}, {2, Slot::timed, 5, -6, 7}});
        b.handles = false;
        c.handles = false;
        require(!item.HandleTimedCoordinates(-9, 10, -11), "all false timed result");
        history.expect({{1, Slot::timed, -9, 10, -11},
            {2, Slot::timed, -9, 10, -11},
            {3, Slot::timed, -9, 10, -11}});
        derived_calls += 2;
        parent_state.check(item);
        a_state.check(a);
        b_state.check(b);
        c_state.check(c);
    }
    storage.check();
    pool.finish({0, 3, 2, 1, 4, 5});
    require(observer_ends == ended, "forwarding owner destroyed a borrowed child");
}

void destruction()
{
    group = 2;
    History history;
    Observer a(history, 1);
    Observer b(history, 2);
    const State a_state(a), b_state(b);
    const unsigned ended = observer_ends;
    PoolSession pool;
    Storage storage;
    {
        Owner owner(storage, "owned-name");
        check_constructor(*owner.live, {0, 0, 0, 0}, "owned-name", "owned-name");
        attach(*owner.live, a);
        attach(*owner.live, b);
        ScreenItem& item = *owner.live;
        item.SetOwnedText("owned-base");
        ++inherited_text_calls;
        require(item.owned_text != nullptr && item.owned_text != owner.live->identifier &&
                    std::strcmp(item.owned_text, "owned-base") == 0,
            "distinct real base text allocation");
        owner.finish();
        // The owner and ScreenChildNode lifetimes have ended. Only the still-live
        // backing guards, pool's new PoolChunkLink objects and children are read.
        storage.check();
        pool.seed.check_free({0, 2, 1, 3, 4, 5});
        require(observer_ends == ended, "ordinary destructor destroyed borrowed children");
        a_state.check(a);
        b_state.check(b);
    }
    {
        std::unique_ptr<ScreenItem> owner(new GuiDefinedItem("virtual-owner"));
        ++owners;
        attach(*owner, a);
        owner->SetOwnedText("virtual-base");
        ++inherited_text_calls;
        require(std::strcmp(owner->owned_text, "virtual-base") == 0, "virtual owner base text");
        owner.reset(); // Actual derived, member and base destructors plus host scalar release.
        ++owner_ends;
        pool.seed.check_free({0, 2, 1, 3, 4, 5});
        require(observer_ends == ended, "virtual destructor destroyed a borrowed child");
    }
    a.Unresolved08();
    b.Unresolved08();
    history.expect({{1, Slot::prepare08}, {2, Slot::prepare08}});
    a_state.check(a);
    b_state.check(b);
    pool.finish({0, 2, 1, 3, 4, 5});
}
} // namespace

// Exact qualified R162/R166 uncontended test-only platform boundary. This is
// not an atomic/concurrent implementation or a gameplay provider. Real pool
// methods own all node allocation, lifetime transitions and release calls.
extern "C" LONG InterlockedExchange(LONG* target, LONG value)
{
    volatile LONG* word = target;
    require(pool_phase && target == &pool_lock_word && value == 1 && *word == 0,
        "unexpected child-ring exchange");
    *word = value;
    ++exchanges;
    return 0;
}
extern "C" void Sleep(DWORD)
{
    require(false, "unexpected child-ring wait");
}

int main()
{
    try
    {
        construction();
        forwarding();
        destruction();
        require(owners == 5 && owner_ends == 5 && observer_ends == 5,
            "literal owner and borrowed-child lifetime totals");
        require(
            derived_calls == 27 && inherited_text_calls == 2 && callbacks == 25 && exchanges == 22,
            "literal operation totals");
        require(!pool_phase && pool_lock_word == 0 && pool_chunk_begin == nullptr &&
                    pool_chunk_end == nullptr && pool_heap_bytes == 0,
            "final pool state");
        for (auto* root : pool_chunk_buckets)
        {
            require(root == nullptr, "final pool root remains");
        }
        require(
            same_rectangle(gui_defined_item_rectangle, {0, 0, 0, 0}), "global was not restored");
        std::printf("groups=3 owners=%u owner-ends=%u child-ends=%u derived-calls=%u "
                    "text-calls=%u callbacks=%u exchanges=%u checks=%llu guard-bytes=%llu "
                    "item-size=%zu defined-size=%zu node-size=%zu\n",
            owners,
            owner_ends,
            observer_ends,
            derived_calls,
            inherited_text_calls,
            callbacks,
            exchanges,
            checks,
            guard_bytes,
            sizeof(ScreenItem),
            sizeof(GuiDefinedItem),
            sizeof(ScreenChildNode));
        return 0;
    }
    catch (const Failure& failure)
    {
        // All genuine owners and generated pool backing have already unwound.
        std::fprintf(stderr, "defined-item group %u: %s\n", group, failure.message);
        return 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "defined-item host failure: %s\n", error.what());
        return 3;
    }
}

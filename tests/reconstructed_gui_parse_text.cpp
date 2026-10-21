#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"
#include "startup_screen_manager.h"
#include "startup_screen_text.h"
#include "startup_screen_text_item.h"

#include <algorithm>
#include <array>
#include <climits>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

namespace
{
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(unsigned short) == 2);
static_assert(std::is_aggregate_v<GuiParserState> && std::is_standard_layout_v<GuiParserState>);
static_assert(std::is_aggregate_v<ScreenManager> && std::is_aggregate_v<ScreenTextLookup>);
using Guard = std::array<unsigned char, 16>;
unsigned int checks = 0;
unsigned int calls = 0;
unsigned int released_links = 0;
const char* current_case = "setup";

struct Failure
{
    const char* reason;
};
void require(bool value, const char* reason)
{
    ++checks;
    if (!value)
    {
        throw Failure{reason};
    }
}

template <typename T> struct Guarded
{
    Guard before;
    T value{};
    Guard after;
    Guarded()
    {
        before.fill(0x26);
        after.fill(0x62);
    }
    void verify() const
    {
        require(std::all_of(before.begin(),
                    before.end(),
                    [](auto v)
                    {
                        return v == 0x26;
                    }) &&
                    std::all_of(after.begin(),
                        after.end(),
                        [](auto v)
                        {
                            return v == 0x62;
                        }),
            "outside guard differs");
    }
};

bool rectangle(const ScreenRectangle& a, const ScreenRectangle& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// Existing localization prefix: borrowed typed records and terminated text.
// No complete manager or localization-owner constructor is supplied here.
struct Lookup
{
    Guarded<ScreenTextLookup> lookup;
    Guarded<std::array<ScreenTextBucket, 3>> buckets;
    Guarded<std::array<ScreenTextValue, 2>> values;
    Guarded<std::array<unsigned short, 4>> title;
    Guarded<std::array<unsigned short, 4>> label;
    ScreenTextLookup* previous = screen_text_lookup;

    Lookup()
    {
        title.value = {0, 65, 0, 0};
        label.value = {0, 66, 0, 0};
        values.value = {{{"TITLE", title.value.data() + 1}, {"LABEL", label.value.data() + 1}}};
        buckets.value = {{{"TITLE", &values.value[0], 0, {1, 2, 3}},
            {"LABEL", &values.value[1], 0, {4, 5, 6}},
            {nullptr, nullptr, 1, {7, 8, 9}}}};
        lookup.value.end_index = 2;
        lookup.value.buckets = buckets.value.data();
        lookup.value.bucket_count = 2;
        screen_text_lookup = &lookup.value;
    }
    ~Lookup()
    {
        screen_text_lookup = previous;
    }
    void verify() const
    {
        require(screen_text_lookup == &lookup.value && lookup.value.end_index == 2 &&
                    lookup.value.bucket_count == 2 && lookup.value.buckets == buckets.value.data(),
            "lookup publication differs");
        require(title.value == std::array<unsigned short, 4>{0, 65, 0, 0} &&
                    label.value == std::array<unsigned short, 4>{0, 66, 0, 0},
            "borrowed text changed");
        for (std::size_t i = 0; i < 2; ++i)
        {
            require(buckets.value[i].value == &values.value[i] &&
                        buckets.value[i].unavailable == 0 &&
                        std::strcmp(buckets.value[i].key, i == 0 ? "TITLE" : "LABEL") == 0 &&
                        values.value[i].text ==
                            (i == 0 ? title.value.data() + 1 : label.value.data() + 1),
                "borrowed record changed");
        }
        lookup.verify();
        buckets.verify();
        values.verify();
        title.verify();
        label.verify();
    }
};

// This is the same qualified parser prefix used by the readers and ParseTGA.
// Its aggregate storage is a borrowed read model, not the full retail parser.
struct Session
{
    Guarded<ScreenManager> manager;
    Guarded<GuiParserState> parser;
    Session()
    {
        parser.value.manager = &manager.value;
#if defined(BEACHLIFE_DECOMP_DEFINED_ITEMS)
        parser.value.defined_items = nullptr;
#else
        std::fill(std::begin(parser.value.unknown_24), std::end(parser.value.unknown_24), 0x75);
#endif
    }
    ~Session()
    {
        clear();
    }
    void clear()
    {
        // The control adds duplicate borrowed registry references. Relinquish
        // those references before genuine link destruction; the all-items chain
        // remains the sole owner of each real Text object, as in the TGA fixture.
        while (parser.value.registry_head)
        {
            auto* link = parser.value.registry_head;
            parser.value.registry_head = link->next;
            link->item = nullptr;
            delete link;
        }
        parser.value.registry_tail = nullptr;
        while (parser.value.items_head)
        {
            auto* link = parser.value.items_head;
            parser.value.items_head = link->next;
            delete link; // Real kind12 link -> virtual Text destructor -> base.
            ++released_links;
        }
        parser.value.items_tail = nullptr;
        parser.value.last_registered_item = nullptr;
    }
};

void preserved(const GuiParserState& p, const GuiParserState& before)
{
    require(p.manager == before.manager && p.buffer == before.buffer &&
                p.current_context == before.current_context &&
                p.alternate_context_10 == before.alternate_context_10 &&
                p.alternate_context_14 == before.alternate_context_14 &&
                p.context_head == before.context_head &&
                p.previous_context == before.previous_context && p.word_38 == before.word_38 &&
                p.word_3c == before.word_3c &&
#if defined(BEACHLIFE_DECOMP_DEFINED_ITEMS)
                p.defined_items == before.defined_items,
#else
                std::equal(std::begin(p.unknown_24),
                    std::end(p.unknown_24),
                    std::begin(before.unknown_24)),
#endif
        "unrelated parser member changed");
}

ScreenTextItem* item(ScreenChildLink* link,
    ScreenRectangle expected,
    const unsigned short* text,
    int font,
    int colour,
    int flags,
    unsigned int word)
{
    require(link != nullptr, "missing real link");
    auto* result = dynamic_cast<ScreenTextItem*>(link->item);
    require(result != nullptr, "registered item is not genuine Text");
    require(rectangle(result->rectangle, expected) &&
                rectangle(result->saved_rectangle, expected) && result->kind_28 == 12 &&
                result->word_2c == word && result->borrowed_text == text &&
                result->owned_text_48 == nullptr && result->font_index == font &&
                result->word_50 == colour && result->word_54 == flags && result->state_58 == 1,
        "Text arguments/publication differ");
    require(result->elapsed_24 == 0.0f && result->state_30 == 0 && result->last_x == 0 &&
                result->last_y == 0 && result->owned_text == nullptr && result->state_40 == 0 &&
                result->state_41 == 0 && result->state_42 == 0 && result->preparation_flag == 1,
        "real base construction differs");
    // Heap constructor padding/untouched opaque bytes have no invented value.
    return result;
}

void unnamed_and_append(Lookup& lookup)
{
    current_case = "unnamed-registration";
    Session session;
    auto& p = session.parser.value;
    constexpr char first[] = "(1,2,3,4,TITLE,1,2,4)";
    p.buffer = first;
    p.word_38 = 0x13579bdfU;
    const auto before_first = p;
    p.ParseText();
    ++calls;
    require(p.registry_head == nullptr && p.registry_tail == nullptr, "unexpected named registry");
    require(p.cursor == 21 && p.items_head == p.items_tail && p.items_head &&
                p.items_head->next == nullptr,
        "first cursor/link differs");
    auto* first_item =
        item(p.items_head, {1, 2, 3, 4}, lookup.title.value.data() + 1, 1, 2, 4, 0x13579bdfU);
    require(p.last_registered_item == first_item, "first last-item publication differs");
    preserved(p, before_first);
    auto* first_link = p.items_head;

    current_case = "append-empty-lookup";
    constexpr char second[] = "(5,6,7,8,MISSING,0,9,0)TAIL";
    p.cursor = 0;
    p.buffer = second;
    p.word_38 = 0x2468ace0U;
    const auto before_second = p;
    p.ParseText();
    ++calls;
    require(p.registry_head == nullptr && p.registry_tail == nullptr, "unexpected named registry");
    require(p.cursor == 23 && std::strcmp(p.buffer + p.cursor, "TAIL") == 0 &&
                p.items_head == first_link && first_link->next == p.items_tail && p.items_tail &&
                p.items_tail != first_link && p.items_tail->next == nullptr,
        "append cursor/link differs");
    auto* second_item = item(p.items_tail, {5, 6, 7, 8}, screen_text_empty, 0, 9, 0, 0x2468ace0U);
    require(p.last_registered_item == second_item && first_item != second_item &&
                first_link->item == first_item && first_item->word_2c == 0x13579bdfU &&
                first_item->borrowed_text == lookup.title.value.data() + 1,
        "append changed first item");
    preserved(p, before_second);

    // Exercise the actual derived/base ownership through the real link cleanup.
    // This is a deliberate valid fixture field assignment after handler checks.
    first_item->SetOwnedText("parsed-base-owned");
    constexpr unsigned short owned[] = {90, 0};
    first_item->owned_text_48 = static_cast<unsigned short*>(::operator new(sizeof(owned)));
    std::memcpy(first_item->owned_text_48, owned, sizeof(owned));
    session.clear();
    session.parser.verify();
    session.manager.verify();
    lookup.verify();
}

void parent_and_open_tail(Lookup& lookup)
{
    current_case = "parent-open-tail";
    ScreenItem parent({10, 20, 100, 200});
    parent.kind_28 = 0;
    parent.word_2c = 0x31415926U;
    ScreenChildLink history(nullptr);
    Session session;
    auto& p = session.parser.value;
    constexpr char text[] = "xx( \t1,2,3,4,LABEL,0,65535,65, \r\nNEXT";
    p.buffer = text;
    p.cursor = 2;
    p.current_context = &parent;
    p.previous_context = &parent;
    p.context_head = &history;
    p.word_38 = 0xfedcba98U;
    const auto before = p;
    p.ParseText();
    ++calls;
    require(p.registry_head == nullptr && p.registry_tail == nullptr, "unexpected named registry");
    require(p.cursor == 33 && std::strcmp(p.buffer + p.cursor, "NEXT") == 0 &&
                p.items_head == p.items_tail && p.items_head && p.items_head->next == nullptr,
        "parent cursor/link differs");
    auto* child = item(
        p.items_head, {11, 22, 13, 24}, lookup.label.value.data() + 1, 0, 65535, 65, 0xfedcba98U);
    require(p.last_registered_item == child && history.next == nullptr && history.item == nullptr,
        "context history or last-item differs");
    preserved(p, before);
    require(rectangle(parent.rectangle, {10, 20, 100, 200}) &&
                rectangle(parent.saved_rectangle, {10, 20, 100, 200}) && parent.kind_28 == 0 &&
                parent.word_2c == 0x31415926U && parent.preparation_flag == 1,
        "real parent callback changed state");
    session.clear();
    session.parser.verify();
    session.manager.verify();
    lookup.verify();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--fatal-open") == 0)
    {
        GuiParserState parser{};
        parser.buffer = "xx!";
        parser.cursor = 2;
        std::fputs("gui-parse-text fatal-before\n", stderr);
        std::fflush(stderr);
        parser.ParseText(); // Actual StartupFatal exits0; successful return is a failure.
        std::fputs("gui-parse-text fatal-returned\n", stderr);
        return 3;
    }
    try
    {
        require(argc == 1, "unexpected arguments");
        require(std::setlocale(LC_CTYPE, "C") != nullptr, "C locale unavailable");
        ScreenTextLookup* previous = screen_text_lookup;
        {
            Lookup lookup;
            unnamed_and_append(lookup);
            parent_and_open_tail(lookup);
        }
        require(screen_text_lookup == previous && calls == 3 && released_links == 3,
            "completion/ownership totals differ");
        std::printf("gui-parse-text groups=2 calls=%u released_links=%u checks=%u pass\n",
            calls,
            released_links,
            checks);
        return 0;
    }
    catch (const Failure& failure)
    {
        std::fprintf(stderr, "gui-parse-text %s: %s\n", current_case, failure.reason);
        return 2;
    }
}

#include "startup_gui_parser.h"
#include "startup_screen_child_link.h"
#include "startup_screen_manager.h"
#include "startup_tga_screen_item.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

namespace
{
struct AllocationProbe
{
    bool active = false;
    unsigned attempts = 0;
    unsigned releases = 0;
    void* widget = nullptr;
} probe;

unsigned checks;

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

// Genuine observed-prefix storage is borrowed by the real widget callbacks.
// This does not instantiate the retail manager or parser's complete lifetime.
struct Session
{
    ScreenManager manager{};
    GuiParserState parser{};
    ScreenManager* previous = screen_manager;

    Session()
    {
        screen_manager = &manager;
    }

    ~Session()
    {
        // Named links borrow items also present in the owning all-items chain.
        // Clearing the borrow prevents the genuine link destructor owning it twice.
        while (parser.registry_head)
        {
            auto* link = parser.registry_head;
            parser.registry_head = link->next;
            link->item = nullptr;
            delete link;
        }
        while (parser.items_head)
        {
            auto* link = parser.items_head;
            parser.items_head = link->next;
            delete link;
        }
        screen_manager = previous;
    }
};

bool rectangle(const ScreenRectangle& value, int left, int top, int right, int bottom)
{
    return value.left == left && value.top == top && value.right == right && value.bottom == bottom;
}

void valid_commands()
{
    Session session;
    auto& parser = session.parser;
    const char first[] = "( \t10,20,110,220,first.tga)";
    parser.buffer = first;
    parser.word_38 = 0x13579bdfu;
    parser.ParseTGA();
    require(parser.registry_head == nullptr && parser.registry_tail == nullptr,
        "TGA command unexpectedly creates a named registry link");
    require(parser.cursor == sizeof(first) - 1, "first TGA cursor differs");
    require(parser.items_head && parser.items_head == parser.items_tail &&
                parser.items_head->next == nullptr,
        "first TGA owning link differs");
    auto* first_item = dynamic_cast<TgaScreenItem*>(parser.items_head->item);
    require(first_item && parser.last_registered_item == first_item,
        "first TGA is not the genuine registered widget");
    require(rectangle(first_item->rectangle, 10, 20, 110, 220) &&
                rectangle(first_item->saved_rectangle, 10, 20, 110, 220),
        "first TGA rectangle differs");
    require(std::strcmp(first_item->filename, "first.tga") == 0 && first_item->image == nullptr,
        "first TGA filename or image differs");
    require(first_item->kind_28 == 16 && first_item->word_2c == 0x13579bdfu,
        "first TGA kind or registration word differs");

    const char second[] = "(1,2,31,42,second.tga)";
    parser.cursor = 0;
    parser.buffer = second;
    parser.current_context = first_item;
    parser.word_38 = 0x2468ace0u;
    parser.ParseTGA();
    require(parser.cursor == sizeof(second) - 1, "second TGA cursor differs");
    require(parser.registry_head == nullptr && parser.registry_tail == nullptr,
        "second TGA creates a named registry link");
    require(parser.items_head->next == parser.items_tail && parser.items_tail->next == nullptr,
        "second TGA does not append a genuine owning link");
    auto* second_item = dynamic_cast<TgaScreenItem*>(parser.items_tail->item);
    require(second_item && second_item != first_item && parser.last_registered_item == second_item,
        "second TGA widget publication differs");
    require(rectangle(second_item->rectangle, 11, 22, 41, 62),
        "second TGA does not use the real parent rectangle");
    require(std::strcmp(second_item->filename, "second.tga") == 0 && second_item->kind_28 == 16 &&
                second_item->image == nullptr && second_item->word_2c == 0x2468ace0u &&
                first_item->word_2c == 0x13579bdfu,
        "second TGA filename, state or preserved first widget differs");
}

void registration_failure()
{
    Session session;
    struct Cleanup
    {
        GuiParserState& parser;
        ~Cleanup()
        {
            probe.active = false;
            if (probe.widget && probe.releases == 0 &&
                (!parser.items_head || parser.items_head->item != probe.widget))
            {
                delete static_cast<TgaScreenItem*>(probe.widget);
            }
            probe = {};
        }
    } cleanup{session.parser};
    session.parser.buffer = "(1,2,3,4,failed.tga)";
    probe.active = true;
    bool threw = false;
    try
    {
        session.parser.ParseTGA();
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    probe.active = false;
    require(threw && probe.attempts == 2 && probe.widget,
        "registration failure did not follow real widget allocation");
    require(probe.releases == 0, "registration failure deletes the completed widget");
    auto* widget = static_cast<TgaScreenItem*>(probe.widget);
    require(widget->kind_28 == 16 && std::strcmp(widget->filename, "failed.tga") == 0,
        "registration failure loses the completed widget");
    require(session.parser.items_head == nullptr && session.parser.items_tail == nullptr &&
                session.parser.last_registered_item == nullptr,
        "failed owning-link allocation publishes an item");
}
} // namespace

void* operator new(std::size_t bytes)
{
    if (probe.active && ++probe.attempts == 2)
    {
        throw std::bad_alloc();
    }
    void* value = std::malloc(bytes ? bytes : 1);
    if (!value)
    {
        throw std::bad_alloc();
    }
    if (probe.active && probe.attempts == 1)
    {
        probe.widget = value;
    }
    return value;
}

void operator delete(void* value) noexcept
{
    if (value && value == probe.widget)
    {
        ++probe.releases;
    }
    std::free(value);
}

void operator delete(void* value, std::size_t) noexcept
{
    ::operator delete(value);
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--fatal") == 0)
    {
        GuiParserState parser{};
        parser.buffer = "missing opening bracket";
        parser.ParseTGA();
        return 3;
    }
    try
    {
        valid_commands();
        registration_failure();
        require(screen_manager == nullptr, "borrowed manager binding is not restored");
        std::printf("groups=2 calls=3 checks=%u gui-tga-command=pass\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}

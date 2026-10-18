#include "startup_config_tokens.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace allocation_probe
{
constexpr std::size_t guard_size = 64;
constexpr unsigned char guard_value = 0xd7;
constexpr std::size_t maximum_request = 1024 * sizeof(ConfigToken);

struct Observation
{
    std::size_t calls = 0;
    std::size_t bytes = 0;
    void* memory = nullptr;
    bool unexpected_request = false;
};
Observation observed;
bool active = false;
bool force_failure = false;

void* allocate(std::size_t bytes) noexcept
{
    if (active)
    {
        ++observed.calls;
        observed.bytes = bytes;
        if (observed.calls != 1 || bytes > maximum_request)
        {
            observed.unexpected_request = true;
            return nullptr;
        }
        if (force_failure)
        {
            return nullptr;
        }
    }
    try
    {
        // The ordinary array allocator and every deallocator remain real host
        // functions. Extra bytes provide a suffix canary without moving the
        // returned pointer or changing its allocation/deallocation pairing.
        void* memory = ::operator new[](bytes + (active ? guard_size : 0));
        if (active)
        {
            observed.memory = memory;
            std::memset(memory, 0xa5, bytes);
            std::memset(static_cast<unsigned char*>(memory) + bytes, guard_value, guard_size);
        }
        return memory;
    }
    catch (...)
    {
        return nullptr;
    }
}
} // namespace allocation_probe

// This labelled test double replaces only the standard nonthrowing array
// overload. It injects null; it never makes ordinary throwing new return null.
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    return allocation_probe::allocate(bytes);
}

namespace
{
constexpr unsigned int maximum_count = 1024;
constexpr unsigned int maximum_previous_count = 17;
constexpr std::size_t owner_guard_size = 32;
std::size_t checks = 0;
std::size_t success_cases = 0;
std::size_t failure_cases = 0;
std::size_t initialized_tokens = 0;

static_assert(sizeof(unsigned int) == 4);
static_assert(std::is_trivially_destructible_v<ConfigToken>);
static_assert(alignof(ConfigToken) <= alignof(std::max_align_t));

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

struct GuardedOwner
{
    std::array<unsigned char, owner_guard_size> before;
    ConfigTokenBuffer owner;
    std::array<unsigned char, owner_guard_size> after;

    GuardedOwner()
    {
        before.fill(0x93);
        after.fill(0x6c);
        owner.tokens = nullptr;
        owner.count = 0;
    }

    void verify() const
    {
        expect(std::all_of(before.begin(),
                   before.end(),
                   [](unsigned char c)
                   {
                       return c == 0x93;
                   }),
            "Allocation changed bytes before its owner.");
        expect(std::all_of(after.begin(),
                   after.end(),
                   [](unsigned char c)
                   {
                       return c == 0x6c;
                   }),
            "Allocation changed bytes after its owner.");
    }
};

char name_byte(unsigned int index, unsigned int byte)
{
    return static_cast<char>('a' + (index * 3u + byte) % 26u);
}

char text_byte(unsigned int index, unsigned int byte)
{
    return static_cast<char>('A' + (index * 5u + byte) % 26u);
}

unsigned int token_kind(unsigned int index)
{
    constexpr std::array<unsigned int, 7> values{0, 1, 2, 3, 0xff, 0x80000000u, 0xffffffffu};
    return values[index % values.size()];
}

void populate(ConfigToken& token, unsigned int index, unsigned int mask)
{
    token.kind = token_kind(index);
    if (mask & 1u)
    {
        const auto size = 1u + index % 5u;
        token.name = new char[size + allocation_probe::guard_size];
        for (unsigned int byte = 0; byte < size; ++byte)
        {
            token.name[byte] = name_byte(index, byte);
        }
        std::memset(token.name + size, allocation_probe::guard_value, allocation_probe::guard_size);
    }
    if (mask & 2u)
    {
        const auto size = 1u + index % 7u;
        token.text = new char[size + allocation_probe::guard_size];
        for (unsigned int byte = 0; byte < size; ++byte)
        {
            token.text[byte] = text_byte(index, byte);
        }
        std::memset(token.text + size, allocation_probe::guard_value, allocation_probe::guard_size);
    }
    if (mask & 4u)
    {
        token.number = new float(static_cast<float>(index) + 0.25f);
    }
}

void verify_payload(const ConfigToken& token, unsigned int index, unsigned int mask)
{
    expect(token.kind == token_kind(index), "Token kind changed during its allocation lifetime.");
    expect((token.name != nullptr) == ((mask & 1u) != 0), "Name ownership changed.");
    expect((token.text != nullptr) == ((mask & 2u) != 0), "Text ownership changed.");
    expect((token.number != nullptr) == ((mask & 4u) != 0), "Number ownership changed.");
    if (token.name)
    {
        const auto size = 1u + index % 5u;
        for (unsigned int byte = 0; byte < size; ++byte)
        {
            expect(token.name[byte] == name_byte(index, byte), "Name payload changed.");
        }
        for (std::size_t byte = 0; byte < allocation_probe::guard_size; ++byte)
        {
            expect(static_cast<unsigned char>(token.name[size + byte]) ==
                       allocation_probe::guard_value,
                "Name suffix canary changed.");
        }
    }
    if (token.text)
    {
        const auto size = 1u + index % 7u;
        for (unsigned int byte = 0; byte < size; ++byte)
        {
            expect(token.text[byte] == text_byte(index, byte), "Text payload changed.");
        }
        for (std::size_t byte = 0; byte < allocation_probe::guard_size; ++byte)
        {
            expect(static_cast<unsigned char>(token.text[size + byte]) ==
                       allocation_probe::guard_value,
                "Text suffix canary changed.");
        }
    }
    if (token.number)
    {
        expect(*token.number == static_cast<float>(index) + 0.25f, "Number payload changed.");
    }
}

struct PreviousTable
{
    GuardedOwner storage;
    std::array<unsigned char, (maximum_previous_count + 1) * sizeof(ConfigToken)> snapshot{};
    std::size_t snapshot_size = 0;

    explicit PreviousTable(int count)
    {
        if (count < 0)
        {
            return;
        }
        if (static_cast<unsigned int>(count) > maximum_previous_count)
        {
            throw std::logic_error("Previous table exceeds the generated test domain.");
        }
        // The final trivial record is a canary, never an owned payload record.
        storage.owner.tokens = new ConfigToken[static_cast<unsigned int>(count) + 1]{};
        storage.owner.count = static_cast<unsigned int>(count);
        for (unsigned int index = 0; index < storage.owner.count; ++index)
        {
            populate(storage.owner.tokens[index], index, 7);
        }
        std::memset(storage.owner.tokens + count, 0xb6, sizeof(ConfigToken));
        snapshot_size = (static_cast<unsigned int>(count) + 1) * sizeof(ConfigToken);
        std::memcpy(snapshot.data(), storage.owner.tokens, snapshot_size);
    }

    void verify() const
    {
        storage.verify();
        if (snapshot_size)
        {
            expect(std::memcmp(snapshot.data(), storage.owner.tokens, snapshot_size) == 0,
                "Allocator changed the old table or its canary.");
            for (unsigned int index = 0; index < storage.owner.count; ++index)
            {
                verify_payload(storage.owner.tokens[index], index, 7);
            }
        }
    }
};

void verify_table_guard(const allocation_probe::Observation& observed)
{
    expect(observed.memory != nullptr, "A successful allocation needs real storage.");
    const auto* bytes = static_cast<const unsigned char*>(observed.memory);
    for (std::size_t byte = 0; byte < allocation_probe::guard_size; ++byte)
    {
        expect(bytes[observed.bytes + byte] == allocation_probe::guard_value,
            "Token initialization changed the table suffix canary.");
    }
}

void exercise(unsigned int count, int previous_count, unsigned int mask, bool fail)
{
    if (count > maximum_count)
    {
        throw std::logic_error("Requested count exceeds the generated test domain.");
    }
    PreviousTable previous(previous_count);
    GuardedOwner storage;
    storage.owner.tokens = previous.storage.owner.tokens;
    storage.owner.count = previous.storage.owner.count;
    const auto old_count = storage.owner.count;
    allocation_probe::observed = {};
    allocation_probe::force_failure = fail;
    allocation_probe::active = true;
    int result = -1;
    try
    {
        result = storage.owner.AllocateTokens(count);
    }
    catch (...)
    {
        allocation_probe::active = false;
        storage.owner.tokens = nullptr;
        storage.owner.count = 0;
        throw;
    }
    allocation_probe::active = false;
    const auto observed = allocation_probe::observed;
    const auto* published = storage.owner.tokens;
    const auto published_count = storage.owner.count;
    // Save the exact retail result before recovering test ownership. A failed
    // call may leave null tokens with a nonzero count, unsafe for the destructor.
    // The previous table remains independently owned even when retail loses it.
    storage.owner.tokens = nullptr;
    storage.owner.count = 0;
    std::unique_ptr<ConfigToken[]> table(static_cast<ConfigToken*>(observed.memory));

    expect(!observed.unexpected_request, "Unexpected nonthrowing allocation request.");
    expect(observed.calls == 1, "Each call must request exactly one token table.");
    expect(observed.bytes == static_cast<std::size_t>(count) * sizeof(ConfigToken),
        "Allocation request must use the complete native token count and size.");
    storage.verify();
    previous.verify();
    if (fail)
    {
        expect(result == 0, "A null allocation must return exactly failure0.");
        expect(
            published == nullptr, "A failed allocation must overwrite the old pointer with null.");
        expect(published_count == old_count,
            "A failed allocation must leave the old count unchanged.");
        expect(observed.memory == nullptr, "Injected failure unexpectedly allocated memory.");
        ++failure_cases;
        return;
    }

    expect(result == 1, "A nonnull allocation must return exactly success1.");
    expect(published == observed.memory && published != nullptr,
        "Successful allocation must publish the new table, including for count0.");
    expect(published_count == count, "Successful allocation must publish the requested count.");
    verify_table_guard(observed);
    for (unsigned int index = 0; index < count; ++index)
    {
        expect(table[index].name == nullptr, "Allocation did not reset every name pointer.");
        expect(table[index].kind == 0, "Allocation did not reset every kind value.");
        expect(table[index].text == nullptr, "Allocation did not reset every text pointer.");
        expect(table[index].number == nullptr, "Allocation did not reset every number pointer.");
    }

    {
        GuardedOwner lifetime;
        lifetime.owner.tokens = table.release();
        lifetime.owner.count = count;
        for (unsigned int index = 0; index < count; ++index)
        {
            populate(lifetime.owner.tokens[index], index, (mask + 3u * index) & 7u);
        }
        for (unsigned int index = 0; index < count; ++index)
        {
            verify_payload(lifetime.owner.tokens[index], index, (mask + 3u * index) & 7u);
        }
        verify_table_guard(observed);
        lifetime.verify();
        // The actual reconstructed destructor owns every new payload and table.
        // Its release order is covered by reconstructed_config_tokens; this
        // fixture deliberately leaves all host delete functions untouched.
    }
    previous.verify();
    initialized_tokens += count;
    ++success_cases;
}

void exercise_count(unsigned int count)
{
    for (const int previous_count : {-1, 0, 3, 17})
    {
        exercise(count, previous_count, 0, true);
        for (unsigned int mask = 0; mask < 8; ++mask)
        {
            exercise(count, previous_count, mask, false);
        }
    }
}
} // namespace

int main()
{
    try
    {
        // This qualification covers bounded counts and the default handler.
        // Custom legacy handlers and oversized/wrapping counts are separate.
        expect(std::get_new_handler() == nullptr, "This fixture requires the default new handler.");
        for (unsigned int count = 0; count <= 64; ++count)
        {
            exercise_count(count);
        }
        for (const unsigned int count :
            {95u, 127u, 128u, 129u, 255u, 256u, 257u, 511u, 512u, 1023u, 1024u})
        {
            exercise_count(count);
        }
        std::cout << checks << " reconstructed allocation checks passed across " << success_cases
                  << " successful tables, " << failure_cases << " failures and "
                  << initialized_tokens << " initialized tokens\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

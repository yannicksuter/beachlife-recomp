#include "startup_config_tokens.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace allocations
{
enum class Kind
{
    array,
    scalar
};

constexpr std::size_t guard = alignof(std::max_align_t);
constexpr std::size_t capacity = 128;
struct Record
{
    unsigned char* base = nullptr;
    std::uintptr_t address = 0;
    std::size_t bytes = 0;
    Kind kind = Kind::array;
    bool released = false;
};
std::array<Record, capacity> records{};
std::array<std::size_t, capacity> release_order{};
std::size_t count = 0;
std::size_t releases = 0;
bool active = false;

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "Token allocation check failed: %s\n", message);
    std::abort();
}

void begin()
{
    if (active)
    {
        fail("nested allocation fixture");
    }
    records.fill({});
    count = 0;
    releases = 0;
    active = true;
}

void* allocate(std::size_t bytes, Kind kind)
{
    if (!active)
    {
        if (void* result = std::malloc(bytes ? bytes : 1))
        {
            return result;
        }
        throw std::bad_alloc();
    }
    if (count == capacity || bytes > 1024 * 1024)
    {
        fail("unexpected allocation");
    }
    auto* base = static_cast<unsigned char*>(std::malloc(bytes + 2 * guard));
    if (!base)
    {
        throw std::bad_alloc();
    }
    std::memset(base, 0xa5, bytes + 2 * guard);
    records[count++] = {base, reinterpret_cast<std::uintptr_t>(base + guard), bytes, kind, false};
    return base + guard;
}

void release(void* memory, Kind kind) noexcept
{
    if (!memory)
    {
        return;
    }
    if (!active)
    {
        std::free(memory);
        return;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        auto& record = records[index];
        if (reinterpret_cast<std::uintptr_t>(memory) != record.address)
        {
            continue;
        }
        if (record.released)
        {
            fail("allocation released twice");
        }
        if (record.kind != kind)
        {
            fail("scalar and array release forms differ");
        }
        for (std::size_t byte = 0; byte < guard; ++byte)
        {
            if (record.base[byte] != 0xa5 || record.base[guard + record.bytes + byte] != 0xa5)
            {
                fail("allocation guard changed");
            }
        }
        record.released = true;
        release_order[releases++] = index;
        std::free(record.base);
        return;
    }
    fail("attempted to release nonowned storage");
}

void finish()
{
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!records[index].released)
        {
            fail("owned allocation was not released");
        }
    }
    active = false;
}
} // namespace allocations

// Standard replaceable allocation functions keep the trace portable. Only this
// test executable replaces them; reconstructed source uses the host C++ API.
void* operator new(std::size_t bytes)
{
    return allocations::allocate(bytes, allocations::Kind::scalar);
}

void* operator new[](std::size_t bytes)
{
    return allocations::allocate(bytes, allocations::Kind::array);
}

void operator delete(void* memory) noexcept
{
    allocations::release(memory, allocations::Kind::scalar);
}

void operator delete(void* memory, std::size_t) noexcept
{
    allocations::release(memory, allocations::Kind::scalar);
}

void operator delete[](void* memory) noexcept
{
    allocations::release(memory, allocations::Kind::array);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    allocations::release(memory, allocations::Kind::array);
}

namespace
{
constexpr std::size_t maximum_tokens = 33;
constexpr std::size_t absent = allocations::capacity;
std::size_t checks = 0;
std::size_t cleanup_cases = 0;
std::size_t reset_cases = 0;

struct alignas(ConfigTokenBuffer) OwnerStorage
{
    std::array<unsigned char, 2 * allocations::guard + sizeof(ConfigTokenBuffer)> bytes;
};
static_assert(allocations::guard % alignof(ConfigTokenBuffer) == 0);
static_assert(alignof(ConfigToken) <= alignof(std::max_align_t));
static_assert(sizeof(unsigned int) == 4);

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

bool same_fields(const ConfigToken& first, const ConfigToken& second)
{
    return first.name == second.name && first.kind == second.kind && first.text == second.text &&
           first.number == second.number;
}

void cleanup(
    OwnerStorage& storage, unsigned int count, unsigned int mask, bool vary, bool null_table)
{
    if (count > maximum_tokens || (null_table && count != 0))
    {
        throw std::logic_error("Invalid generated token ownership domain.");
    }
    storage.bytes.fill(0xa5);
    // Construct through the real missing-path loader before observing allocations.
    auto* owner = new (storage.bytes.data() + allocations::guard)
        ConfigTokenBuffer("Data/generated-unbound.pis");
    std::array<std::array<std::size_t, 3>, maximum_tokens> field_allocations{};
    allocations::begin();
    ConfigToken* table = null_table ? nullptr : new ConfigToken[count];
    for (unsigned int index = 0; index < count; ++index)
    {
        // Reset is initialization, not ownership cleanup. It only sees empty
        // records here; payload ownership is established afterwards.
        table[index] = {nullptr, 0xffffffffu, nullptr, nullptr};
        table[index].Reset();
        const auto presence = vary ? (mask + index * 3u) & 7u : mask;
        auto& ids = field_allocations[index];
        ids.fill(absent);
        if (presence & 1u)
        {
            ids[0] = allocations::count;
            const auto size = 1u + index % 5u;
            table[index].name = new char[size];
            std::fill_n(table[index].name, size, static_cast<char>('a' + index % 26u));
        }
        if (presence & 2u)
        {
            ids[1] = allocations::count;
            const auto size = 1u + index % 7u;
            table[index].text = new char[size];
            std::fill_n(table[index].text, size, static_cast<char>('A' + index % 26u));
        }
        if (presence & 4u)
        {
            ids[2] = allocations::count;
            table[index].number = new float(static_cast<float>(index) + 0.25f);
        }
        table[index].kind = index % 4u == 3u ? 0xffffffffu : index % 4u;
    }
    std::array<std::size_t, allocations::capacity> expected{};
    std::size_t expected_count = 0;
    for (unsigned int index = count; index > 0; --index)
    {
        // Allocation IDs avoid any assumption about a native array cookie or
        // the original x86 pointer width. The field order is the observed one.
        for (const auto id : field_allocations[index - 1])
        {
            if (id != absent)
            {
                expected[expected_count++] = id;
            }
        }
    }
    if (!null_table)
    {
        expected[expected_count++] = 0;
    }
    owner->tokens = table;
    owner->count = count;
    const auto before = storage.bytes;
    owner->~ConfigTokenBuffer();
    allocations::finish();

    expect(allocations::count == expected_count && allocations::releases == expected_count,
        "Token cleanup must release every owned allocation exactly once.");
    for (std::size_t index = 0; index < expected_count; ++index)
    {
        expect(allocations::release_order[index] == expected[index],
            "Token cleanup changed reverse-token/name/text/number/table release order.");
    }
    expect(
        storage.bytes == before, "Token cleanup changed its owner fields or surrounding guards.");
    ++cleanup_cases;
}

void reset(
    unsigned int count, unsigned int target, unsigned int mask, unsigned int kind, bool alias)
{
    if (!count || count > maximum_tokens || target >= count)
    {
        throw std::logic_error("Invalid generated token reset domain.");
    }
    std::array<char, 16> names{};
    std::array<char, 16> texts{};
    std::array<float, 16> numbers{};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        names[index] = static_cast<char>(index * 17 + 5);
        texts[index] = static_cast<char>(index * 13 + 11);
        numbers[index] = static_cast<float>(index * 3) + 0.25f;
    }
    const auto original_names = names;
    const auto original_texts = texts;
    const auto original_numbers = numbers;
    std::array<ConfigToken, maximum_tokens> before{};
    allocations::begin();
    auto* tokens = new ConfigToken[count];
    for (unsigned int index = 0; index < count; ++index)
    {
        const auto presence = index == target ? mask : mask ^ 7u;
        tokens[index].name = presence & 1u ? names.data() + index % names.size() : nullptr;
        tokens[index].kind = index == target ? kind : kind ^ 0x59ba27c1u;
        tokens[index].text =
            presence & 2u ? (alias ? names.data() : texts.data()) + index % texts.size() : nullptr;
        tokens[index].number = presence & 4u ? numbers.data() + index % numbers.size() : nullptr;
        before[index] = tokens[index];
    }
    tokens[target].Reset();
    const auto cleared = tokens[target];
    tokens[target].Reset();
    const bool idempotent = same_fields(cleared, tokens[target]);
    bool neighbors_unchanged = true;
    for (unsigned int index = 0; index < count; ++index)
    {
        if (index != target)
        {
            neighbors_unchanged = neighbors_unchanged && same_fields(tokens[index], before[index]);
        }
    }
    const bool reset_did_not_allocate = allocations::count == 1;
    const bool reset_did_not_release = allocations::releases == 0;
    delete[] tokens;
    allocations::finish();

    expect(cleared.name == nullptr, "Token reset must clear name.");
    expect(cleared.kind == 0, "Token reset must clear kind.");
    expect(cleared.text == nullptr, "Token reset must clear text.");
    expect(cleared.number == nullptr, "Token reset must clear number.");
    expect(idempotent, "Token reset must be idempotent.");
    expect(neighbors_unchanged, "Token reset changed a neighboring record.");
    expect(names == original_names, "Token reset modified borrowed name storage.");
    expect(texts == original_texts, "Token reset modified borrowed text storage.");
    expect(numbers == original_numbers, "Token reset modified borrowed numeric storage.");
    expect(reset_did_not_allocate && reset_did_not_release,
        "Token reset must not allocate or release storage.");
    ++reset_cases;
}
} // namespace

int main()
{
    try
    {
        OwnerStorage owner;
        cleanup(owner, 0, 0, false, true);
        cleanup(owner, 0, 0, false, false);
        for (const unsigned int count : {1u, 2u, 3u, 7u, 16u, 33u})
        {
            for (unsigned int mask = 0; mask < 8; ++mask)
            {
                cleanup(owner, count, mask, false, false);
                cleanup(owner, count, mask, true, false);
                for (unsigned int target = 0; target < count; ++target)
                {
                    for (const unsigned int kind : {0u,
                             1u,
                             2u,
                             3u,
                             0xffu,
                             0x100u,
                             0x7fffffffu,
                             0x80000000u,
                             std::numeric_limits<unsigned int>::max()})
                    {
                        reset(count, target, mask, kind, false);
                        reset(count, target, mask, kind, true);
                    }
                }
            }
        }
        // Start a fresh owner lifetime in the same guarded storage each time.
        // Calling a destructor twice without reconstruction is not a valid case.
        for (unsigned int iteration = 0; iteration < 64; ++iteration)
        {
            cleanup(owner, iteration % 8u, iteration % 8u, true, false);
        }
        std::cout << checks << " reconstructed token checks passed across " << reset_cases
                  << " reset and " << cleanup_cases << " cleanup lifetimes\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

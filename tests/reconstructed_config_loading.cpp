#include "data/folder_source.h"
#include "data/source.h"
#include "runtime/retail_files.h"
#include "startup_config_decimal.h"
#include "startup_config_tokens.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace loading_fixture
{
using Bytes = std::vector<unsigned char>;

enum class Operation
{
    open,
    close,
    seek,
    position,
    read
};
struct Event
{
    Operation operation;
    unsigned int stage;
    std::uint64_t amount = 0;
    std::uint64_t result = 0;
};
struct Scenario
{
    std::string path = "Data/generated-config.pis";
    Bytes payload;
    Bytes growth;
    std::uint64_t reported_size = 0;
    unsigned int failed_open = 0;
    bool throwing_open = false;
    bool failed_seek = false;
    bool failed_position = false;
    std::size_t read_chunk = std::numeric_limits<std::size_t>::max();
    std::size_t read_stop = std::numeric_limits<std::size_t>::max();
    bool throwing_read = false;
};
struct State
{
    Scenario scenario;
    std::vector<Event> events;
    unsigned int opens = 0;
    unsigned int closes = 0;
    const ConfigTokenBuffer* constructing_owner = nullptr;
    bool first_open_seen = false;
    bool initialized_before_open = false;
};

class ScriptedFile final : public beachlife::data::SourceFile
{
  public:
    ScriptedFile(std::shared_ptr<State> state, unsigned int stage)
        : state_(std::move(state))
        , stage_(stage)
    {
        data_ = state_->scenario.payload;
        if (stage_ == 3)
        {
            data_.insert(
                data_.end(), state_->scenario.growth.begin(), state_->scenario.growth.end());
        }
    }
    ~ScriptedFile() override
    {
        ++state_->closes;
        state_->events.push_back({Operation::close, stage_});
    }
    std::uint64_t size() const override
    {
        return stage_ == 2 ? state_->scenario.reported_size : data_.size();
    }
    std::uint64_t tell() const override
    {
        if (seek_attempted_)
        {
            state_->events.push_back({Operation::position, stage_, cursor_});
            if (state_->scenario.failed_position)
            {
                throw std::runtime_error("Generated no-write position failure.");
            }
        }
        return cursor_;
    }
    void seek(std::uint64_t position) override
    {
        seek_attempted_ = true;
        state_->events.push_back({Operation::seek, stage_, position});
        if (state_->scenario.failed_seek)
        {
            throw std::runtime_error("Generated seek failure.");
        }
        if (position > size())
        {
            throw std::out_of_range("Generated fixture seek.");
        }
        cursor_ = position;
    }
    std::size_t read(std::span<unsigned char> destination) override
    {
        const auto remaining = cursor_ < data_.size() ? data_.size() - cursor_ : 0;
        const auto stop = state_->scenario.read_stop;
        const auto allowed = cursor_ < stop ? stop - cursor_ : 0;
        const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(
            {remaining, allowed, destination.size(), state_->scenario.read_chunk}));
        state_->events.push_back({Operation::read, stage_, destination.size(), amount});
        if (!amount && state_->scenario.throwing_read)
        {
            throw std::runtime_error("Generated partial read failure.");
        }
        if (amount)
        {
            std::copy_n(
                data_.begin() + static_cast<std::ptrdiff_t>(cursor_), amount, destination.begin());
            cursor_ += amount;
        }
        return amount;
    }

  private:
    std::shared_ptr<State> state_;
    unsigned int stage_;
    Bytes data_;
    std::uint64_t cursor_ = 0;
    bool seek_attempted_ = false;
};

class ScriptedSource final : public beachlife::data::FileSource
{
  public:
    explicit ScriptedSource(std::shared_ptr<State> state)
        : state_(std::move(state))
    {
    }
    std::unique_ptr<beachlife::data::SourceFile> open(std::string_view path) const override
    {
        const auto stage = ++state_->opens;
        state_->events.push_back({Operation::open, stage});
        if (stage == 1 && state_->constructing_owner)
        {
            state_->first_open_seen = true;
            state_->initialized_before_open =
                !state_->constructing_owner->tokens && state_->constructing_owner->count == 0;
        }
        if (path != state_->scenario.path || stage == state_->scenario.failed_open)
        {
            if (state_->scenario.throwing_open)
            {
                throw std::runtime_error("Generated open failure.");
            }
            return nullptr;
        }
        return std::make_unique<ScriptedFile>(state_, stage);
    }

  private:
    std::shared_ptr<State> state_;
};

inline std::shared_ptr<State> state(Scenario scenario)
{
    auto value = std::make_shared<State>();
    value->scenario = std::move(scenario);
    // Stream destruction records into existing capacity; no throwing allocation
    // is needed by the noexcept destructor in this bounded fixture.
    value->events.reserve(value->scenario.payload.size() + value->scenario.growth.size() + 32);
    return value;
}
} // namespace loading_fixture

namespace allocation_probe
{
struct Result
{
    std::size_t calls = 0;
    std::array<std::size_t, 2> bytes{};
    std::array<void*, 2> memory{};
    bool unexpected = false;
};
Result observed;
bool active = false;
bool fail_table = false;

void* allocate(std::size_t bytes) noexcept
{
    std::size_t call = 0;
    if (active)
    {
        call = observed.calls++;
        if (call >= observed.bytes.size())
        {
            observed.unexpected = true;
            return nullptr;
        }
        observed.bytes[call] = bytes;
        if (call == 1 && fail_table)
        {
            return nullptr;
        }
    }
    try
    {
        // Delegate to the matching ordinary array allocator without adding any
        // initialized bytes or padding. ASan sees the exact temporary extent.
        void* memory = ::operator new[](bytes);
        if (active)
        {
            observed.memory[call] = memory;
        }
        return memory;
    }
    catch (...)
    {
        return nullptr;
    }
}
} // namespace allocation_probe

// Labelled failure injection for the standard nonthrowing array overload only.
// The temporary always succeeds; the second request is the checked token table.
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    return allocation_probe::allocate(bytes);
}

namespace
{
namespace data = beachlife::data;
namespace files = beachlife::runtime::retail_files;
using loading_fixture::Bytes;
using loading_fixture::Operation;
using loading_fixture::Scenario;
using loading_fixture::ScriptedSource;
using loading_fixture::State;

static_assert(sizeof(unsigned int) == 4);
static_assert(std::is_trivially_destructible_v<ConfigToken>);
std::size_t checks = 0;
std::size_t cases = 0;
std::size_t records = 0;
std::size_t first_failures = 0;
std::size_t second_failures = 0;
std::size_t third_failures = 0;
std::size_t seek_failures = 0;
std::size_t read_failures = 0;
std::size_t table_failures = 0;
std::size_t previous_tables = 0;
std::size_t high_positions = 0;
std::size_t grown_files = 0;
std::size_t folder_cases = 0;
std::size_t construction_cases = 0;
std::size_t construction_folder_cases = 0;
std::size_t initialization_observations = 0;
std::size_t exact_numeric_cases = 0;

void expect(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
struct Record
{
    Bytes name;
    unsigned int kind;
    Bytes text;
    std::uint32_t number;
};
struct Document
{
    Bytes bytes;
    std::vector<Record> records;
};

Document document(unsigned int count, unsigned int variant = 0)
{
    // The oracle constructs records before their byte representation. Names
    // contain no apostrophe, strings contain no double quote, and separators are
    // explicitly outside the accepted 33..121 byte set. This complete grammar
    // reaches every sought name before the last byte and never needs lookahead
    // beyond the loader's in-range final NUL.
    const std::array<Bytes, 4> separators{{{'\r'}, {'\t', '\n'}, {0, 0x80, 0xff, 'z'}, {' '}}};
    const auto& separator = separators[variant % separators.size()];
    Document result;
    for (unsigned int i = 0; i != count; ++i)
    {
        const std::string name = "item" + std::to_string(i);
        Record record{Bytes(name.begin(), name.end()), (i + variant) % 3, {}, 0};
        if (variant & 4u)
        {
            record.name.insert(record.name.end(), {0, 0xff, 'z'});
        }
        result.bytes.push_back('\'');
        result.bytes.insert(result.bytes.end(), record.name.begin(), record.name.end());
        result.bytes.push_back('\'');
        result.bytes.insert(result.bytes.end(), separator.begin(), separator.end());
        if (record.kind == 1)
        {
            record.text = {'v', 0, 0xff, '\'', '\\'};
            result.bytes.push_back('"');
            result.bytes.insert(result.bytes.end(), record.text.begin(), record.text.end());
            result.bytes.push_back('"');
        }
        else
        {
            const auto value = static_cast<int>(i) - 7;
            const bool negative_zero = record.kind == 2 && (variant & 8u) && i == 0;
            const std::string spelling =
                record.kind == 2 ? (negative_zero ? "-0.0" : std::to_string(value) + ".0")
                                 : "unclassified";
            result.bytes.insert(result.bytes.end(), spelling.begin(), spelling.end());
            if (record.kind == 2)
            {
                record.number =
                    std::bit_cast<std::uint32_t>(negative_zero ? -0.0f : static_cast<float>(value));
            }
        }
        result.bytes.insert(result.bytes.end(), separator.begin(), separator.end());
        result.records.push_back(std::move(record));
    }
    return result;
}

struct OwnerFixture
{
    std::uint64_t before = 0x2974a5ed632089bfULL;
    // Construct before mounting the scripted VFS or observing allocations.
    // These cases still exercise LoadAndParse separately from construction.
    ConfigTokenBuffer owner{"Data/generated-unbound.pis"};
    std::uint64_t after = 0x6f3982b47a1c50deULL;
    ConfigToken* old = nullptr;

    explicit OwnerFixture(bool previous)
    {
        if (previous)
        {
            old = new ConfigToken[1];
            old->Reset();
            old->name = new char[4]{'o', 'l', 'd', 0};
            old->text = new char[4]{'p', 'r', 'e', 0};
            old->number = new float(17.0f);
            old->kind = 0x12345678u;
            owner.tokens = old;
            owner.count = 1;
        }
    }
    ~OwnerFixture()
    {
        // A failed replacement can leave null tokens with a nonzero old count.
        // Observe that state before teardown, then make only this test-owned
        // object safe to destroy; the faithful cleanup has no null-table guard.
        if (!owner.tokens)
        {
            owner.count = 0;
        }
        // The loader overwrites a previous table without freeing it. Recover
        // that separately owned test allocation; actual cleanup handles current
        // fields. A first-open failure keeps the old table owned by owner.
        if (old && old != owner.tokens)
        {
            delete[] old->name;
            delete[] old->text;
            delete old->number;
            delete[] old;
        }
    }
    void verify_previous() const
    {
        expect(before == 0x2974a5ed632089bfULL && after == 0x6f3982b47a1c50deULL,
            "Loading changed bytes adjacent to its owner.");
        if (old)
        {
            expect(std::memcmp(old->name, "old", 4) == 0 && std::memcmp(old->text, "pre", 4) == 0 &&
                       *old->number == 17.0f && old->kind == 0x12345678u,
                "Loading released or modified its previous table payloads.");
        }
    }
};

void payload(const char* actual, const Bytes& expected)
{
    expect(actual != nullptr, "Expected copied payload is missing.");
    expect(expected.empty() || std::memcmp(actual, expected.data(), expected.size()) == 0,
        "Copied payload differs from the independently constructed record.");
    expect(actual[expected.size()] == 0, "Copied payload terminator differs.");
}

void verify_records(const ConfigTokenBuffer& owner, const std::vector<Record>& expected)
{
    expect(owner.count == expected.size(), "Loaded token count differs from document records.");
    expect(owner.tokens != nullptr, "Successful table allocation unexpectedly failed.");
    for (std::size_t i = 0; i != expected.size(); ++i)
    {
        const auto& wanted = expected[i];
        const auto& token = owner.tokens[i];
        payload(token.name, wanted.name);
        expect(token.kind == wanted.kind, "Loaded value kind differs from document record.");
        if (wanted.kind == 1)
        {
            payload(token.text, wanted.text);
        }
        else
        {
            expect(token.text == nullptr, "Unselected text pointer was initialized incorrectly.");
        }
        if (wanted.kind == 2)
        {
            expect(token.number && std::bit_cast<std::uint32_t>(*token.number) == wanted.number,
                "Loaded numeric value differs from the qualified bit oracle.");
        }
        else
        {
            expect(
                token.number == nullptr, "Unselected numeric pointer was initialized incorrectly.");
        }
    }
    records += expected.size();
}

void load(OwnerFixture& fixture, const char* path, bool fail_table)
{
    allocation_probe::observed = {};
    allocation_probe::fail_table = fail_table;
    allocation_probe::active = true;
    try
    {
        fixture.owner.LoadAndParse(path);
    }
    catch (...)
    {
        allocation_probe::active = false;
        throw;
    }
    allocation_probe::active = false;
}

void verify_io(const State& observed, const Scenario& scenario, std::size_t temporary)
{
    expect(observed.opens == (scenario.failed_open == 1 ? 1u : 3u),
        "Probe/size/read open count changed.");
    expect(observed.closes == (scenario.failed_open == 1 ? 0u
                                  : scenario.failed_open ? 2u
                                                         : 3u),
        "A source handle escaped the loader call.");
    std::uint64_t read_bytes = 0;
    std::vector<std::pair<Operation, unsigned int>> protocol;
    for (const auto& event : observed.events)
    {
        if (event.operation == Operation::read)
        {
            expect(event.stage == 3 && event.amount <= temporary && event.amount <= 256 * 1024,
                "Loader read from the wrong handle or exceeded its requested extent.");
            read_bytes += event.result;
        }
        else
        {
            protocol.emplace_back(event.operation, event.stage);
        }
    }
    std::vector<std::pair<Operation, unsigned int>> expected_protocol{{Operation::open, 1}};
    if (scenario.failed_open != 1)
    {
        expected_protocol.emplace_back(Operation::close, 1);
        expected_protocol.emplace_back(Operation::open, 2);
        if (scenario.failed_open != 2)
        {
            expected_protocol.emplace_back(Operation::seek, 2);
            expected_protocol.emplace_back(Operation::position, 2);
            expected_protocol.emplace_back(Operation::close, 2);
        }
        expected_protocol.emplace_back(Operation::open, 3);
        if (scenario.failed_open != 3)
        {
            expected_protocol.emplace_back(Operation::close, 3);
        }
    }
    expect(protocol == expected_protocol, "Probe/size/read handle ordering changed.");
    const auto available = scenario.payload.size() + scenario.growth.size();
    const auto expected_read =
        (scenario.failed_open == 1 || scenario.failed_open == 3)
            ? 0u
            : std::min<std::uint64_t>({temporary, available, scenario.read_stop});
    expect(read_bytes == expected_read, "Reader did not consume the full allocated-byte request.");
}

void check(const Document& input, Scenario scenario, bool previous = false, bool fail_table = false)
{
    const bool empty_size =
        scenario.failed_open == 2 || scenario.failed_seek || input.bytes.empty();
    if (scenario.failed_position || (scenario.failed_open == 3 && !empty_size) ||
        ((scenario.read_stop != std::numeric_limits<std::size_t>::max() ||
             scenario.throwing_read) &&
            !empty_size))
    {
        throw std::logic_error("Generated loader case would consume an indeterminate buffer tail.");
    }
    if (static_cast<std::uint32_t>(scenario.reported_size) != input.bytes.size())
    {
        throw std::logic_error("Virtual size low word must equal the complete generated document.");
    }
    scenario.payload = input.bytes;
    auto observed = loading_fixture::state(scenario);
    OwnerFixture fixture(previous);
    std::weak_ptr<ScriptedSource> weak_source;
    {
        auto source = std::make_shared<ScriptedSource>(observed);
        weak_source = source;
        files::Session session(source);
        load(fixture, scenario.path.c_str(), fail_table);
    }
    expect(weak_source.expired(), "Loading retained a mounted source after its session ended.");
    fixture.verify_previous();
    const auto& allocations = allocation_probe::observed;
    expect(!allocations.unexpected, "Loading performed an unexpected nonthrowing allocation.");
    const std::size_t count = empty_size ? 0 : input.records.size();
    const std::size_t temporary = empty_size ? 1 : input.bytes.size() + 1;
    if (scenario.failed_open == 1)
    {
        expect(fixture.owner.tokens == fixture.old && fixture.owner.count == (previous ? 1u : 0u),
            "First-open failure changed the caller's owner state.");
        expect(allocations.calls == 0, "First-open failure allocated memory.");
        ++first_failures;
    }
    else
    {
        expect(allocations.calls == 2 && allocations.bytes[0] == temporary &&
                   allocations.bytes[1] == count * sizeof(ConfigToken),
            "Temporary/table allocation sizes differ from the independent document model.");
        expect(allocations.memory[0] != nullptr, "Temporary allocation was not successful.");
        if (fail_table)
        {
            expect(!fixture.owner.tokens && !allocations.memory[1] &&
                       fixture.owner.count == (previous ? 1u : 0u),
                "Table failure did not preserve null publication and the previous count.");
            ++table_failures;
        }
        else
        {
            expect(fixture.owner.tokens == allocations.memory[1] &&
                       fixture.owner.tokens != fixture.old,
                "Loading did not publish its new table allocation.");
            const std::vector<Record> empty;
            verify_records(fixture.owner, empty_size ? empty : input.records);
        }
    }
    verify_io(*observed, scenario, temporary);
    second_failures += scenario.failed_open == 2;
    third_failures += scenario.failed_open == 3;
    seek_failures += scenario.failed_seek;
    read_failures += scenario.read_stop == 0;
    previous_tables += previous;
    high_positions += scenario.reported_size > std::numeric_limits<std::uint32_t>::max();
    grown_files += !scenario.growth.empty();
    ++cases;
}

// Only the owner storage is prefilled. The constructor's temporary remains an
// exact-size allocation, so sanitizers can observe its real read boundary.
struct ConstructionStorage
{
    static constexpr std::size_t guard = alignof(ConfigTokenBuffer);
    alignas(
        ConfigTokenBuffer) std::array<unsigned char, sizeof(ConfigTokenBuffer) + 2 * guard> bytes;
    ConfigTokenBuffer* owner = nullptr;

    ConstructionStorage()
    {
        bytes.fill(0xa5);
    }
    ~ConstructionStorage()
    {
        if (owner)
        {
            owner->~ConfigTokenBuffer();
        }
    }
    ConfigTokenBuffer* address()
    {
        return reinterpret_cast<ConfigTokenBuffer*>(bytes.data() + guard);
    }
    void construct(const char* path, bool fail_table = false)
    {
        allocation_probe::observed = {};
        allocation_probe::fail_table = fail_table;
        allocation_probe::active = true;
        try
        {
            owner = new (bytes.data() + guard) ConfigTokenBuffer(path);
        }
        catch (...)
        {
            allocation_probe::active = false;
            throw;
        }
        allocation_probe::active = false;
    }
    void verify_guards() const
    {
        expect(std::all_of(bytes.begin(),
                   bytes.begin() + guard,
                   [](unsigned char value)
                   {
                       return value == 0xa5;
                   }) &&
                   std::all_of(bytes.end() - guard,
                       bytes.end(),
                       [](unsigned char value)
                       {
                           return value == 0xa5;
                       }),
            "Construction changed bytes adjacent to its owner.");
    }
    void destroy()
    {
        const auto before = bytes;
        owner->~ConfigTokenBuffer();
        owner = nullptr;
        expect(bytes == before, "Actual cleanup changed its constructed owner storage.");
    }
};

void construct_check(const Document& input, Scenario scenario, bool fail_table = false)
{
    const bool empty_size =
        scenario.failed_open == 2 || scenario.failed_seek || input.bytes.empty();
    if (scenario.failed_position || (scenario.failed_open == 3 && !empty_size) ||
        ((scenario.read_stop != std::numeric_limits<std::size_t>::max() ||
             scenario.throwing_read) &&
            !empty_size) ||
        static_cast<std::uint32_t>(scenario.reported_size) != input.bytes.size())
    {
        throw std::logic_error("Constructor case is outside the fully initialized loader domain.");
    }
    scenario.payload = input.bytes;
    auto observed = loading_fixture::state(scenario);
    ConstructionStorage storage;
    observed->constructing_owner = storage.address();
    std::weak_ptr<ScriptedSource> weak_source;
    {
        auto source = std::make_shared<ScriptedSource>(observed);
        weak_source = source;
        files::Session session(source);
        storage.construct(scenario.path.c_str(), fail_table);
    }
    expect(observed->first_open_seen && observed->initialized_before_open,
        "Constructor did not initialize null tokens and count0 before its first open.");
    ++initialization_observations;
    expect(weak_source.expired(), "Constructed owner retained its source after the session ended.");
    storage.verify_guards();
    const auto& allocations = allocation_probe::observed;
    expect(!allocations.unexpected, "Construction performed an unexpected nonthrowing allocation.");
    const std::size_t temporary = empty_size ? 1 : input.bytes.size() + 1;
    const std::size_t count = empty_size ? 0 : input.records.size();
    if (scenario.failed_open == 1)
    {
        expect(!storage.owner->tokens && storage.owner->count == 0,
            "First-open failure did not retain constructor initialization.");
        expect(allocations.calls == 0, "Failed construction probe allocated a temporary or table.");
    }
    else
    {
        expect(allocations.calls == 2 && allocations.bytes[0] == temporary &&
                   allocations.bytes[1] == count * sizeof(ConfigToken),
            "Temporary/table allocation sizes differ from the independent document model.");
        expect(allocations.memory[0] != nullptr,
            "Constructor temporary allocation was not successful.");
        if (fail_table)
        {
            expect(!storage.owner->tokens && storage.owner->count == 0 && !allocations.memory[1],
                "Constructor table failure did not preserve its initialized empty state.");
        }
        else
        {
            expect(storage.owner->tokens == allocations.memory[1],
                "Constructor did not publish its actual table allocation.");
            const std::vector<Record> empty;
            verify_records(*storage.owner, empty_size ? empty : input.records);
        }
    }
    verify_io(*observed, scenario, temporary);
    storage.destroy();
    ++construction_cases;
    ++cases;
}

void constructed_cases()
{
    for (const unsigned int count : {0u, 1u, 2u, 3u, 7u, 16u})
    {
        for (unsigned int variant = 0; variant != 16; ++variant)
        {
            const auto input = document(count, variant);
            for (const std::uint64_t high : {0ULL, 0x100000000ULL, 0x200000000ULL})
            {
                for (const std::size_t chunk : {1u, 7u, 1024u})
                {
                    Scenario scenario;
                    scenario.reported_size = high + input.bytes.size();
                    scenario.read_chunk = chunk;
                    construct_check(input, scenario);
                    scenario.growth = {'x', 'y'};
                    construct_check(input, scenario);
                    construct_check(input, scenario, true);
                }
            }
            for (const unsigned int stage : {1u, 2u})
            {
                Scenario scenario;
                scenario.reported_size = input.bytes.size();
                scenario.failed_open = stage;
                construct_check(input, scenario);
                scenario.throwing_open = true;
                construct_check(input, scenario);
            }
            Scenario failed_seek;
            failed_seek.reported_size = input.bytes.size();
            failed_seek.failed_seek = true;
            construct_check(input, failed_seek);
            for (const bool throws : {false, true})
            {
                failed_seek.read_stop = 0;
                failed_seek.throwing_read = throws;
                construct_check(input, failed_seek);
            }
        }
    }
    Scenario empty_read_failure;
    empty_read_failure.failed_open = 3;
    construct_check(document(0), empty_read_failure);
    empty_read_failure.throwing_open = true;
    construct_check(document(0), empty_read_failure, true);

    // Comments contain no name delimiters and end in CR before any record.
    // The independently constructed records are unchanged by these prefix bytes.
    auto commented = document(7);
    const std::string comment = "// generated comment\r\n\t";
    commented.bytes.insert(commented.bytes.begin(), comment.begin(), comment.end());
    Scenario scenario;
    scenario.reported_size = commented.bytes.size();
    construct_check(commented, scenario);
}

Document numeric_document(const std::string& spelling, std::uint32_t bits)
{
    Document result;
    const std::string name = "generated-number";
    const std::string encoded = "'" + name + "' " + spelling + "\r\n";
    result.bytes.assign(encoded.begin(), encoded.end());
    result.records.push_back({Bytes(name.begin(), name.end()), 2, {}, bits});
    return result;
}

void constructed_numbers()
{
    // The selected noninteger2.5 has exact expected float32 bits. This does not
    // extend qualification to arbitrary decimals or altered floating-point modes.
    for (const char* literal : {"2.5", "2.5\r"})
    {
        expect(std::bit_cast<std::uint32_t>(ConvertConfigDecimalPrefix(literal)) == 0x40200000u,
            "The selected2.5 literal differs from its exact float32 bit oracle.");
    }
    for (int integer = -2048; integer <= 2048; ++integer)
    {
        auto spelling = std::to_string(integer);
        if (integer % 2 == 0)
        {
            spelling += ".0";
        }
        const auto input =
            numeric_document(spelling, std::bit_cast<std::uint32_t>(static_cast<float>(integer)));
        Scenario scenario;
        scenario.reported_size = input.bytes.size();
        construct_check(input, scenario);
        ++exact_numeric_cases;
    }
    const auto fraction = numeric_document("2.5", 0x40200000u);
    for (const std::size_t chunk : {1u, 7u, 1024u})
    {
        Scenario scenario;
        scenario.reported_size = fraction.bytes.size();
        scenario.read_chunk = chunk;
        construct_check(fraction, scenario);
        ++exact_numeric_cases;
    }
}

void construction_folder_inputs(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root / "Data");
    for (const unsigned int count : {0u, 1u, 2u, 7u, 16u})
    {
        const auto input = document(count);
        const auto filename = "generated-constructor-" + std::to_string(count) + ".pis";
        const auto path = root / "Data" / filename;
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(input.bytes.data()),
                static_cast<std::streamsize>(input.bytes.size()));
            if (!output)
            {
                throw std::runtime_error("Could not write generated constructor fixture.");
            }
        }
        ConstructionStorage storage;
        {
            auto source = std::make_shared<data::FolderSource>(
                root, std::vector<data::SourceEntry>{{"Data/" + filename, input.bytes.size()}});
            files::Session session(source);
            const auto logical = ".\\dAtA\\GENERATED-CONSTRUCTOR-" + std::to_string(count) + ".PIS";
            storage.construct(logical.c_str());
        }
        storage.verify_guards();
        verify_records(*storage.owner, input.records);
        storage.destroy();
        std::filesystem::remove(path);
        ++construction_folder_cases;
        ++construction_cases;
        ++cases;
    }
}

void mounted_cases()
{
    for (const unsigned int count : {0u, 1u, 2u, 3u, 7u, 16u})
    {
        for (unsigned int variant = 0; variant != 16; ++variant)
        {
            const auto input = document(count, variant);
            for (const std::uint64_t high : {0ULL, 0x100000000ULL, 0x200000000ULL})
            {
                for (const std::size_t chunk : {1u, 7u, 1024u})
                {
                    Scenario scenario;
                    scenario.reported_size = high + input.bytes.size();
                    scenario.read_chunk = chunk;
                    const bool previous = (variant & 1u) != 0;
                    check(input, scenario, previous);
                    scenario.growth = {'x', 'y'};
                    check(input, scenario, previous);
                    check(input, scenario, previous, true);
                }
            }
            for (const unsigned int stage : {1u, 2u})
            {
                Scenario scenario;
                scenario.reported_size = input.bytes.size();
                scenario.failed_open = stage;
                check(input, scenario, false);
                check(input, scenario, true);
                scenario.throwing_open = true;
                check(input, scenario, true);
            }
            Scenario failed_seek;
            failed_seek.reported_size = input.bytes.size();
            failed_seek.failed_seek = true;
            check(input, failed_seek, true);
            for (bool throws : {false, true})
            {
                failed_seek.read_stop = 0;
                failed_seek.throwing_read = throws;
                check(input, failed_seek, true);
            }
        }
    }
    Scenario empty_read_failure;
    empty_read_failure.failed_open = 3;
    check(document(0), empty_read_failure, true);
    empty_read_failure.throwing_open = true;
    check(document(0), empty_read_failure, true, true);

    // Cross the adapter's chunk boundary using one complete long-name record.
    Document large;
    Record record{Bytes(256 * 1024 + 17, 'n'), 2, {}, std::bit_cast<std::uint32_t>(1.0f)};
    large.bytes.push_back('\'');
    large.bytes.insert(large.bytes.end(), record.name.begin(), record.name.end());
    large.bytes.insert(large.bytes.end(), {'\'', ' ', '1', '\r'});
    large.records.push_back(std::move(record));
    Scenario scenario;
    scenario.reported_size = large.bytes.size();
    scenario.read_chunk = 32768;
    check(large, scenario);
}

void unbound_cases()
{
    for (const bool previous : {false, true})
    {
        OwnerFixture fixture(previous);
        load(fixture, "Data/generated-unbound.pis", false);
        expect(fixture.owner.tokens == fixture.old && fixture.owner.count == (previous ? 1u : 0u),
            "Unbound loading changed its caller's owner state.");
        expect(allocation_probe::observed.calls == 0, "Unbound loading allocated memory.");
        fixture.verify_previous();
        ++cases;
    }
}

void folder_inputs(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root / "Data");
    const std::array<unsigned int, 5> counts{0, 1, 2, 7, 16};
    for (std::size_t i = 0; i != counts.size(); ++i)
    {
        const auto input = document(counts[i], static_cast<unsigned int>(i));
        const auto filename = "generated-config-" + std::to_string(i) + ".pis";
        const auto path = root / "Data" / filename;
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(input.bytes.data()),
                static_cast<std::streamsize>(input.bytes.size()));
            if (!output)
            {
                throw std::runtime_error("Could not write generated configuration fixture.");
            }
        }
        OwnerFixture fixture(true);
        {
            auto source = std::make_shared<data::FolderSource>(
                root, std::vector<data::SourceEntry>{{"Data/" + filename, input.bytes.size()}});
            files::Session session(source);
            const auto logical = ".\\dAtA\\GENERATED-CONFIG-" + std::to_string(i) + ".PIS";
            load(fixture, logical.c_str(), false);
        }
        fixture.verify_previous();
        verify_records(fixture.owner, input.records);
        expect(
            allocation_probe::observed.calls == 2 &&
                allocation_probe::observed.bytes[0] == input.bytes.size() + 1 &&
                allocation_probe::observed.bytes[1] == input.records.size() * sizeof(ConfigToken),
            "Folder-backed loading changed its allocation extents.");
        std::filesystem::remove(path);
        ++folder_cases;
        ++cases;
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("Pass a generated fixture directory.");
        }
        constructed_cases();
        constructed_numbers();
        construction_folder_inputs(std::filesystem::path(argv[1]));
        const auto construction_checks = checks;
        const auto construction_records = records;
        unbound_cases();
        mounted_cases();
        folder_inputs(std::filesystem::path(argv[1]));
        expect(first_failures && second_failures && third_failures && seek_failures &&
                   read_failures && table_failures && previous_tables && high_positions &&
                   grown_files && folder_cases,
            "Required loading scenarios were not exercised.");
        const auto retained_loading_checks = checks - construction_checks;
        const auto retained_loading_records = records - construction_records;
        expect(initialization_observations && construction_folder_cases && exact_numeric_cases,
            "Required constructor scenarios were not exercised.");
        std::cout << "{\"cases\":" << cases << ",\"checks\":" << checks
                  << ",\"records\":" << records << ",\"first_open_failures\":" << first_failures
                  << ",\"second_open_failures\":" << second_failures
                  << ",\"third_open_failures\":" << third_failures
                  << ",\"seek_failures\":" << seek_failures
                  << ",\"read_failures\":" << read_failures
                  << ",\"table_failures\":" << table_failures
                  << ",\"previous_tables\":" << previous_tables
                  << ",\"high_positions\":" << high_positions << ",\"grown_files\":" << grown_files
                  << ",\"folder_cases\":" << folder_cases
                  << ",\"construction_cases\":" << construction_cases
                  << ",\"construction_checks\":" << construction_checks
                  << ",\"construction_records\":" << construction_records
                  << ",\"initialization_observations\":" << initialization_observations
                  << ",\"construction_folder_cases\":" << construction_folder_cases
                  << ",\"exact_numeric_cases\":" << exact_numeric_cases
                  << ",\"retained_loading_checks\":" << retained_loading_checks
                  << ",\"retained_loading_records\":" << retained_loading_records << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        allocation_probe::active = false;
        std::cerr << "Configuration loading fixture failure: " << error.what() << '\n';
        return 1;
    }
}

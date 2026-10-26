#include "data/folder_source.h"
#include "data/source.h"
#include "runtime/retail_files.h"
#include "startup_config_owner.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace data = beachlife::data;
namespace files = beachlife::runtime::retail_files;

struct Input
{
    std::string name;
    std::string text;
    int integer = 0;
    bool zero_fraction = false;
    bool negative_zero = false;
    bool selected_fraction = false;
};
struct Expected
{
    std::string name;
    bool flag = false;
    int integer = 0;
    std::uint32_t number = 0;
    bool has_text = false;
    std::string text;
};
struct Observation
{
    const StartupConfig* destination = nullptr;
    std::string filename;
    std::size_t opens = 0;
    std::size_t closes = 0;
    std::size_t seeks = 0;
    std::size_t positions = 0;
    std::size_t reads = 0;
    std::size_t read_bytes = 0;
    std::uint64_t sought = 0;
    bool initialized = true;
    bool protocol = true;
};

std::size_t checks = 0;
std::size_t cases = 0;
std::size_t records = 0;
std::size_t appended = 0;
std::size_t folder_cases = 0;
std::size_t numeric_cases = 0;
std::size_t selected_fractions = 0;
std::size_t embedded_nuls = 0;
std::size_t duplicate_names = 0;
std::size_t longest_name = 0;
std::size_t longest_text = 0;
std::array<std::size_t, 4> typed_records{};
bool completed = false;

void require_completion()
{
    // The faithful fatal routine exits with status zero. Keep CTest's normal
    // exit-code checks, and reject any early std::exit without changing it.
    if (!completed)
    {
        std::fputs("Startup configuration fixture did not complete.\n", stderr);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
}

void require(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class MemoryFile final : public data::SourceFile
{
  public:
    MemoryFile(std::string value, std::size_t chunk)
        : value_(std::move(value))
        , chunk_(chunk)
    {
    }
    std::uint64_t size() const override
    {
        return value_.size();
    }
    std::uint64_t tell() const override
    {
        return cursor_;
    }
    void seek(std::uint64_t position) override
    {
        if (position > size())
        {
            throw std::out_of_range("Generated startup seek.");
        }
        cursor_ = static_cast<std::size_t>(position);
    }
    std::size_t read(std::span<unsigned char> output) override
    {
        const auto amount = std::min({value_.size() - cursor_, output.size(), chunk_});
        std::memcpy(output.data(), value_.data() + cursor_, amount);
        cursor_ += amount;
        return amount;
    }

  private:
    std::string value_;
    std::size_t chunk_;
    std::size_t cursor_ = 0;
};

class MemorySource final : public data::FileSource
{
  public:
    MemorySource(std::string value, std::size_t chunk)
        : value_(std::move(value))
        , chunk_(chunk)
    {
    }
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override
    {
        return path == "Data/generated-owner.pis" ? std::make_unique<MemoryFile>(value_, chunk_)
                                                  : nullptr;
    }

  private:
    std::string value_;
    std::size_t chunk_;
};

class ObservedFile final : public data::SourceFile
{
  public:
    ObservedFile(std::unique_ptr<data::SourceFile> inner,
        std::shared_ptr<Observation> observed,
        std::size_t stage)
        : inner_(std::move(inner))
        , observed_(std::move(observed))
        , stage_(stage)
    {
    }
    ~ObservedFile() override
    {
        ++observed_->closes;
    }
    std::uint64_t size() const override
    {
        return inner_->size();
    }
    std::uint64_t tell() const override
    {
        if (sought_)
        {
            ++observed_->positions;
            observed_->protocol &= stage_ == 2;
        }
        return inner_->tell();
    }
    void seek(std::uint64_t position) override
    {
        ++observed_->seeks;
        observed_->sought = position;
        observed_->protocol &= stage_ == 2;
        sought_ = true;
        inner_->seek(position);
    }
    std::size_t read(std::span<unsigned char> output) override
    {
        ++observed_->reads;
        observed_->protocol &= stage_ == 3;
        const auto amount = inner_->read(output);
        observed_->read_bytes += amount;
        return amount;
    }

  private:
    std::unique_ptr<data::SourceFile> inner_;
    std::shared_ptr<Observation> observed_;
    std::size_t stage_;
    bool sought_ = false;
};

class ObservedSource final : public data::FileSource
{
  public:
    ObservedSource(
        std::shared_ptr<const data::FileSource> inner, std::shared_ptr<Observation> observed)
        : inner_(std::move(inner))
        , observed_(std::move(observed))
    {
    }
    std::unique_ptr<data::SourceFile> open(std::string_view path) const override
    {
        const auto stage = ++observed_->opens;
        observed_->initialized &= startup_config_owner == observed_->destination &&
                                  observed_->destination->head == nullptr;
        observed_->protocol &= stage <= 3 && path == observed_->filename;
        auto file = inner_->open(path);
        if (!file)
        {
            return nullptr;
        }
        return std::make_unique<ObservedFile>(std::move(file), observed_, stage);
    }

  private:
    std::shared_ptr<const data::FileSource> inner_;
    std::shared_ptr<Observation> observed_;
};

std::string c_prefix(const std::string& text)
{
    return text.substr(0, text.find('\0'));
}

Expected oracle(const Input& input)
{
    Expected result;
    result.name = c_prefix(input.name);
    // Expected typed values come from generated records, independently of the
    // reconstructed scanner, decimal converter and token parser.
    switch (input.name[0])
    {
    case 'b':
        result.flag = c_prefix(input.text) == "true";
        break;
    case 'i':
        result.integer = input.integer;
        break;
    case 'f':
        result.number = input.selected_fraction ? 0x40200000U
                        : input.negative_zero
                            ? 0x80000000U
                            : std::bit_cast<std::uint32_t>(static_cast<float>(input.integer));
        break;
    case 's':
        result.has_text = true;
        result.text = c_prefix(input.text);
        break;
    default:
        throw std::logic_error("Generated name has no supported type.");
    }
    return result;
}

std::string make_document(const std::vector<Input>& inputs, unsigned int separators)
{
    static const std::array<std::string, 4> choices = {
        "\r", "\r\n", " \t\r\n", std::string("\x80\xff\r", 3)};
    const auto& separator = choices[separators % choices.size()];
    std::string result = separator;
    for (const auto& input : inputs)
    {
        require(!input.name.empty() && input.name.size() <= 255 &&
                    input.name.find('\'') == std::string::npos,
            "Generated name is outside the bounded quoted domain.");
        result += '\'';
        result += input.name;
        result += "' ";
        if (input.name[0] == 'b' || input.name[0] == 's')
        {
            require(input.text.size() <= 255 && input.text.find('"') == std::string::npos,
                "Generated text is outside the bounded quoted domain.");
            result += '"';
            result += input.text;
            result += '"';
        }
        else
        {
            require(input.integer >= -1000000 && input.integer <= 1000000 &&
                        (!input.selected_fraction || input.name[0] == 'f'),
                "Generated numeric value is outside the qualified domain.");
            result += input.selected_fraction ? "2.5"
                      : input.negative_zero   ? "-0"
                                              : std::to_string(input.integer);
            if (input.zero_fraction && !input.selected_fraction)
            {
                result += ".0";
            }
        }
        // A complete quoted name/value and trailing CR keep every composed
        // scanner read inside the actual loader's allocation, including its
        // final NUL. No extra test padding or initialized tail is supplied.
        result += separator;
    }
    return result;
}

struct Storage
{
    std::array<unsigned char, 32> before;
    alignas(StartupConfig) unsigned char value[sizeof(StartupConfig)];
    std::array<unsigned char, 32> after;
    Storage()
    {
        before.fill(0xa7);
        after.fill(0x6d);
        std::memset(value, 0x4b, sizeof(value));
    }
    bool intact() const
    {
        return std::all_of(before.begin(),
                   before.end(),
                   [](auto byte)
                   {
                       return byte == 0xa7;
                   }) &&
               std::all_of(after.begin(),
                   after.end(),
                   [](auto byte)
                   {
                       return byte == 0x6d;
                   });
    }
};
struct Destroy
{
    void operator()(StartupConfig* owner) const
    {
        std::destroy_at(owner);
    }
};

void verify_nodes(const StartupConfig& owner, const std::vector<Expected>& expected)
{
    const auto* node = owner.head;
    std::vector<const StartupConfigNode*> seen;
    std::vector<const char*> strings;
    for (const auto& value : expected)
    {
        require(node != nullptr, "Owner omitted an ordered record.");
        require(std::find(seen.begin(), seen.end(), node) == seen.end(),
            "Owner list contains a cycle or a reused node.");
        require(std::memcmp(node->name, value.name.c_str(), value.name.size() + 1) == 0,
            "Owned name differs from its independent record.");
        require(
            node->flag == value.flag, "Boolean classification differs from exact lowercase true.");
        require(
            node->integer == value.integer, "Integer conversion differs from exact-domain oracle.");
        require(std::bit_cast<std::uint32_t>(node->number) == value.number,
            "Float value differs from its independent bit oracle.");
        require((node->text != nullptr) == value.has_text, "Typed optional-text policy differs.");
        if (value.has_text)
        {
            require(std::find(strings.begin(), strings.end(), node->text) == strings.end(),
                "Owner string allocations alias one another.");
            require(std::memcmp(node->text, value.text.c_str(), value.text.size() + 1) == 0,
                "Owned string differs from its independent record.");
            strings.push_back(node->text);
        }
        seen.push_back(node);
        node = node->next;
    }
    require(node == nullptr, "Owner added an unexpected record.");
}

void run(const std::vector<Input>& inputs,
    unsigned int separators,
    const std::filesystem::path* folder = nullptr,
    bool append_after = false)
{
    require(!inputs.empty(), "Empty-owner cleanup is outside the executed domain.");
    require(startup_config_owner == nullptr, "A previous owner publication survived cleanup.");
    std::vector<Expected> expected;
    for (const auto& input : inputs)
    {
        const auto value = oracle(input);
        duplicate_names += std::any_of(expected.begin(),
            expected.end(),
            [&](const auto& previous)
            {
                return previous.name == value.name;
            });
        expected.push_back(value);
    }
    std::string document = make_document(inputs, separators);
    const auto original = document;
    std::string filename = folder ? "data\\GENERATED-OWNER.PIS" : "Data/generated-owner.pis";
    const auto original_filename = filename;
    Storage storage;
    auto* destination = reinterpret_cast<StartupConfig*>(storage.value);
    auto observed = std::make_shared<Observation>();
    observed->destination = destination;
    observed->filename = filename;
    std::weak_ptr<const data::FileSource> weak_source;
    std::unique_ptr<StartupConfig, Destroy> owner;
    {
        std::shared_ptr<const data::FileSource> backing;
        if (folder)
        {
            std::filesystem::create_directories(*folder / "Data");
            std::ofstream output(*folder / "Data/generated-owner.pis", std::ios::binary);
            output.write(document.data(), static_cast<std::streamsize>(document.size()));
            require(static_cast<bool>(output), "Generated folder fixture could not be written.");
            output.close();
            backing = std::make_shared<data::FolderSource>(*folder,
                std::vector<data::SourceEntry>{{"Data/generated-owner.pis", document.size()}});
        }
        else
        {
            // Real VFS read loops must also handle repeated short reads.
            const auto chunk = separators % 2 ? 7 : std::numeric_limits<std::size_t>::max();
            backing = std::make_shared<MemorySource>(document, chunk);
        }
        auto source = std::make_shared<ObservedSource>(std::move(backing), observed);
        weak_source = source;
        files::Session session(source);
        owner.reset(std::construct_at(destination, filename.c_str()));
        require(owner.get() == destination && startup_config_owner == owner.get(),
            "Constructor did not publish its actual owner.");
        require(observed->initialized && observed->opens == 3,
            "Owner publication and null head did not precede all three opens.");
        require(observed->protocol && observed->closes == 3 && observed->seeks == 1 &&
                    observed->positions == 1 && observed->reads != 0 &&
                    observed->sought == document.size() && observed->read_bytes == document.size(),
            "Actual VFS load protocol differs from the bounded document.");
        require(document == original && filename == original_filename,
            "Constructor modified borrowed input.");
    }
    require(weak_source.expired(), "Configuration retained a VFS mount after the session.");
    std::fill(document.begin(), document.end(), 'X');
    std::fill(filename.begin(), filename.end(), 'Y');
    verify_nodes(*owner, expected);
    if (append_after)
    {
        // Exercise the actual Append method on the constructed nonempty list,
        // then invalidate borrowed bytes before checking its owned copies.
        std::string name = expected.front().name;
        std::string text = "appended owned string";
        Expected value{name, true, -17, 0x40200000U, true, text};
        owner->Append(name.c_str(), value.flag, value.integer, 2.5f, text.c_str());
        expected.push_back(value);
        std::fill(name.begin(), name.end(), 'N');
        std::fill(text.begin(), text.end(), 'T');
        verify_nodes(*owner, expected);
        ++appended;
    }
    require(storage.intact(), "Construction changed adjacent nonowned storage.");
    owner.reset();
    require(startup_config_owner == nullptr, "Actual owner destruction did not clear publication.");
    require(storage.intact(), "Destruction changed adjacent nonowned storage.");
    for (const auto& input : inputs)
    {
        const auto tag = input.name[0];
        ++typed_records[tag == 'b' ? 0 : tag == 'i' ? 1 : tag == 'f' ? 2 : 3];
        selected_fractions += input.selected_fraction;
        embedded_nuls += input.name.find('\0') != std::string::npos ||
                         input.text.find('\0') != std::string::npos;
        longest_name = std::max(longest_name, input.name.size());
        longest_text = std::max(longest_text, input.text.size());
    }
    records += expected.size();
    folder_cases += folder != nullptr;
    ++cases;
}

std::string payload(std::size_t length, unsigned int seed, char excluded)
{
    std::string result(length, 'x');
    for (std::size_t index = 0; index != length; ++index)
    {
        const auto byte = static_cast<unsigned char>(1 + (index * 73 + seed) % 255);
        result[index] =
            byte == static_cast<unsigned char>(excluded) ? '_' : static_cast<char>(byte);
    }
    return result;
}

void generated_cases()
{
    const std::array<std::string, 10> booleans = {"true",
        "false",
        "TRUE",
        "True",
        "",
        "true ",
        "truex",
        std::string("true\0tail", 9),
        std::string("\xfftrue", 5),
        "falsey"};
    for (unsigned int separator = 0; separator != 4; ++separator)
    {
        for (const auto& value : booleans)
        {
            run({{"bFirst", value},
                    {"iNumber", {}, -1000000, true},
                    {"fNumber", {}, 1000000},
                    {"sMessage", "owned"},
                    {"bFirst", "false"}},
                separator,
                nullptr,
                true);
        }
    }
    for (std::size_t length = 1; length <= 255; ++length)
    {
        auto name = "s" + payload(length - 1, static_cast<unsigned int>(length), '\'');
        auto text = payload(255 - length, static_cast<unsigned int>(length * 3), '"');
        run({{name, text}, {"bFlag", "true"}}, static_cast<unsigned int>(length));
    }
    run({{"sLongest", payload(255, 12, '"')},
            {"fZero", {}, 0, false, true},
            {"iZero", {}, 0, true, true},
            {std::string("b\0ignored", 9), "true"},
            {"sNul", std::string("ok\0ignored", 10)},
            {"fSelected", {}, 0, false, false, true}},
        2);
    unsigned int random = 0xca40ce90U;
    for (unsigned int trial = 0; trial != 192; ++trial)
    {
        std::vector<Input> inputs;
        for (unsigned int index = 0; index <= trial % 13; ++index)
        {
            static const char types[] = {'b', 'i', 'f', 's'};
            random = random * 1664525U + 1013904223U;
            const auto tag = types[(trial + index) % 4];
            Input input;
            input.name = std::string(1, tag) + payload(1 + random % 48, random, '\'');
            input.integer = static_cast<int>(random % 2000001U) - 1000000;
            input.zero_fraction = index % 2 != 0;
            input.text = tag == 'b' ? booleans[(trial + index) % booleans.size()]
                                    : payload(random % 128, random, '"');
            inputs.push_back(std::move(input));
        }
        run(inputs, trial, nullptr, trial % 3 == 0);
    }
    for (int integer = -2048; integer <= 2048; ++integer)
    {
        run({{"iExact", {}, integer, true}, {"fExact", {}, integer, true}},
            static_cast<unsigned int>(integer + 2048));
        ++numeric_cases;
    }
}

void folder_inputs(const std::filesystem::path& folder)
{
    for (unsigned int separator = 0; separator != 4; ++separator)
    {
        run({{"bFolder", "true"},
                {"sFolder", "owned folder text"},
                {"iFolder", {}, -123, true},
                {"fFolder", {}, 0, false, false, true},
                {"sFolder", std::string("prefix\0suffix", 13)}},
            separator,
            &folder,
            true);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (std::atexit(require_completion) != 0)
    {
        std::cerr << "Startup configuration completion guard could not be registered.\n";
        return EXIT_FAILURE;
    }
    try
    {
        require(argc == 2, "Expected a writable generated-fixture directory.");
        generated_cases();
        folder_inputs(argv[1]);
        require(longest_name == 255 && longest_text == 255 && embedded_nuls != 0 &&
                    duplicate_names != 0 && appended != 0 && selected_fractions != 0,
            "Generated boundary coverage is incomplete.");
        for (const auto count : typed_records)
        {
            require(count != 0, "A typed configuration domain was not exercised.");
        }
        std::cout << "{\"cases\":" << cases << ",\"checks\":" << checks
                  << ",\"records\":" << records << ",\"appended\":" << appended
                  << ",\"owner_lifetimes\":" << cases << ",\"folder_cases\":" << folder_cases
                  << ",\"numeric_cases\":" << numeric_cases
                  << ",\"selected_fractions\":" << selected_fractions
                  << ",\"embedded_nuls\":" << embedded_nuls
                  << ",\"duplicate_names\":" << duplicate_names << ",\"max_name\":" << longest_name
                  << ",\"max_text\":" << longest_text << ",\"typed_records\":[" << typed_records[0]
                  << ',' << typed_records[1] << ',' << typed_records[2] << ',' << typed_records[3]
                  << "]}\n";
        std::cout.flush();
        if (!std::cout)
        {
            throw std::runtime_error("Fixture completion output failed.");
        }
        completed = true;
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Startup configuration fixture failure: " << error.what() << '\n';
        return 1;
    }
}

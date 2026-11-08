#include "clickteam_patch.h"

#include "platform/sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace beachlife::data
{
namespace
{
constexpr std::size_t max_patch_bytes = 64 * 1024 * 1024;
constexpr std::size_t max_output_bytes = 256 * 1024 * 1024;
// The stub records its size after this marker within its first 0x16000 bytes.
constexpr std::array<unsigned char, 6> stub_marker{0x77, 0x77, 0x67, 0x54, 0x29, 0x48};
constexpr std::size_t stub_scan_bytes = 0x16000;
constexpr std::uint16_t patch_list_tag = 0x1242, end_tag = 0x7f7f;

[[noreturn]] void damaged()
{
    throw std::runtime_error("The 1.1 patch file is damaged or not a supported patch.");
}

void require(bool valid)
{
    if (!valid)
    {
        damaged();
    }
}

std::uint32_t u16(std::span<const unsigned char> bytes, std::size_t at)
{
    require(at <= bytes.size() && bytes.size() - at >= 2);
    return bytes[at] | (bytes[at + 1] << 8);
}

std::uint32_t u32(std::span<const unsigned char> bytes, std::size_t at)
{
    require(at <= bytes.size() && bytes.size() - at >= 4);
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::span<const unsigned char> slice(
    std::span<const unsigned char> bytes, std::size_t at, std::size_t size)
{
    require(at <= bytes.size() && bytes.size() - at >= size);
    return bytes.subspan(at, size);
}

class Bits
{
  public:
    explicit Bits(std::span<const unsigned char> input)
        : input_(input)
    {
    }
    unsigned get(int count)
    {
        std::uint32_t value = buffer_;
        while (available_ < count)
        {
            require(position_ < input_.size());
            value |= static_cast<std::uint32_t>(input_[position_++]) << available_;
            available_ += 8;
        }
        buffer_ = value >> count;
        available_ -= count;
        return value & ((1u << count) - 1);
    }
    // Drops the rest of the current byte and returns the next whole bytes.
    std::span<const unsigned char> bytes(std::size_t count)
    {
        buffer_ = 0;
        available_ = 0;
        const auto result = slice(input_, position_, count);
        position_ += count;
        return result;
    }

  private:
    std::span<const unsigned char> input_;
    std::size_t position_ = 0;
    std::uint32_t buffer_ = 0;
    int available_ = 0;
};

// Canonical Huffman decoding by code-length counts, as in zlib's puff.
struct Huffman
{
    std::array<std::uint16_t, 16> count{};
    std::vector<std::uint16_t> symbol;

    explicit Huffman(std::span<const std::uint8_t> lengths)
        : symbol(lengths.size())
    {
        for (const auto length : lengths)
        {
            ++count[length];
        }
        int left = 1;
        for (std::size_t length = 1; length < count.size(); ++length)
        {
            left = (left << 1) - count[length];
            require(left >= 0);
        }
        std::array<std::uint16_t, 16> offsets{};
        for (std::size_t length = 1; length + 1 < count.size(); ++length)
        {
            offsets[length + 1] = static_cast<std::uint16_t>(offsets[length] + count[length]);
        }
        for (std::size_t value = 0; value < lengths.size(); ++value)
        {
            if (lengths[value])
            {
                symbol[offsets[lengths[value]]++] = static_cast<std::uint16_t>(value);
            }
        }
    }
    unsigned decode(Bits& bits) const
    {
        int code = 0, first = 0, index = 0;
        for (std::size_t length = 1; length < count.size(); ++length)
        {
            code |= static_cast<int>(bits.get(1));
            const int here = count[length];
            if (code - here < first)
            {
                return symbol[static_cast<std::size_t>(index + code - first)];
            }
            index += here;
            first = (first + here) << 1;
            code <<= 1;
        }
        damaged();
    }
};

constexpr std::array<std::uint16_t, 29> length_base{3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    13,
    15,
    17,
    19,
    23,
    27,
    31,
    35,
    43,
    51,
    59,
    67,
    83,
    99,
    115,
    131,
    163,
    195,
    227,
    258};
constexpr std::array<std::uint8_t, 29> length_extra{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> distance_base{1,
    2,
    3,
    4,
    5,
    7,
    9,
    13,
    17,
    25,
    33,
    49,
    65,
    97,
    129,
    193,
    257,
    385,
    513,
    769,
    1025,
    1537,
    2049,
    3073,
    4097,
    6145,
    8193,
    12289,
    16385,
    24577};
constexpr std::array<std::uint8_t, 30> distance_extra{0,
    0,
    0,
    0,
    1,
    1,
    2,
    2,
    3,
    3,
    4,
    4,
    5,
    5,
    6,
    6,
    7,
    7,
    8,
    8,
    9,
    9,
    10,
    10,
    11,
    11,
    12,
    12,
    13,
    13};
constexpr std::array<std::uint8_t, 19> code_length_order{
    18, 17, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

void codes(Bits& bits,
    const Huffman& literals,
    const Huffman& distances,
    std::vector<unsigned char>& out,
    std::size_t size)
{
    for (;;)
    {
        const auto value = literals.decode(bits);
        if (value < 256)
        {
            require(out.size() < size);
            out.push_back(static_cast<unsigned char>(value));
            continue;
        }
        if (value == 256)
        {
            return;
        }
        const auto index = value - 257;
        require(index < length_base.size());
        const auto length = length_base[index] + bits.get(length_extra[index]);
        const auto code = distances.decode(bits);
        require(code < distance_base.size());
        const auto distance = distance_base[code] + bits.get(distance_extra[code]);
        require(distance <= out.size() && length <= size - out.size());
        for (unsigned i = 0; i < length; ++i)
        {
            out.push_back(out[out.size() - distance]);
        }
    }
}

std::int64_t sign_extend(std::uint32_t value, int bit, std::int64_t offset)
{
    if (value & (1u << bit))
    {
        return static_cast<std::int32_t>(value | (0xffffffffu << bit)) - offset;
    }
    return static_cast<std::int64_t>(value) + offset;
}

// A patch stream copies runs of the original (at offsets relative to the end
// of the previous run) and inserts literals from a second stream. Reads beyond
// the original produce zero bytes.
std::vector<unsigned char> apply_runs(std::span<const unsigned char> original,
    std::span<const unsigned char> runs,
    std::span<const unsigned char> literals,
    std::size_t size)
{
    std::vector<unsigned char> out;
    out.reserve(size);
    std::size_t at = 0, literal = 0;
    std::uint32_t base = 0;
    const auto next = [&]
    {
        require(at < runs.size());
        return runs[at++];
    };
    while (at < runs.size())
    {
        auto code = next();
        std::uint32_t copy = 0;
        if (code & 0x80)
        {
            std::uint32_t count = (code >> 5) & 3;
            if (count == 0)
            {
                count = (code & 0x1f) + 1u;
            }
            else
            {
                copy = (code & 0x1f) + 2u;
            }
            const auto bytes = slice(literals, literal, count);
            require(bytes.size() <= size - out.size());
            out.insert(out.end(), bytes.begin(), bytes.end());
            literal += count;
        }
        else if (code == 0)
        {
            copy = u16(runs, at) + 0x81u;
            at += 2;
        }
        else
        {
            copy = code + 1u;
        }
        if (!copy)
        {
            continue;
        }
        code = next();
        std::int64_t delta = 0;
        if (!(code & 0x80))
        {
            delta = sign_extend(((code & 0x7fu) << 8) | next(), 14, 0x20);
        }
        else if (!(code & 0x40))
        {
            delta = sign_extend(code & 0x3fu, 5, 0);
        }
        else
        {
            const std::uint32_t high = next();
            const std::uint32_t value = ((code & 0x3fu) << 16) | (high << 8) | next();
            if (value == 0x100000)
            {
                std::uint32_t exact = 0;
                for (int i = 0; i < 4; ++i)
                {
                    exact = (exact << 8) | next();
                }
                delta = static_cast<std::int32_t>(exact);
            }
            else
            {
                delta = sign_extend(value, 21, 0x4020);
            }
        }
        require(copy <= size - out.size());
        const std::uint32_t from = base + static_cast<std::uint32_t>(delta);
        for (std::uint32_t i = 0; i < copy; ++i)
        {
            const std::uint64_t position = static_cast<std::uint64_t>(from) + i;
            out.push_back(from < original.size() && position < original.size()
                              ? original[static_cast<std::size_t>(position)]
                              : 0);
        }
        base += static_cast<std::uint32_t>(delta) + copy;
    }
    require(literal == literals.size());
    return out;
}
} // namespace

std::uint32_t clickteam_checksum(std::span<const unsigned char> bytes)
{
    std::uint32_t value = 0;
    const auto words = bytes.size() & ~std::size_t{3};
    for (std::size_t i = 0; i < words; i += 4)
    {
        const std::uint32_t word = bytes[i] | (bytes[i + 1] << 8) | (bytes[i + 2] << 16) |
                                   (static_cast<std::uint32_t>(bytes[i + 3]) << 24);
        value = word + (value >> 31) + (value << 1);
    }
    for (std::size_t i = words; i < bytes.size(); ++i)
    {
        value = bytes[i] + (value >> 31) + (value << 1);
    }
    return value;
}

std::vector<unsigned char> clickteam_inflate(std::span<const unsigned char> input, std::size_t size)
{
    require(size <= max_output_bytes);
    std::vector<unsigned char> out;
    out.reserve(size);
    Bits bits(input);
    for (bool last = false; !last;)
    {
        const auto type = bits.get(3);
        last = bits.get(1) != 0;
        if (type == 7)
        {
            const auto length = u16(bits.bytes(2), 0);
            const auto stored = bits.bytes(length);
            require(stored.size() <= size - out.size());
            out.insert(out.end(), stored.begin(), stored.end());
        }
        else if (type == 5)
        {
            std::array<std::uint8_t, 288> lengths{};
            std::fill(lengths.begin(), lengths.begin() + 144, 8);
            std::fill(lengths.begin() + 144, lengths.begin() + 256, 9);
            std::fill(lengths.begin() + 256, lengths.begin() + 280, 7);
            std::fill(lengths.begin() + 280, lengths.end(), 8);
            std::array<std::uint8_t, 30> distances{};
            distances.fill(5);
            codes(bits, Huffman(lengths), Huffman(distances), out, size);
        }
        else if (type == 6)
        {
            const auto literal_count = bits.get(5) + 257;
            const auto distance_count = bits.get(5) + 1;
            const auto code_count = bits.get(4) + 4;
            require(literal_count <= 286 && distance_count <= 30);
            std::array<std::uint8_t, 19> code_lengths{};
            for (unsigned i = 0; i < code_count; ++i)
            {
                code_lengths[code_length_order[i]] = static_cast<std::uint8_t>(bits.get(3));
            }
            const Huffman code_lengths_code(code_lengths);
            std::vector<std::uint8_t> lengths;
            while (lengths.size() < literal_count + distance_count)
            {
                const auto value = code_lengths_code.decode(bits);
                std::size_t repeat = 1;
                std::uint8_t length = static_cast<std::uint8_t>(value);
                if (value == 16)
                {
                    require(!lengths.empty());
                    length = lengths.back();
                    repeat = 3 + bits.get(2);
                }
                else if (value == 17)
                {
                    length = 0;
                    repeat = 3 + bits.get(3);
                }
                else if (value == 18)
                {
                    length = 0;
                    repeat = 11 + bits.get(7);
                }
                require(lengths.size() + repeat <= literal_count + distance_count);
                lengths.insert(lengths.end(), repeat, length);
            }
            require(lengths[256] != 0);
            const auto all = std::span<const std::uint8_t>(lengths);
            codes(bits,
                Huffman(all.first(literal_count)),
                Huffman(all.subspan(literal_count)),
                out,
                size);
        }
        else
        {
            damaged();
        }
    }
    require(out.size() == size);
    return out;
}

ClickteamPatch::ClickteamPatch(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        throw std::runtime_error("Cannot open the 1.1 patch file.");
    }
    const auto size = static_cast<std::uint64_t>(input.tellg());
    require(size >= 64 && size <= max_patch_bytes);
    image_.resize(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(
        input.read(reinterpret_cast<char*>(image_.data()), static_cast<std::streamsize>(size))));
    sha256_ = [&]
    {
        platform::Sha256 hash;
        hash.update(image_.data(), image_.size());
        return hash.finish_hex();
    }();
    const std::span<const unsigned char> image(image_);
    require(image[0] == 'M' && image[1] == 'Z');

    const auto scan = image.first(std::min(image.size(), stub_scan_bytes));
    const auto marker =
        std::search(scan.begin(), scan.end(), stub_marker.begin(), stub_marker.end());
    require(marker != scan.end());
    std::size_t at =
        u32(image, static_cast<std::size_t>(marker - scan.begin()) + stub_marker.size());
    std::optional<std::vector<unsigned char>> list;
    for (;;)
    {
        const auto tag = u16(image, at);
        const auto length = u32(image, at + 4);
        if (tag == end_tag)
        {
            // The data area follows its own 32-bit size.
            data_size_ = length;
            data_ = at + 12;
            require(u32(image, at + 8) == length && slice(image, data_, data_size_).size());
            break;
        }
        const auto payload = slice(image, at + 8, length);
        if (tag == patch_list_tag)
        {
            if (u16(image, at + 2) & 1)
            {
                list = clickteam_inflate(payload.subspan(4), u32(payload, 0));
            }
            else
            {
                list.emplace(payload.begin(), payload.end());
            }
        }
        at += 8 + std::size_t{length};
    }
    require(list.has_value());

    // u32 count, then entries: u32 length, [7] operation, [8] type (bit 1:
    // added file), [0x0a] size, [0x0e] checksum, [0x12] u16 original count,
    // [0x16] u16 table offset, [0x36] name; table rows are 16 bytes (original
    // checksum, original size, data offset, data length).
    const std::span<const unsigned char> records(*list);
    const auto count = u32(records, 0);
    require(count <= 100000);
    std::size_t position = 4;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto record = slice(records, position, u32(records, position));
        require(record.size() > 0x36);
        position += record.size();
        Entry entry;
        const auto name = record.subspan(0x36);
        const auto end = std::find(name.begin(), name.end(), 0);
        require(end != name.end() && end != name.begin());
        entry.path.assign(name.begin(), end);
        entry.supported = record[7] == 0;
        entry.added = (record[8] & 2) != 0;
        entry.size = u32(record, 0x0a);
        entry.checksum = u32(record, 0x0e);
        const auto originals = u16(record, 0x12);
        const auto table = u16(record, 0x16);
        for (std::uint32_t row = 0; row < originals; ++row)
        {
            const auto at_row = table + row * 16;
            entry.originals.push_back(
                {u32(record, at_row + 4), u32(record, at_row + 8), u32(record, at_row + 12)});
            slice(image.subspan(data_, data_size_),
                entry.originals.back().offset,
                entry.originals.back().length);
        }
        require(!entry.added || !entry.originals.empty());
        entries_.push_back(std::move(entry));
    }
}

std::optional<std::vector<unsigned char>> ClickteamPatch::apply(
    const Entry& entry, std::span<const unsigned char> original) const
{
    if (original.size() == entry.size && clickteam_checksum(original) == entry.checksum)
    {
        return std::vector<unsigned char>(original.begin(), original.end());
    }
    const auto data = std::span<const unsigned char>(image_).subspan(data_, data_size_);
    // Patch data: a block holding the run stream, then one holding the
    // literals. Block header: codec (bit 0 compressed, bit 1 32-bit unpacked
    // size, codec & 5 == 5: 32-bit packed size, == 1: 16-bit packed size).
    const auto block = [&](std::size_t& at)
    {
        const auto codec = data[at];
        require(codec <= 7 && (codec & 5) != 4);
        ++at;
        std::size_t unpacked = (codec & 2) ? u32(data, at) : u16(data, at);
        at += (codec & 2) ? 4 : 2;
        std::size_t packed = unpacked;
        if ((codec & 5) == 5)
        {
            packed = u32(data, at);
            at += 4;
        }
        else if ((codec & 5) == 1)
        {
            packed = u16(data, at);
            at += 2;
        }
        const auto bytes = slice(data, at, packed);
        at += packed;
        return (codec & 1) ? clickteam_inflate(bytes, unpacked)
                           : std::vector<unsigned char>(bytes.begin(), bytes.end());
    };
    for (const auto& candidate : entry.originals)
    {
        if (entry.added || candidate.size != original.size())
        {
            continue;
        }
        std::size_t at = candidate.offset;
        require(at < data.size());
        const auto runs = block(at);
        const auto literals = block(at);
        auto result = apply_runs(original, runs, literals, entry.size);
        if (result.size() == entry.size && clickteam_checksum(result) == entry.checksum)
        {
            return result;
        }
    }
    for (const auto& candidate : entry.originals)
    {
        if (candidate.size == original.size())
        {
            throw std::runtime_error("The 1.1 patch produced a file that fails its checksum.");
        }
    }
    return std::nullopt;
}

std::vector<unsigned char> ClickteamPatch::added(const Entry& entry) const
{
    require(entry.added && !entry.originals.empty());
    const auto& stored = entry.originals.front();
    const auto data = std::span<const unsigned char>(image_).subspan(data_, data_size_);
    auto result = clickteam_inflate(slice(data, stored.offset, stored.length), entry.size);
    require(clickteam_checksum(result) == entry.checksum);
    return result;
}

} // namespace beachlife::data

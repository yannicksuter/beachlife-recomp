#include "cursor_resource.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <tuple>

namespace beachlife::compat
{
namespace
{
using Bytes = std::span<const unsigned char>;

[[noreturn]] void invalid()
{
    throw std::runtime_error("Invalid or missing PE cursor resource");
}

Bytes slice(Bytes bytes, std::size_t offset, std::size_t count)
{
    if (offset > bytes.size() || count > bytes.size() - offset)
    {
        invalid();
    }
    return bytes.subspan(offset, count);
}

std::uint16_t u16(Bytes bytes, std::size_t offset)
{
    const auto word = slice(bytes, offset, 2);
    return word[0] | (std::uint16_t(word[1]) << 8);
}

std::uint32_t u32(Bytes bytes, std::size_t offset)
{
    const auto word = slice(bytes, offset, 4);
    return word[0] | (std::uint32_t(word[1]) << 8) | (std::uint32_t(word[2]) << 16) |
           (std::uint32_t(word[3]) << 24);
}

class Resources
{
  public:
    explicit Resources(Bytes executable)
        : bytes_(executable)
    {
        if (u16(bytes_, 0) != 0x5a4d)
        {
            invalid();
        }
        const auto pe = slice(bytes_, u32(bytes_, 0x3c), 24);
        if (u32(pe, 0) != 0x4550 || u16(pe, 4) != 0x14c)
        {
            invalid();
        }
        const auto optional_offset = std::size_t(u32(bytes_, 0x3c)) + 24;
        const auto optional = slice(bytes_, optional_offset, u16(pe, 20));
        if (u16(optional, 0) != 0x10b || u32(optional, 92) <= 2)
        {
            invalid();
        }
        image_size_ = u32(optional, 56);
        headers_size_ = u32(optional, 60);
        sections_ = slice(bytes_, optional_offset + optional.size(), std::size_t(u16(pe, 6)) * 40);
        directory_ = map(u32(optional, 112), u32(optional, 116));
        slice(directory_, 0, 16);
    }

    struct Resource
    {
        Bytes bytes;
        std::uint16_t language;
    };

    Resource get(
        std::uint16_t type, std::uint16_t name, std::optional<std::uint16_t> language = {}) const
    {
        const auto names = child(0, type, true);
        const auto languages = child(names, name, true);
        const auto entries = directory_entries(languages);
        std::optional<std::uint16_t> selected;
        unsigned best = 4;
        for (std::size_t at = 0; at < entries.size(); at += 8)
        {
            const auto id = u32(entries, at);
            if (id > 0xffff)
            {
                continue;
            }
            // These executable resources normally have one language. Prefer
            // the group's language for its image, then neutral, then English.
            const unsigned rank = language && id == *language ? 0
                                  : id == 0                   ? 1
                                  : id == 0x409               ? 2
                                                              : 3;
            if (rank < best)
            {
                selected = static_cast<std::uint16_t>(id);
                best = rank;
            }
        }
        if (!selected)
        {
            invalid();
        }
        const auto data = slice(directory_, child(languages, *selected, false), 16);
        return {map(u32(data, 0), u32(data, 4)), *selected};
    }

  private:
    Bytes map(std::uint32_t rva, std::uint32_t count) const
    {
        if (!rva || !count || rva >= image_size_ || count > image_size_ - rva)
        {
            invalid();
        }
        std::optional<std::size_t> offset;
        if (rva < headers_size_ && count <= headers_size_ - rva)
        {
            offset = rva;
        }
        for (std::size_t at = 0; at < sections_.size(); at += 40)
        {
            const auto start = u32(sections_, at + 12);
            const auto size = u32(sections_, at + 16);
            if (rva < start || rva - start >= size || count > size - (rva - start))
            {
                continue;
            }
            if (offset)
            {
                invalid();
            }
            offset = std::size_t(u32(sections_, at + 20)) + (rva - start);
        }
        if (!offset)
        {
            invalid();
        }
        return slice(bytes_, *offset, count);
    }

    Bytes directory_entries(std::uint32_t offset) const
    {
        const auto header = slice(directory_, offset, 16);
        const std::size_t count = std::size_t(u16(header, 12)) + u16(header, 14);
        return slice(directory_, std::size_t(offset) + 16, count * 8);
    }

    std::uint32_t child(std::uint32_t directory, std::uint16_t id, bool branch) const
    {
        const auto entries = directory_entries(directory);
        for (std::size_t at = 0; at < entries.size(); at += 8)
        {
            if (u32(entries, at) == id)
            {
                const auto target = u32(entries, at + 4);
                if (bool(target & 0x80000000) != branch)
                {
                    invalid();
                }
                return target & 0x7fffffff;
            }
        }
        invalid();
    }

    Bytes bytes_, sections_, directory_;
    std::uint32_t image_size_ = 0, headers_size_ = 0;
};

struct Frame
{
    int width, height;
    unsigned depth;
    std::uint32_t bytes;
    std::uint16_t id;
};

Frame frame(Bytes group, std::size_t index)
{
    const auto entry = slice(group, 6 + index * 14, 14);
    const int width = u16(entry, 0), double_height = u16(entry, 2);
    if (!width || !double_height || double_height % 2 || u16(entry, 4) != 1)
    {
        invalid();
    }
    return {width, double_height / 2, u16(entry, 6), u32(entry, 8), u16(entry, 12)};
}

Frame select_frame(Bytes group, int& width, int& height)
{
    if (u16(group, 0) || u16(group, 2) != 2 || !u16(group, 4) || width < 0 || height < 0)
    {
        invalid();
    }
    const auto count = u16(group, 4);
    slice(group, 6, std::size_t(count) * 14);
    Frame selected = frame(group, 0);
    width = width ? width : selected.width;
    height = height ? height : selected.height;
    const auto rank = [&](const Frame& candidate)
    {
        // LookupIconIdFromDirectoryEx: closest size not exceeding the request,
        // then display depth. The SDL host uses a 32-bit colour cursor surface.
        const bool exceeds = candidate.width > width || candidate.height > height;
        const std::int64_t distance =
            exceeds ? candidate.width + candidate.height
                    : std::int64_t(width) - candidate.width + height - candidate.height;
        const bool too_deep = candidate.depth > 32;
        return std::tuple(
            exceeds, distance, too_deep, too_deep ? candidate.depth - 32 : 32 - candidate.depth);
    };
    for (std::size_t index = 1; index < count; ++index)
    {
        const Frame candidate = frame(group, index);
        if (rank(candidate) < rank(selected))
        {
            selected = candidate;
        }
    }
    return selected;
}

bool valid_mask(std::uint32_t mask, unsigned depth)
{
    if (!mask || (depth < 32 && mask >> depth))
    {
        return false;
    }
    const auto value = mask >> std::countr_zero(mask);
    return !(value & (value + 1));
}

unsigned char component(std::uint32_t pixel, std::uint32_t mask)
{
    const auto value = (pixel & mask) >> std::countr_zero(mask);
    const unsigned width = std::popcount(mask);
    if (width >= 8)
    {
        return static_cast<unsigned char>(value >> (width - 8));
    }
    // Match DIB channel expansion: repeat the high bits (RGB555 uses
    // (value << 3) | (value >> 2)), as in the compat GDI conversion.
    auto expanded = value;
    unsigned bits = width;
    while (bits < 8)
    {
        expanded = (expanded << width) | value;
        bits += width;
    }
    return static_cast<unsigned char>(expanded >> (bits - 8));
}

CursorImage decode_dib(Bytes resource, const Frame& selected, int width, int height)
{
    const auto hot_x = u16(resource, 0), hot_y = u16(resource, 2);
    const auto dib = resource.subspan(4);
    const auto header_size = u32(dib, 0);
    if (header_size != 40 && header_size != 52 && header_size != 56 && header_size != 108 &&
        header_size != 124)
    {
        throw UnsupportedCursor("cursor requires a Windows DIB header (PNG/core DIB unsupported)");
    }
    slice(dib, 0, header_size);
    const auto source_width = std::bit_cast<std::int32_t>(u32(dib, 4));
    const auto signed_height = std::bit_cast<std::int32_t>(u32(dib, 8));
    if (source_width <= 0 || !signed_height || signed_height == std::numeric_limits<int>::min() ||
        signed_height % 2 || u16(dib, 12) != 1)
    {
        invalid();
    }
    const int source_height = (signed_height < 0 ? -signed_height : signed_height) / 2;
    const auto depth = u16(dib, 14);
    const auto compression = u32(dib, 16);
    if (source_width != selected.width || source_height != selected.height ||
        depth != selected.depth || hot_x >= source_width || hot_y >= source_height)
    {
        invalid();
    }
    if ((compression != 0 && compression != 3) ||
        (compression == 3 && depth != 16 && depth != 32) ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32))
    {
        throw UnsupportedCursor("cursor DIB depth or compression is not supported");
    }
    std::array<std::uint32_t, 3> masks =
        depth == 16 ? std::array<std::uint32_t, 3>{0x7c00, 0x03e0, 0x001f}
                    : std::array<std::uint32_t, 3>{0xff0000, 0x00ff00, 0x0000ff};
    std::size_t palette_at = header_size;
    if (compression == 3)
    {
        for (unsigned channel = 0; channel < 3; ++channel)
        {
            masks[channel] = u32(dib, 40 + channel * 4);
            if (!valid_mask(masks[channel], depth))
            {
                invalid();
            }
        }
        if ((masks[0] & masks[1]) || (masks[0] & masks[2]) || (masks[1] & masks[2]))
        {
            invalid();
        }
        palette_at = std::max<std::size_t>(palette_at, 52);
    }
    // Embedded nonstandard alpha masks need a separate conversion path.
    if (header_size >= 56 && u32(dib, 52) && u32(dib, 52) != 0xff000000)
    {
        throw UnsupportedCursor("cursor DIB has a nonstandard alpha mask");
    }
    auto colours = u32(dib, 32);
    if (depth <= 8)
    {
        colours = colours ? colours : 1U << depth;
        if (colours > (1U << depth))
        {
            invalid();
        }
    }
    const auto palette = slice(dib, palette_at, std::size_t(colours) * 4);
    const std::size_t xor_stride = ((std::size_t(source_width) * depth + 31) / 32) * 4;
    const std::size_t and_stride = ((std::size_t(source_width) + 31) / 32) * 4;
    const auto xor_bits = slice(dib, palette_at + palette.size(), xor_stride * source_height);
    const auto and_bits =
        slice(dib, palette_at + palette.size() + xor_bits.size(), and_stride * source_height);
    bool alpha = false;
    if (depth == 32)
    {
        for (std::size_t at = 3; at < xor_bits.size(); at += 4)
        {
            alpha |= xor_bits[at] != 0;
        }
    }
    // Bound host allocations independently of potentially huge PE dimensions.
    constexpr std::uint64_t max_pixels = 16 * 1024 * 1024;
    if (std::uint64_t(width) * height > max_pixels)
    {
        invalid();
    }
    CursorImage image{width,
        height,
        int(std::int64_t(hot_x) * width / source_width),
        int(std::int64_t(hot_y) * height / source_height),
        {}};
    image.rgba.resize(std::size_t(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        const auto sy = std::int64_t(y) * source_height / height;
        const auto row = signed_height < 0 ? sy : source_height - 1 - sy;
        for (int x = 0; x < width; ++x)
        {
            const auto sx = std::int64_t(x) * source_width / width;
            std::array<unsigned char, 4> pixel{0, 0, 0, 255};
            if (depth <= 8)
            {
                const auto bit = sx * depth;
                const auto index = (xor_bits[row * xor_stride + bit / 8] >> (8 - depth - bit % 8)) &
                                   ((1U << depth) - 1);
                if (index >= colours)
                {
                    invalid();
                }
                pixel[0] = palette[index * 4 + 2];
                pixel[1] = palette[index * 4 + 1];
                pixel[2] = palette[index * 4];
            }
            else
            {
                const auto at = row * xor_stride + sx * (depth / 8);
                const auto value = depth == 16   ? u16(xor_bits, at)
                                   : depth == 32 ? u32(xor_bits, at)
                                                 : std::uint32_t(xor_bits[at]) |
                                                       (std::uint32_t(xor_bits[at + 1]) << 8) |
                                                       (std::uint32_t(xor_bits[at + 2]) << 16);
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    pixel[channel] = component(value, masks[channel]);
                }
                if (alpha)
                {
                    pixel[3] = xor_bits[at + 3];
                }
            }
            if (!alpha && (and_bits[row * and_stride + sx / 8] & (0x80 >> (sx % 8))))
            {
                if (pixel[0] || pixel[1] || pixel[2])
                {
                    throw UnsupportedCursor("screen-dependent AND/XOR cursor pixels cannot be "
                                            "represented by SDL_CreateColorCursor");
                }
                pixel[3] = 0;
            }
            std::copy(
                pixel.begin(), pixel.end(), image.rgba.begin() + (std::size_t(y) * width + x) * 4);
        }
    }
    return image;
}
} // namespace

CursorImage decode_cursor_resource(Bytes executable, std::uint16_t resource, int width, int height)
{
    const Resources resources(executable);
    const auto group = resources.get(12, resource); // RT_GROUP_CURSOR
    const auto selected = select_frame(group.bytes, width, height);
    const auto cursor = resources.get(1, selected.id, group.language); // RT_CURSOR
    return decode_dib(slice(cursor.bytes, 0, selected.bytes), selected, width, height);
}
} // namespace beachlife::compat

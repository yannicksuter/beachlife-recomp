#include "shell_link.h"

#include "ansi_text.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace beachlife::compat::shell
{
namespace
{
void require(bool condition, const char* detail)
{
    if (!condition)
    {
        throw InvalidLink(detail);
    }
}

class Bytes
{
  public:
    explicit Bytes(std::span<const unsigned char> data)
        : data_(data)
    {
    }
    std::size_t size() const
    {
        return data_.size();
    }
    Bytes range(std::size_t offset, std::size_t count) const
    {
        require(offset <= size() && count <= size() - offset, "truncated ShellLink structure");
        return Bytes(data_.subspan(offset, count));
    }
    unsigned u16(std::size_t offset) const
    {
        const auto part = range(offset, 2).data_;
        return unsigned(part[0]) | (unsigned(part[1]) << 8);
    }
    DWORD u32(std::size_t offset) const
    {
        return DWORD(u16(offset)) | (DWORD(u16(offset + 2)) << 16);
    }
    std::string text(std::size_t offset, std::size_t count, bool wide) const
    {
        const auto part = range(offset, count * (wide ? 2 : 1));
        if (!wide)
        {
            require(std::find(part.data_.begin(), part.data_.end(), 0) == part.data_.end(),
                "embedded NUL in ShellLink string");
            return ansi_to_utf8(
                std::string_view(reinterpret_cast<const char*>(part.data_.data()), count));
        }
        std::string result;
        for (std::size_t i = 0; i < count; ++i)
        {
            std::uint32_t code = part.u16(i * 2);
            require(code != 0, "embedded NUL in ShellLink string");
            if (code >= 0xd800 && code <= 0xdbff)
            {
                require(++i < count, "truncated UTF-16 surrogate pair");
                const auto tail = part.u16(i * 2);
                require(tail >= 0xdc00 && tail <= 0xdfff, "invalid UTF-16 surrogate pair");
                code = 0x10000 + ((code - 0xd800) << 10) + tail - 0xdc00;
            }
            else
            {
                require(code < 0xdc00 || code > 0xdfff, "unpaired UTF-16 surrogate");
            }
            if (code < 0x80)
            {
                result += static_cast<char>(code);
            }
            else
            {
                const unsigned tails = code < 0x800 ? 1 : code < 0x10000 ? 2 : 3;
                result += static_cast<char>((tails == 1      ? 0xc0
                                                : tails == 2 ? 0xe0
                                                             : 0xf0) |
                                            (code >> (6 * tails)));
                for (unsigned n = tails; n > 0; --n)
                {
                    result += static_cast<char>(0x80 | ((code >> (6 * (n - 1))) & 0x3f));
                }
            }
        }
        return result;
    }
    std::string terminated(std::size_t offset, bool wide, std::size_t minimum) const
    {
        require(offset >= minimum && offset < size(), "invalid ShellLink string offset");
        const auto step = wide ? 2u : 1u;
        std::size_t end = offset;
        while (end < size())
        {
            const bool zero = wide ? u16(end) == 0 : data_[end] == 0;
            if (zero)
            {
                return text(offset, (end - offset) / step, wide);
            }
            end += step;
        }
        throw InvalidLink("unterminated ShellLink string");
    }

  private:
    std::span<const unsigned char> data_;
};

std::string link_info(const Bytes& bytes)
{
    const auto header = bytes.u32(4);
    require(header == 0x1c || (header >= 0x24 && header <= bytes.size()),
        "invalid LinkInfo header size");
    const auto flags = bytes.u32(8);
    if (flags != 1)
    {
        throw UnsupportedLink("network or unknown LinkInfo location flags");
    }
    const auto volume_offset = bytes.u32(12);
    require(volume_offset >= header, "invalid VolumeID offset");
    const auto volume = bytes.range(volume_offset, bytes.u32(volume_offset));
    require(volume.size() >= 16, "invalid VolumeID size");
    const auto label = volume.u32(12);
    if (label == 0x14)
    {
        (void)volume.terminated(volume.u32(16), true, 20);
    }
    else
    {
        (void)volume.terminated(label, false, 16);
    }
    std::string base = bytes.terminated(bytes.u32(16), false, header);
    std::string suffix = bytes.terminated(bytes.u32(24), false, header);
    if (header >= 0x24)
    {
        if (const auto offset = bytes.u32(28))
        {
            base = bytes.terminated(offset, true, header);
        }
        if (const auto offset = bytes.u32(32))
        {
            suffix = bytes.terminated(offset, true, header);
        }
    }
    require(!base.empty(), "empty LinkInfo local path");
    if (!suffix.empty())
    {
        if (base.back() != '\\' && base.back() != '/' && suffix.front() != '\\' &&
            suffix.front() != '/')
        {
            base += '\\';
        }
        base += suffix;
    }
    return base;
}
} // namespace

Target parse(std::span<const unsigned char> data)
{
    const Bytes bytes(data);
    require(bytes.size() >= 76 && bytes.u32(0) == 76, "invalid ShellLink header size");
    constexpr std::array<unsigned char, 16> clsid{
        1, 0x14, 2, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
    require(std::equal(clsid.begin(), clsid.end(), data.begin() + 4), "invalid ShellLink CLSID");
    const auto flags = bytes.u32(20);
    // Tracking policy and target metadata do not replace an explicit path.
    if (flags & ~DWORD(0xff | 0x00040000 | 0x00080000 | 0x00100000 | 0x00200000 | 0x00400000))
    {
        throw UnsupportedLink("environment, advertised, or unknown ShellLink flags");
    }
    Target target;
    target.metadata.dwFileAttributes = bytes.u32(24);
    target.metadata.ftCreationTime = {bytes.u32(28), bytes.u32(32)};
    target.metadata.ftLastAccessTime = {bytes.u32(36), bytes.u32(40)};
    target.metadata.ftLastWriteTime = {bytes.u32(44), bytes.u32(48)};
    target.metadata.nFileSizeLow = bytes.u32(52);
    std::size_t offset = 76;
    if (flags & 1)
    {
        const auto length = bytes.u16(offset);
        offset += 2;
        const auto list = bytes.range(offset, length);
        std::size_t item = 0;
        for (;;)
        {
            const auto size = list.u16(item);
            if (!size)
            {
                require(item + 2 == length, "invalid IDList terminator");
                break;
            }
            require(size >= 2, "invalid IDList item size");
            (void)list.range(item, size);
            item += size;
        }
        offset += length;
    }
    if (flags & 2)
    {
        const auto length = bytes.u32(offset);
        target.local_path = link_info(bytes.range(offset, length));
        offset += length;
    }
    for (DWORD flag = 4; flag <= 64; flag <<= 1)
    {
        if (flags & flag)
        {
            const auto count = bytes.u16(offset);
            offset += 2;
            const auto value = bytes.text(offset, count, flags & 128);
            offset += count * (flags & 128 ? 2 : 1);
            if (flag == 8)
            {
                target.relative_path = value;
            }
        }
    }
    for (;;)
    {
        const auto length = bytes.u32(offset);
        if (length < 4)
        {
            require(offset + 4 == bytes.size(), "trailing bytes after ShellLink terminal block");
            break;
        }
        require(length >= 8, "invalid ShellLink ExtraData size");
        const auto block = bytes.range(offset, length);
        switch (block.u32(4))
        {
        case 0xa0000003: // TrackerDataBlock: only used if an explicit path has moved.
            require(length == 96 && block.u32(8) == 88 && !block.u32(12), "invalid tracker block");
            break;
        case 0xa0000009: // PropertyStoreDataBlock, metadata rather than a target.
            require(length >= 12, "invalid property store block");
            break;
        default:
            throw UnsupportedLink("ShellLink ExtraData requires an unimplemented resolver");
        }
        offset += length;
    }
    if (target.local_path.empty() && target.relative_path.empty())
    {
        throw UnsupportedLink("IDList-only or pathless ShellLink target");
    }
    return target;
}
} // namespace beachlife::compat::shell

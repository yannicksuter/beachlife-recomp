#include "tga.h"
#include "source.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace beachlife::data {
namespace {

constexpr std::uint64_t max_input_bytes = 128u * 1024u * 1024u;
constexpr std::uint64_t max_pixels = 16u * 1024u * 1024u;

class Reader {
public:
    explicit Reader(SourceFile& file) : file_(file), size_(file.size()) {
        if (size_ > max_input_bytes) throw std::runtime_error("TGA input exceeds 128 MiB.");
        file_.seek(0);
    }

    unsigned char byte() {
        if (position_ == available_) {
            const auto request = static_cast<std::size_t>(
                std::min<std::uint64_t>(buffer_.size(), size_ - fetched_));
            if (!request) throw std::runtime_error("TGA file is truncated.");
            available_ = file_.read(std::span(buffer_).first(request));
            if (!available_ || available_ > request)
                throw std::runtime_error("TGA read is incomplete or invalid.");
            fetched_ += available_;
            position_ = 0;
        }
        return buffer_[position_++];
    }

    std::uint32_t word() {
        const std::uint32_t low = byte();
        return low | (std::uint32_t(byte()) << 8);
    }

    std::uint32_t color(unsigned depth, bool alpha) {
        const std::uint32_t blue = byte(), green = byte(), red = byte();
        const std::uint32_t fourth = depth == 32 ? byte() : 255;
        return red | (green << 8) | (blue << 16) | ((alpha ? fourth : 255u) << 24);
    }

private:
    SourceFile& file_;
    std::uint64_t size_, fetched_ = 0;
    std::array<unsigned char, 16 * 1024> buffer_{};
    std::size_t position_ = 0, available_ = 0;
};

} // namespace

TgaImage read_tga(const FileSource& source, std::string_view logical_path) {
    auto file = source.open(logical_path);
    if (!file) throw std::runtime_error("Cannot open the TGA file.");
    Reader input(*file);
    const auto id_length = input.byte();
    const auto map_type = input.byte();
    const auto image_type = input.byte();
    const auto palette_first = input.word();
    const auto palette_length = input.word();
    const auto palette_depth = input.byte();
    // Placement on a larger canvas does not change this image's pixel order.
    (void)input.word(); // x origin
    (void)input.word(); // y origin
    const auto width = input.word(), height = input.word();
    const auto depth = input.byte(), descriptor = input.byte();

    const bool indexed = image_type == 1 || image_type == 9;
    const bool truecolor = image_type == 2 || image_type == 10;
    const bool rle = image_type == 9 || image_type == 10;
    if ((!indexed && !truecolor) || map_type != (indexed ? 1 : 0))
        throw std::runtime_error("Unsupported TGA image or color-map type.");
    if (descriptor & 0xc0) throw std::runtime_error("Interleaved TGA images are unsupported.");
    if (!width || !height || std::uint64_t(width) * height > max_pixels)
        throw std::runtime_error("TGA dimensions are empty or exceed 16M pixels.");

    const auto color_depth = indexed ? palette_depth : depth;
    const auto alpha_bits = descriptor & 0x0f;
    if ((color_depth != 24 && color_depth != 32) ||
        (alpha_bits != 0 && (alpha_bits != 8 || color_depth != 32)))
        throw std::runtime_error("Unsupported TGA color depth or alpha layout.");
    if (indexed) {
        if ((depth != 8 && depth != 16) || !palette_length ||
            palette_first + palette_length > (1u << depth))
            throw std::runtime_error("TGA palette range or index depth is invalid.");
    } else if (palette_first || palette_length || palette_depth) {
        throw std::runtime_error("TGA without a color map has palette metadata.");
    }
    for (unsigned i = 0; i < id_length; ++i) (void)input.byte();

    const bool alpha = alpha_bits == 8;
    std::vector<std::uint32_t> palette;
    if (indexed) {
        palette.reserve(palette_length);
        for (std::uint32_t i = 0; i < palette_length; ++i)
            palette.push_back(input.color(color_depth, alpha));
    }
    auto pixel = [&]() -> std::uint32_t {
        if (!indexed) return input.color(color_depth, alpha);
        const auto index = depth == 8 ? std::uint32_t(input.byte()) : input.word();
        if (index < palette_first || index - palette_first >= palette.size())
            throw std::runtime_error("TGA pixel refers outside its palette.");
        return palette[index - palette_first];
    };

    TgaImage image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    const auto count = static_cast<std::size_t>(std::uint64_t(width) * height);
    image.pixels.resize(count);
    const bool right_origin = (descriptor & 0x10) != 0;
    const bool top_origin = (descriptor & 0x20) != 0;
    std::size_t emitted = 0;
    auto emit = [&](std::uint32_t color) {
        const auto column = emitted % width, row = emitted / width;
        const auto x = right_origin ? width - 1 - column : column;
        const auto y = top_origin ? row : height - 1 - row;
        image.pixels[y * width + x] = color;
        ++emitted;
    };
    while (emitted < count) {
        const auto packet = rle ? input.byte() : 0;
        const auto length = rle ? std::size_t((packet & 0x7f) + 1) : count - emitted;
        if (length > count - emitted)
            throw std::runtime_error("TGA RLE packet exceeds the image dimensions.");
        if (rle && (packet & 0x80)) {
            const auto color = pixel();
            for (std::size_t i = 0; i < length; ++i) emit(color);
        } else {
            for (std::size_t i = 0; i < length; ++i) emit(pixel());
        }
    }
    return image;
}

} // namespace beachlife::data

#include "data/source.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

void expect(bool valid)
{
    if (!valid)
    {
        throw std::runtime_error("ISO source check failed.");
    }
}

template <class F> void rejects(F operation)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    expect(rejected);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    const std::filesystem::path image(argv[1]);
    std::unique_ptr<beachlife::data::SourceFile> first;
    {
        const beachlife::data::IsoSource source(image);
        // This test mutates only the generated fixture supplied by check_data.py.
        expect(source.index().volume == "TEST DISC" && source.index().image_bytes == 32 * 2048);
        first = source.open(".\\data\\Gr\xc3\xbc"
                            "sse.txt");
        auto second = source.open("BEACHLIFE.EXE");
        expect(first->size() == 5 && first->tell() == 0 && second->size() == 4);
        std::array<unsigned char, 8> buffer{};
        expect(
            first->read(std::span(buffer).first(2)) == 2 && buffer[0] == 'h' && buffer[1] == 'e');
        expect(first->tell() == 2 && second->tell() == 0);
        first->seek(4);
        expect(first->read(buffer) == 1 && buffer[0] == 'o' && first->tell() == 5);
        expect(first->read(buffer) == 0);
        rejects(
            [&]
            {
                first->seek(6);
            });
        rejects(
            [&]
            {
                first->seek(std::numeric_limits<std::uint64_t>::max());
            });
        expect(first->tell() == 5);
        first->seek(0);
        expect(first->read({}) == 0 && first->tell() == 0);
        rejects(
            [&]
            {
                source.open("missing");
            });
        rejects(
            [&]
            {
                source.open("Data");
            });
        rejects(
            [&]
            {
                source.open("Data/../BeachLife.exe");
            });
    }
    std::array<unsigned char, 8> buffer{};
    expect(first->read(buffer) == 5 && std::string(buffer.begin(), buffer.begin() + 5) == "hello");
    const beachlife::data::IsoSource source(image);
    auto truncated = source.open("Data/Gr\xc3\xbc"
                                 "sse.txt");
    std::filesystem::resize_file(image, 25 * 2048);
    rejects(
        [&]
        {
            truncated->read(buffer);
        });
    rejects(
        [&]
        {
            source.open("BeachLife.exe");
        });
    std::cout << "Read, seek, independent cursors, lifetime and truncation checks passed.\n";
}

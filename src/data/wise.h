#pragma once

#include "source.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace beachlife::data
{

// One file that a Wise installation script installs below %MAINDIR%.
struct WiseFile
{
    std::string path;         // below %MAINDIR%, as stored ('\' separators)
    std::uint64_t offset = 0; // of its raw-deflate stream in the installer
    std::uint32_t size = 0;   // inflated size
    std::uint32_t crc = 0;    // CRC-32 of the inflated bytes
};

struct WiseInstaller
{
    std::string installer; // the installer executable's path on the disc
    std::vector<WiseFile> files;
};

// Reads the installation script of a Wise installer executable without running
// it: the overlay after the PE image holds raw-deflate streams, each followed by
// the CRC-32 of its inflated bytes; one of the first streams is the script.
// Returns nullopt when the file is no Wise installer with installed files.
std::optional<WiseInstaller> index_wise_file(SourceFile& file, std::string name);

// The first Wise installer among the disc's root executables, if any.
std::optional<WiseInstaller> index_wise(const IsoSource& source);

// Streams one installed file, checking its size and both CRC-32 values.
void extract_wise(const IsoSource& source,
    const WiseInstaller& installer,
    const WiseFile& file,
    const std::function<void(std::span<const unsigned char>)>& sink);

} // namespace beachlife::data

#pragma once

#include "shlobj.h"

#include <span>
#include <stdexcept>
#include <string>

namespace beachlife::compat::shell
{
class UnsupportedLink : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};
class InvalidLink : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};
struct Target
{
    std::string local_path;    // UTF-8 from LinkInfo
    std::string relative_path; // UTF-8, relative to the .lnk's directory
    WIN32_FIND_DATAA metadata{};
};
// MS-SHLLINK bytes, never native structs or host wchar_t. Malformed data is a
// load failure; a valid variant requiring another resolver is unsupported.
Target parse(std::span<const unsigned char> bytes);
HRESULT create(REFIID iid, void** output);
} // namespace beachlife::compat::shell

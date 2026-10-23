#pragma once

#include "kernel_internal.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beachlife::compat::kernel
{
// Only relative retail paths and absolute paths inside the user root are
// accepted. No operation changes the process working directory.
std::filesystem::path user_root();
std::string logical_path(const char* path);
std::filesystem::path user_path(const char* path);
std::optional<std::vector<unsigned char>> read_file(const char* path);
bool write_file(const char* path, std::string_view bytes);
DWORD file_error(int error);
std::optional<DWORD> manifest_attributes(std::string_view path);
// Returns no value for another kernel handle type, false for a failed close.
std::optional<bool> close_file_handle(HANDLE handle);
std::optional<uint64_t> seek_file(int descriptor, int64_t offset, int origin);
int mapping_descriptor(HANDLE file, bool writable, uint64_t& size);
std::optional<bool> close_mapping_handle(HANDLE handle);
} // namespace beachlife::compat::kernel

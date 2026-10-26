#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

// Compiled without the retail SDK include directory. This boundary is the only
// part of the compat kernel that sees the host Windows SDK and native CRT I/O.
namespace beachlife::compat::native
{
FILE* open_file(const std::filesystem::path& path, const char* mode);
int open_descriptor(const std::filesystem::path& path, int flags);
int close_descriptor(int descriptor);
int duplicate_descriptor(int descriptor);
int resize_descriptor(int descriptor, uint64_t size);
int file_descriptor(FILE* file);
FILE* descriptor_stream(int descriptor, const char* mode);
int seek(FILE* file, int64_t offset, int origin);
int64_t tell(FILE* file);
int temporary_file(std::filesystem::path& path);
bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to);

#ifdef _WIN32
bool text_mode(FILE* file);
void* map(int descriptor,
    uint32_t protection,
    uint32_t access,
    uint64_t offset,
    std::size_t bytes,
    uint32_t& error);
bool unmap(const void* view, uint32_t& error);
void* create_mutex(const char* name, bool owner, uint32_t& error);
bool release_mutex(void* mutex, uint32_t& error);
void close_handle(void* handle);
#endif
} // namespace beachlife::compat::native

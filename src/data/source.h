#pragma once

#include "iso9660.h"

#include <map>
#include <memory>
#include <span>
#include <string_view>

namespace beachlife::data {

class SourceFile {
public:
    virtual ~SourceFile() = default;
    virtual std::uint64_t size() const = 0;
    virtual std::uint64_t tell() const = 0;
    virtual void seek(std::uint64_t position) = 0;
    virtual std::size_t read(std::span<unsigned char> destination) = 0;
};

class IsoSource {
public:
    explicit IsoSource(std::filesystem::path image);
    const IsoIndex& index() const { return index_; }
    std::unique_ptr<SourceFile> open(std::string_view logical_path) const;

private:
    std::filesystem::path image_;
    IsoIndex index_;
    std::map<std::string, std::size_t> lookup_;
};

} // namespace beachlife::data

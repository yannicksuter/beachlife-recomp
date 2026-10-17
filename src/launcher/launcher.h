#pragma once
#include "settings.h"

namespace beachlife::launcher {

struct Options {
    std::filesystem::path config;
    std::filesystem::path screenshot;
    std::string source;
    SourceKind kind = SourceKind::Iso;
    int page = 0;
    bool smoke = false;
};

int run(const Options& options);

} // namespace beachlife::launcher

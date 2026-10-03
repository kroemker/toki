#pragma once

#include <filesystem>
#include <string>

namespace cparser {

/// One file's bytes as they were read. Every `Token` points at the file it came
/// from and views into its `contents`, so a file never changes or moves once a
/// `SourceSet` has handed it out.
struct SourceFile {
    std::filesystem::path path;
    std::string contents;
};

} // namespace cparser

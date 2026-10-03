#pragma once

#include "base/Diagnostic.h"
#include "cparser/SourceFile.h"
#include "cparser/Types.h"

#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cparser {

/// Owns every file one reading pass opened, keyed by canonical path so a file
/// opened twice is read once. Tokens, macros and declarations all borrow from
/// the files, so the set must outlive everything that came out of it.
///
/// A set is a snapshot, never refreshed: a file edited on disk is seen by the
/// next set, not this one. That is what lets a reload replace a whole unit
/// while the previous one is still being looked at. Moving the set is safe;
/// every file lives on the heap behind it.
class SourceSet {
public:
    [[nodiscard]] std::expected<const SourceFile*, base::Diagnostic>
    open(const std::filesystem::path& path);

    /// Takes a file that exists only in memory, such as the definitions a
    /// build passes on the command line. A file already held under the same
    /// path is kept, and the new contents are dropped.
    const SourceFile* add(const std::filesystem::path& path, std::string contents);

    [[nodiscard]] const SourceFile* find(const std::filesystem::path& path) const;

    /// Every file held, in the order it was first opened.
    [[nodiscard]] std::vector<const SourceFile*> getFiles() const;

    [[nodiscard]] usize count() const {
        return files.size();
    }

private:
    std::vector<std::unique_ptr<SourceFile>> files;
    std::map<std::filesystem::path, const SourceFile*> byPath;
};

[[nodiscard]] std::filesystem::path canonicalize(const std::filesystem::path& path);

[[nodiscard]] bool isInside(const std::filesystem::path& path, const std::filesystem::path& root);

} // namespace cparser

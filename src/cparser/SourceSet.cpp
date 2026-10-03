#include "SourceSet.h"

#include "base/Diagnostic.h"
#include "cparser/SourceFile.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace cparser {

std::filesystem::path canonicalize(const std::filesystem::path& path) {
    std::error_code failure;
    const std::filesystem::path absolute = std::filesystem::absolute(path, failure);
    return (failure ? path : absolute).lexically_normal();
}

bool isInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

std::expected<const SourceFile*, base::Diagnostic>
SourceSet::open(const std::filesystem::path& path) {
    const std::filesystem::path key = canonicalize(path);
    if (const SourceFile* found = lookup(key)) {
        return found;
    }

    std::ifstream stream(key, std::ios::binary);
    if (!stream) {
        return std::unexpected(
            base::error(base::SourceLocation{.file = path}, "cannot open the file for reading"));
    }

    auto file = std::make_unique<SourceFile>();
    file->path = key;
    file->contents.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());

    const SourceFile* opened = file.get();
    files.push_back(std::move(file));
    index(key, opened);
    return opened;
}

const SourceFile* SourceSet::add(const std::filesystem::path& path, std::string contents) {
    const std::filesystem::path key = canonicalize(path);
    if (const SourceFile* found = lookup(key)) {
        return found;
    }

    auto file = std::make_unique<SourceFile>();
    file->path = key;
    file->contents = std::move(contents);

    const SourceFile* added = file.get();
    files.push_back(std::move(file));
    index(key, added);
    return added;
}

std::vector<const SourceFile*> SourceSet::getFiles() const {
    std::vector<const SourceFile*> held;
    held.reserve(files.size());
    for (const std::unique_ptr<SourceFile>& file : files) {
        held.push_back(file.get());
    }
    return held;
}

const SourceFile* SourceSet::find(const std::filesystem::path& path) const {
    return lookup(canonicalize(path));
}

const SourceFile* SourceSet::lookup(const std::filesystem::path& key) const {
    const auto found = std::ranges::lower_bound(byPath, key, {}, &Indexed::path);
    return found != byPath.end() && found->path == key ? found->file : nullptr;
}

void SourceSet::index(const std::filesystem::path& key, const SourceFile* file) {
    const auto at = std::ranges::lower_bound(byPath, key, {}, &Indexed::path);
    byPath.insert(at, Indexed{.path = key, .file = file});
}

} // namespace cparser

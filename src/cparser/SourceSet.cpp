#include "SourceSet.h"

#include "base/Diagnostic.h"
#include "cparser/SourceFile.h"

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
    if (const auto found = byPath.find(key); found != byPath.end()) {
        return found->second;
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
    byPath.emplace(key, opened);
    return opened;
}

const SourceFile* SourceSet::add(const std::filesystem::path& path, std::string contents) {
    const std::filesystem::path key = canonicalize(path);
    if (const auto found = byPath.find(key); found != byPath.end()) {
        return found->second;
    }

    auto file = std::make_unique<SourceFile>();
    file->path = key;
    file->contents = std::move(contents);

    const SourceFile* added = file.get();
    files.push_back(std::move(file));
    byPath.emplace(key, added);
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
    const auto found = byPath.find(canonicalize(path));
    return found == byPath.end() ? nullptr : found->second;
}

} // namespace cparser

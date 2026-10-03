#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cparser {

/// Answers questions about files from one listing per directory, taken the
/// first time the directory is asked about and kept. A checkout on a network
/// share charges milliseconds per filesystem query, and reading one scene asks
/// hundreds; one listing per directory makes that a handful.
///
/// A cache is a snapshot, like the `SourceSet` it serves: a file created
/// after its directory was listed is not seen.
class DirectoryCache {
public:
    /// The names in `directory`, sorted. Empty when it cannot be listed.
    [[nodiscard]] const std::vector<std::string>& listing(const std::filesystem::path& directory);

    [[nodiscard]] bool holds(const std::filesystem::path& file);

    /// The first name in `directory` that starts with `prefix` and ends with
    /// `suffix`, or nothing.
    [[nodiscard]] std::string startingWith(const std::filesystem::path& directory,
                                           std::string_view prefix, std::string_view suffix);

private:
    std::map<std::filesystem::path, std::vector<std::string>> listings;
};

} // namespace cparser

#include "DirectoryCache.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cparser {

const std::vector<std::string>& DirectoryCache::listing(const std::filesystem::path& directory) {
    const auto found = listings.find(directory);
    if (found != listings.end()) {
        return found->second;
    }

    std::vector<std::string> names;
    std::error_code failure;
    for (std::filesystem::directory_iterator entry(directory, failure);
         !failure && entry != std::filesystem::directory_iterator(); entry.increment(failure)) {
        names.push_back(entry->path().filename().string());
    }
    std::ranges::sort(names);
    return listings.emplace(directory, std::move(names)).first->second;
}

bool DirectoryCache::holds(const std::filesystem::path& file) {
    const std::vector<std::string>& names = listing(file.parent_path());
    return std::ranges::binary_search(names, file.filename().string());
}

std::string DirectoryCache::startingWith(const std::filesystem::path& directory,
                                         std::string_view prefix, std::string_view suffix) {
    const std::vector<std::string>& names = listing(directory);
    for (auto candidate = std::ranges::lower_bound(names, prefix);
         candidate != names.end() && std::string_view(*candidate).starts_with(prefix);
         ++candidate) {
        if (candidate->ends_with(suffix)) {
            return *candidate;
        }
    }
    return {};
}

} // namespace cparser

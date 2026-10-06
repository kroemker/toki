#include "ObjectFiles.h"

#include "base/Diagnostic.h"
#include "hackeroot/Assets.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <vector>

namespace hackeroot {

std::vector<std::filesystem::path> ObjectFiles::getAllFiles() const {
    std::vector<std::filesystem::path> all = files;
    all.insert(all.end(), headers.begin(), headers.end());
    return all;
}

ObjectFiles findObjectFiles(const AssetDirectory& asset) {
    ObjectFiles found;
    std::error_code failure;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(asset.directory, failure)) {
        const std::string name = entry.path().filename().string();
        if (name.ends_with(".h")) {
            found.headers.push_back(entry.path());
        } else if (name.ends_with(".c") && !name.ends_with(".inc.c")) {
            found.files.push_back(entry.path());
        }
    }
    if (failure) {
        found.diagnostics.push_back(base::makeError(
            {.file = asset.directory},
            std::format("the object folder could not be listed: {}", failure.message())));
        return found;
    }
    std::ranges::sort(found.files);
    std::ranges::sort(found.headers);
    if (found.files.empty()) {
        found.diagnostics.push_back(
            base::makeError({.file = asset.directory},
                            std::format("no C file in the object folder {}", asset.name)));
    }
    return found;
}

} // namespace hackeroot

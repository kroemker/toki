#include "FilePoller.h"

#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

[[nodiscard]] std::optional<std::filesystem::file_time_type>
findModificationTime(const std::filesystem::path& path) {
    std::error_code failure;
    const std::filesystem::file_time_type modified =
        std::filesystem::last_write_time(path, failure);
    if (failure) {
        return std::nullopt;
    }
    return modified;
}

} // namespace

FilePoller::FilePoller(std::vector<std::filesystem::path> files) {
    watched.reserve(files.size());
    for (std::filesystem::path& file : files) {
        const std::optional<std::filesystem::file_time_type> modified = findModificationTime(file);
        watched.push_back(Watched{.path = std::move(file), .modified = modified});
    }
}

std::vector<std::filesystem::path> FilePoller::findChanges() {
    std::vector<std::filesystem::path> changed;
    for (Watched& file : watched) {
        const std::optional<std::filesystem::file_time_type> modified =
            findModificationTime(file.path);
        if (modified != file.modified) {
            file.modified = modified;
            changed.push_back(file.path);
        }
    }
    return changed;
}

} // namespace hackeroot

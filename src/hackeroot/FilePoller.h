#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace hackeroot {

/// Remembers when each of a set of files last changed, and reports which ones
/// changed since it last looked. Windows delivers no change notifications for a
/// checkout on `\\wsl.localhost`, so asking is the only way to find out; one
/// question costs about a millisecond there.
///
/// A file that does not exist is watched too: creating it is a change, and so
/// is deleting a file that existed.
class FilePoller {
public:
    explicit FilePoller(std::vector<std::filesystem::path> files);

    /// The files whose modification time or existence changed since the
    /// previous call, or since construction for the first one.
    [[nodiscard]] std::vector<std::filesystem::path> findChanges();

private:
    struct Watched {
        std::filesystem::path path;
        std::optional<std::filesystem::file_time_type> modified;
    };

    std::vector<Watched> watched;
};

} // namespace hackeroot

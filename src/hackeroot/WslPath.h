#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace hackeroot {

/// A path inside a WSL distribution, as a Linux process in it sees the path.
struct WslPath {
    std::string distribution;
    std::string linuxPath;
};

/// Where a Windows path into a WSL share points: `\\wsl.localhost\Ubuntu\home\x`
/// and the older `\\wsl$\Ubuntu\home\x` are both `/home/x` in `Ubuntu`. Either
/// slash direction is accepted, and the prefix in any case. Nothing for a path
/// that is not on a WSL share.
[[nodiscard]] std::optional<WslPath> findWslPath(const std::filesystem::path& path);

} // namespace hackeroot

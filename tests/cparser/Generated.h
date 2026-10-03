#pragma once

#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"

#include <filesystem>
#include <optional>

/// Embeds every missing `.inc.c`, tagged "generated": a stand-in for a real
/// codebase's rule about includes its build generates, so the tests exercise
/// the mechanism without depending on any one codebase's naming.
[[nodiscard]] inline std::optional<cparser::ResolvedInclude>
embedGenerated(const std::filesystem::path& candidate, cparser::DirectoryCache& /*directories*/) {
    if (!candidate.filename().string().ends_with(".inc.c")) {
        return std::nullopt;
    }
    return cparser::ResolvedInclude{.path = candidate, .embedded = true, .tag = "generated"};
}

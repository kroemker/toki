#include "TextureIncludes.h"

#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"
#include "z64/Types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace z64 {

namespace {

constexpr std::string_view GeneratedSuffix = ".inc.c";

} // namespace

std::optional<cparser::ResolvedInclude>
resolveTextureInclude(const std::filesystem::path& candidate,
                      cparser::DirectoryCache& directories) {
    const std::string name = candidate.filename().string();
    if (!name.ends_with(GeneratedSuffix)) {
        return std::nullopt;
    }
    const std::string stem = name.substr(0, name.size() - GeneratedSuffix.size());
    const usize dot = stem.rfind('.');
    if (dot == std::string::npos) {
        return std::nullopt;
    }
    const std::string format = stem.substr(dot + 1);
    const std::filesystem::path directory = candidate.parent_path();

    if (format == "bin") {
        if (!directories.holds(directory / stem)) {
            return std::nullopt;
        }
        return cparser::ResolvedInclude{.path = directory / stem, .embedded = true, .tag = format};
    }

    const std::string image = directories.startingWith(directory, stem + ".", ".png");
    if (image.empty()) {
        return std::nullopt;
    }
    return cparser::ResolvedInclude{.path = directory / image, .embedded = true, .tag = format};
}

} // namespace z64

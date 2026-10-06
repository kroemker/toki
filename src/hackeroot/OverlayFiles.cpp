#include "OverlayFiles.h"

#include "base/Diagnostic.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/AssetSource.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hackeroot {

std::vector<std::filesystem::path> OverlayFiles::getAllFiles() const {
    std::vector<std::filesystem::path> files{main};
    files.insert(files.end(), headers.begin(), headers.end());
    return files;
}

std::expected<OverlayFiles, base::Diagnostic>
findOverlayFiles(const std::filesystem::path& repository, const std::string_view name) {
    OverlayFiles overlay{.directory = repository / "src" / "overlays" / "actors" /
                                      std::format("ovl_{}", name)};
    std::string lower(name);
    std::ranges::transform(lower, lower.begin(), [](const unsigned char letter) {
        return static_cast<char>(std::tolower(letter));
    });
    overlay.main = overlay.directory / std::format("z_{}.c", lower);
    std::error_code failure;
    if (!std::filesystem::is_regular_file(overlay.main, failure)) {
        return std::unexpected(
            base::makeError({.file = overlay.main}, std::format("actor '{}' has no overlay at {}",
                                                                name, overlay.main.string())));
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(overlay.directory, failure)) {
        if (entry.path().extension() == ".h") {
            overlay.headers.push_back(entry.path());
        }
    }
    std::ranges::sort(overlay.headers);
    return overlay;
}

std::expected<cparser::TranslationUnit, base::Diagnostic>
preprocessOverlay(const Workspace& workspace, const OverlayFiles& overlay) {
    cparser::PreprocessOptions options =
        makeAssetPreprocessOptions(workspace.getRoot(), workspace.getBuildDefines().version);
    options.allowedRoots.push_back(overlay.directory);
    return cparser::preprocess(workspace.getMacros(), overlay.main, options);
}

} // namespace hackeroot

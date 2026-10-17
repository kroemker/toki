#include "AssetSource.h"

#include "base/Diagnostic.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"
#include "z64/TextureIncludes.h"

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace hackeroot {

std::vector<std::filesystem::path> makeBuildIncludePaths(const std::filesystem::path& repository,
                                                        const std::string_view version) {
    return {repository / "include",         repository / "include" / "libc",
            repository / "src",             repository / "build" / version,
            repository,                     repository / "extracted" / version};
}

cparser::PreprocessOptions makeAssetPreprocessOptions(const std::filesystem::path& repository,
                                                      const std::string_view version) {
    const std::filesystem::path extracted = repository / "extracted" / version;
    return cparser::PreprocessOptions{
        .searchPaths = makeBuildIncludePaths(repository, version),
        .allowedRoots = {repository / "assets", extracted / "assets"},
        .resolveMissingInclude = z64::resolveTextureInclude,
        .dialect = &z64::getDecompDialect(),
    };
}

std::expected<cparser::TranslationUnit, base::Diagnostic>
preprocessAsset(const Workspace& workspace, const std::filesystem::path& file) {
    return cparser::preprocess(
        workspace.getMacros(), file,
        makeAssetPreprocessOptions(workspace.getRoot(), workspace.getBuildDefines().version));
}

} // namespace hackeroot

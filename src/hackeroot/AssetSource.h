#pragma once

#include "base/Diagnostic.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace hackeroot {

/// The directories the build's `cpp` searches for an include, in the order of
/// the Makefile's `-I` arguments.
[[nodiscard]] std::vector<std::filesystem::path>
makeBuildIncludePaths(const std::filesystem::path& repository, std::string_view version);

/// How a checkout's asset C is preprocessed: includes searched for where the
/// build searches, followed only into the two `assets/` trees, generated texture
/// includes resolved to the images they are generated from, and conditions
/// evaluated in the decomp's dialect.
[[nodiscard]] cparser::PreprocessOptions
makeAssetPreprocessOptions(const std::filesystem::path& repository, std::string_view version);

/// Preprocesses one asset file of the workspace's checkout against its build
/// configuration.
[[nodiscard]] std::expected<cparser::TranslationUnit, base::Diagnostic>
preprocessAsset(const Workspace& workspace, const std::filesystem::path& file);

} // namespace hackeroot

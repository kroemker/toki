#pragma once

#include "base/Diagnostic.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <filesystem>
#include <string_view>

namespace hackeroot {

/// How a checkout's asset C is preprocessed: includes spelled from the
/// repository root or from `extracted/<version>`, as the Makefile passes them
/// to `cpp`, followed only into the two `assets/` trees, generated texture
/// includes resolved to the images they are generated from, and conditions
/// evaluated in the decomp's dialect.
[[nodiscard]] cparser::PreprocessOptions
makeAssetPreprocessOptions(const std::filesystem::path& repository, std::string_view version);

/// Preprocesses one asset file of the workspace's checkout against its build
/// configuration.
[[nodiscard]] std::expected<cparser::TranslationUnit, base::Diagnostic>
preprocessAsset(const Workspace& workspace, const std::filesystem::path& file);

} // namespace hackeroot

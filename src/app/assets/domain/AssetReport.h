#pragma once

#include "app/assets/Types.h"
#include "base/Diagnostic.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace toki::assets {

/// What reading one source file of an asset found.
struct FileReport {
    std::filesystem::path file;
    usize declarations = 0;
    usize images = 0;
    std::vector<base::Diagnostic> diagnostics;
};

/// What opening an asset folder found, file by file. Until there is a
/// renderer to hand it to, this is where opening an asset ends, and every
/// parser that is added widens what it reports.
struct AssetReport {
    std::string name;
    std::filesystem::path directory;
    std::vector<FileReport> files;
    std::chrono::milliseconds elapsed{0};
};

struct ReportCounts {
    usize files = 0;
    usize declarations = 0;
    usize images = 0;
    usize errors = 0;
    usize warnings = 0;
    usize notes = 0;
};

/// The C files an asset folder builds from, sorted: every `.c` directly in
/// it, leaving out the `.inc.c` fragments those include.
[[nodiscard]] std::vector<std::filesystem::path>
listAssetSources(const std::filesystem::path& directory);

/// Preprocesses and parses every source of `asset` against the workspace's
/// build configuration.
[[nodiscard]] AssetReport readAssetReport(const hackeroot::Workspace& workspace,
                                          const hackeroot::AssetDirectory& asset);

[[nodiscard]] ReportCounts countReport(const AssetReport& report);

} // namespace toki::assets

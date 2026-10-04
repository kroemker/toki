#include "AssetReport.h"

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/AssetSource.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace toki::assets {

namespace {

FileReport readFile(const hackeroot::Workspace& workspace, const std::filesystem::path& file) {
    FileReport report{.file = file};
    const std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        hackeroot::preprocessAsset(workspace, file);
    if (!unit) {
        report.diagnostics.push_back(unit.error());
        return report;
    }
    report.images = unit->embedded.size();
    report.diagnostics = unit->diagnostics;
    const cparser::ParseResult parsed = cparser::parseDeclarations(
        *unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});
    report.declarations = parsed.declarations.size();
    report.diagnostics.insert(report.diagnostics.end(), parsed.diagnostics.begin(),
                              parsed.diagnostics.end());
    return report;
}

} // namespace

std::vector<std::filesystem::path> listAssetSources(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> sources;
    std::error_code failure;
    for (std::filesystem::directory_iterator entry(directory, failure);
         !failure && entry != std::filesystem::directory_iterator(); entry.increment(failure)) {
        const std::filesystem::path& path = entry->path();
        const std::string name = path.filename().string();
        if (path.extension() == ".c" && !name.ends_with(".inc.c")) {
            sources.push_back(path);
        }
    }
    std::ranges::sort(sources);
    return sources;
}

AssetReport readAssetReport(const hackeroot::Workspace& workspace,
                            const hackeroot::AssetDirectory& asset) {
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    AssetReport report{.name = asset.name, .directory = asset.directory};
    for (const std::filesystem::path& source : listAssetSources(asset.directory)) {
        report.files.push_back(readFile(workspace, source));
    }
    report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return report;
}

ReportCounts countReport(const AssetReport& report) {
    ReportCounts counts{.files = report.files.size()};
    for (const FileReport& file : report.files) {
        counts.declarations += file.declarations;
        counts.images += file.images;
        for (const base::Diagnostic& diagnostic : file.diagnostics) {
            switch (diagnostic.severity) {
            case base::Severity::Error:
                ++counts.errors;
                break;
            case base::Severity::Warning:
                ++counts.warnings;
                break;
            case base::Severity::Note:
                ++counts.notes;
                break;
            }
        }
    }
    return counts;
}

} // namespace toki::assets

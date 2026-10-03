#include "Workspace.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Scrape.h"
#include "cparser/SourceFile.h"
#include "hackeroot/Assets.h"
#include "hackeroot/BuildDefines.h"
#include "hackeroot/Configuration.h"
#include "hackeroot/Tables.h"
#include "makeparser/Makefile.h"

#include <array>
#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::array MakeFiles = {std::string_view("Makefile"),
                                  std::string_view(".make_options.mk"),
                                  std::string_view(".make_hackeroot.mk")};

constexpr std::array TableFiles = {std::string_view("include/tables/scene_table.h"),
                                   std::string_view("include/tables/actor_table.h"),
                                   std::string_view("include/tables/object_table.h"),
                                   std::string_view("include/tables/entrance_table.h")};

} // namespace

std::expected<Workspace, base::Diagnostic>
Workspace::load(const std::filesystem::path& root, const makeparser::MakeOverrides& settings) {
    std::expected<BuildDefines, base::Diagnostic> defines = readBuildDefines(root, settings);
    if (!defines) {
        return std::unexpected(std::move(defines.error()));
    }

    Workspace workspace;
    workspace.root = root;
    workspace.buildDefines = std::move(*defines);
    workspace.scraped =
        std::make_unique<cparser::ScrapeResult>(cparser::scrapeDefinitions(root / "include"));
    workspace.configuration = std::make_unique<Configuration>(
        readConfiguration(workspace.scraped->macros, root, workspace.buildDefines.arguments));
    workspace.tables = readTables(workspace.configuration->macros, root);
    workspace.assets = listAssets(root, workspace.buildDefines.version);
    return workspace;
}

std::vector<base::Diagnostic> Workspace::collectDiagnostics() const {
    std::vector<base::Diagnostic> all;
    const std::array<const std::vector<base::Diagnostic>*, 5> parts = {
        &buildDefines.diagnostics, &scraped->diagnostics, &configuration->diagnostics,
        &tables.diagnostics, &assets.diagnostics};
    for (const std::vector<base::Diagnostic>* part : parts) {
        all.insert(all.end(), part->begin(), part->end());
    }
    return all;
}

const cparser::MacroTable& Workspace::getMacros() const {
    return configuration->macros;
}

std::vector<std::filesystem::path> Workspace::listWatchedFiles() const {
    const std::vector<const cparser::SourceFile*> sources = configuration->sources.getFiles();
    std::vector<std::filesystem::path> files;
    files.reserve(MakeFiles.size() + sources.size() + TableFiles.size());
    for (const std::string_view file : MakeFiles) {
        files.push_back(root / file);
    }
    for (const cparser::SourceFile* source : sources) {
        files.push_back(source->path);
    }
    for (const std::string_view file : TableFiles) {
        files.push_back(root / file);
    }
    return files;
}

} // namespace hackeroot

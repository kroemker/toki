#include "app/assets/domain/AssetReport.h"

#include "app/assets/domain/AssetReader.h"
#include "app/assets/domain/AssetRows.h"
#include "app/assets/domain/SceneNames.h"
#include "base/Diagnostic.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::filesystem::path getCheckout() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

std::shared_ptr<const hackeroot::Workspace> loadCheckout() {
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(getCheckout(), {});
    REQUIRE(workspace.has_value());
    return std::make_shared<const hackeroot::Workspace>(std::move(*workspace));
}

const hackeroot::AssetDirectory& findScene(const hackeroot::Workspace& workspace,
                                           const std::string_view name) {
    const std::vector<hackeroot::AssetDirectory>& scenes = workspace.getAssets().scenes;
    const auto found = std::ranges::find(scenes, name, &hackeroot::AssetDirectory::name);
    REQUIRE(found != scenes.end());
    return *found;
}

} // namespace

TEST_CASE("an asset builds from its C files, not the fragments they include", "[assets]") {
    const std::filesystem::path spot00 =
        getCheckout() / "extracted/ntsc-1.2/assets/scenes/overworld/spot00";

    const std::vector<std::filesystem::path> sources = toki::assets::listAssetSources(spot00);

    REQUIRE(sources.size() == 2);
    REQUIRE(sources[0].filename() == "spot00_room_0.c");
    REQUIRE(sources[1].filename() == "spot00_scene.c");
}

TEST_CASE("opening a scene reports its files, declarations, images and diagnostics", "[assets]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();

    const toki::assets::AssetReport report =
        toki::assets::readAssetReport(*workspace, findScene(*workspace, "spot00"));

    REQUIRE(report.name == "spot00");
    const toki::assets::ReportCounts counts = toki::assets::countReport(report);
    REQUIRE(counts.files == 2);
    REQUIRE(counts.declarations == 3);
    REQUIRE(counts.images == 1);
    REQUIRE(report.files[0].images == 1);
    REQUIRE(
        std::ranges::any_of(report.files[0].diagnostics, [](const base::Diagnostic& diagnostic) {
            return diagnostic.message.contains("missing.h");
        }));
    REQUIRE(report.files[1].diagnostics.empty());
}

TEST_CASE("an asset is read off the calling thread", "[assets]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();
    toki::assets::AssetReader reader;

    reader.open(workspace, findScene(*workspace, "spot00"));

    std::optional<toki::assets::AssetReport> report;
    for (int attempt = 0; attempt < 500 && !report; ++attempt) {
        report = reader.update();
        if (!report) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    REQUIRE(report.has_value());
    REQUIRE(report->files.size() == 2);
    REQUIRE_FALSE(reader.findReadingName().has_value());
    const std::optional<toki::assets::ReportCounts> counts =
        reader.findReportCounts(findScene(*workspace, "spot00").directory);
    REQUIRE(counts.has_value());
    REQUIRE(counts->files == 2);
}

TEST_CASE("scene enums read as names", "[assets][names]") {
    REQUIRE(toki::assets::makeReadableSceneName("SCENE_HYRULE_FIELD") == "Hyrule Field");
    REQUIRE(toki::assets::makeReadableSceneName("SCENE_DEKU_TREE_BOSS") == "Deku Tree Boss");
    REQUIRE(toki::assets::makeReadableSceneName("TESTMAP") == "Testmap");
}

TEST_CASE("a scene folder finds its table entry and category", "[assets][names]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();

    const hackeroot::SceneEntry* custom =
        toki::assets::findSceneEntry(workspace->getTables(), "custom");

    REQUIRE(custom != nullptr);
    REQUIRE(custom->enumName == "SCENE_CUSTOM");
    REQUIRE(toki::assets::findSceneEntry(workspace->getTables(), "spot00") == nullptr);
    REQUIRE(toki::assets::getAssetCategory(findScene(*workspace, "spot00")) == "overworld");
}

TEST_CASE("a scene directly under scenes/ is in no category of its own", "[assets][names]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();

    REQUIRE(toki::assets::getAssetCategory(findScene(*workspace, "custom")) == "other");
}

TEST_CASE("scene rows join the folders with the scene and entrance tables", "[assets][rows]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();

    const std::vector<toki::assets::SceneRow> rows = toki::assets::sortSceneRows(
        toki::assets::makeSceneRows(*workspace, ""), toki::assets::AssetColumn::Name, true);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].name == "Custom");
    REQUIRE(rows[0].category == "other");
    REQUIRE(rows[0].entry != nullptr);
    REQUIRE(rows[0].entrances == 1);
    REQUIRE(rows[1].name == "spot00");
    REQUIRE(rows[1].entry == nullptr);
    REQUIRE(rows[1].asset->roomCount == 1);
}

TEST_CASE("the filter matches the enum as well as the names", "[assets][rows]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();

    const std::vector<toki::assets::SceneRow> rows =
        toki::assets::makeSceneRows(*workspace, "scene_cus");

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].asset->name == "custom");
}

TEST_CASE("object rows sort by ID with unlisted folders last", "[assets][rows]") {
    const std::shared_ptr<const hackeroot::Workspace> workspace = loadCheckout();

    const std::vector<toki::assets::ObjectRow> rows = toki::assets::sortObjectRows(
        toki::assets::makeObjectRows(*workspace, ""), toki::assets::AssetColumn::Id, true);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].asset->name == "gameplay_keep");
    REQUIRE(rows[0].entry != nullptr);
    REQUIRE(rows[0].entry->id == 1);
    REQUIRE(rows[1].asset->name == "object_custom");
    REQUIRE(rows[1].entry == nullptr);

    const std::vector<toki::assets::ObjectRow> descending = toki::assets::sortObjectRows(
        toki::assets::makeObjectRows(*workspace, ""), toki::assets::AssetColumn::Id, false);
    REQUIRE(descending[0].asset->name == "object_custom");
}

TEST_CASE("every scene of a real checkout opens", "[assets][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    toki::assets::ReportCounts total;
    for (const hackeroot::AssetDirectory& scene : workspace->getAssets().scenes) {
        const toki::assets::AssetReport report = toki::assets::readAssetReport(*workspace, scene);
        const toki::assets::ReportCounts counts = toki::assets::countReport(report);
        INFO(scene.name);
        REQUIRE(counts.files > 0);
        REQUIRE(scene.roomCount > 0);
        total.files += counts.files;
        total.declarations += counts.declarations;
        total.images += counts.images;
        total.errors += counts.errors;
        total.warnings += counts.warnings;
        total.notes += counts.notes;
        for (const toki::assets::FileReport& file : report.files) {
            for (const base::Diagnostic& diagnostic : file.diagnostics) {
                if (diagnostic.severity != base::Severity::Note) {
                    UNSCOPED_INFO(base::describe(diagnostic));
                }
            }
        }
    }
    WARN(workspace->getAssets().scenes.size()
         << " scenes: " << total.files << " files, " << total.declarations << " declarations, "
         << total.images << " images; " << total.errors << " errors, " << total.warnings
         << " warnings, " << total.notes << " notes");
    REQUIRE(total.errors == 0);
    REQUIRE(total.warnings == 0);
}

#include "hackeroot/Workspace.h"

#include "base/Diagnostic.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"
#include "makeparser/Makefile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path getCheckout() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

[[nodiscard]] hackeroot::Workspace loadCheckout(const makeparser::MakeOverrides& settings = {}) {
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(getCheckout(), settings);
    REQUIRE(workspace.has_value());
    return std::move(*workspace);
}

[[nodiscard]] std::vector<std::string>
collectNames(const std::vector<hackeroot::AssetDirectory>& directories) {
    std::vector<std::string> names;
    names.reserve(directories.size());
    for (const hackeroot::AssetDirectory& directory : directories) {
        names.push_back(directory.name + (directory.isExtracted ? " (extracted)" : ""));
    }
    return names;
}

[[nodiscard]] bool watches(const hackeroot::Workspace& workspace, std::string_view file) {
    const std::vector<std::filesystem::path> watched = workspace.listWatchedFiles();
    return std::ranges::any_of(watched, [file](const std::filesystem::path& path) {
        return path.generic_string().ends_with(file);
    });
}

} // namespace

TEST_CASE("the tables carry the IDs the build gives them", "[hackeroot][workspace]") {
    const hackeroot::Workspace workspace = loadCheckout();
    const hackeroot::Tables& tables = workspace.getTables();

    REQUIRE(tables.scenes.size() == 3);
    REQUIRE(tables.scenes[1].name == "example_scene");
    REQUIRE(tables.scenes[1].enumName == "SCENE_EXAMPLE");
    REQUIRE(tables.scenes[1].titleCard == "none");

    REQUIRE(tables.actors.size() == 3);
    REQUIRE(tables.actors[0].name == "Player");
    REQUIRE(tables.actors[1].isUnset);
    REQUIRE(tables.actors[1].enumName == "ACTOR_UNSET_1");
    REQUIRE(tables.actors[2].enumName == "ACTOR_EN_BOX");
    REQUIRE(tables.actors[2].id == 2);

    REQUIRE(tables.objects.size() == 3);
    REQUIRE(tables.objects[0].isUnset);
    REQUIRE(tables.objects[2].name == "object_box");
}

TEST_CASE("an entry with too few arguments keeps its ID and is reported",
          "[hackeroot][workspace]") {
    const hackeroot::Workspace workspace = loadCheckout();
    const std::vector<hackeroot::EntranceEntry>& entrances = workspace.getTables().entrances;

    REQUIRE(entrances.size() == 3);
    REQUIRE(entrances[1].enumName == "ENTR_BROKEN");
    REQUIRE(entrances[1].sceneEnumName.empty());
    REQUIRE(entrances[2].enumName == "ENTR_CUSTOM_0");
    REQUIRE(entrances[2].id == 2);

    const std::vector<base::Diagnostic> diagnostics = workspace.collectDiagnostics();
    REQUIRE(std::ranges::any_of(diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.severity == base::Severity::Warning &&
               diagnostic.message.contains("DEFINE_ENTRANCE has 1 arguments");
    }));
}

TEST_CASE("the build settings decide which entries exist", "[hackeroot][workspace]") {
    const hackeroot::Workspace workspace = loadCheckout({{"VERSION", "gc-eu-mq-dbg"}});

    REQUIRE(workspace.getBuildDefines().version == "gc-eu-mq-dbg");
    REQUIRE(workspace.getTables().scenes.size() == 4);
    REQUIRE(workspace.getTables().actors.size() == 4);
    REQUIRE(workspace.getTables().actors[3].id == 3);
}

TEST_CASE("asset folders come from the checkout and from its extracted version",
          "[hackeroot][workspace]") {
    const hackeroot::Workspace workspace = loadCheckout();

    REQUIRE(collectNames(workspace.getAssets().scenes) ==
            std::vector<std::string>{"custom", "spot00 (extracted)"});
    REQUIRE(collectNames(workspace.getAssets().objects) ==
            std::vector<std::string>{"gameplay_keep (extracted)", "object_custom"});
}

TEST_CASE("a version that was never extracted is a warning, not a failure",
          "[hackeroot][workspace]") {
    const hackeroot::Assets assets = hackeroot::listAssets(getCheckout(), "pal-1.0");

    REQUIRE(collectNames(assets.scenes) == std::vector<std::string>{"custom"});
    REQUIRE(assets.diagnostics.size() == 1);
    REQUIRE(assets.diagnostics.front().message.contains("pal-1.0"));
}

TEST_CASE("the workspace watches what its configuration and tables were read from",
          "[hackeroot][workspace]") {
    const hackeroot::Workspace workspace = loadCheckout();

    REQUIRE(watches(workspace, "/Makefile"));
    REQUIRE(watches(workspace, "/.make_options.mk"));
    REQUIRE(watches(workspace, "/include/config.h"));
    REQUIRE(watches(workspace, "/include/config/config_debug.h"));
    REQUIRE(watches(workspace, "/include/tables/entrance_table.h"));
}

TEST_CASE("a folder without a Makefile is not a workspace", "[hackeroot][workspace]") {
    REQUIRE_FALSE(hackeroot::Workspace::load(getCheckout() / "include", {}).has_value());
}

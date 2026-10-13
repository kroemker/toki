#include "app/actor/domain/ItemModels.h"
#include "app/actor/domain/Pass.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

using base::usize;

TEST_CASE("every get-item model of a real checkout is read from z_draw.c without a diagnostic",
          "[actor][items][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const auto started = std::chrono::steady_clock::now();
    const toki::actor::ItemModels models = toki::actor::readItemModels(*workspace);
    WARN("read in " << std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count()
                    << " ms");

    std::string listed;
    for (const base::Diagnostic& diagnostic : models.diagnostics) {
        listed += diagnostic.message + "\n";
    }
    INFO(listed);
    REQUIRE(models.diagnostics.empty());
    REQUIRE(models.models.size() > 100);
    const toki::actor::ItemModel* heart = toki::actor::findItemModel(models, "GID_HEART_PIECE");
    REQUIRE(heart != nullptr);
    REQUIRE(heart->object == "object_gi_hearts");
    REQUIRE_FALSE(heart->draws.empty());
    const toki::actor::ItemModel* token = toki::actor::findItemModel(models, "GID_SKULL_TOKEN");
    REQUIRE(token != nullptr);
    REQUIRE(token->drawFunction == "GetItem_DrawSkullToken");
    REQUIRE(token->draws.size() == 2);
    REQUIRE(token->draws[1].pass == toki::actor::Pass::Translucent);
    REQUIRE(token->draws[1].tiles.size() == 2);
    REQUIRE(token->draws[1].tiles[1].height == 64);
    const toki::actor::ItemModel* rupee = toki::actor::findItemModel(models, "GID_RUPEE_GREEN");
    REQUIRE(rupee != nullptr);
    REQUIRE(rupee->draws.size() == 4);
    REQUIRE(rupee->draws[0].placement.scale[0] == 0.7F);
    REQUIRE(rupee->draws[0].list == usize{1});
}

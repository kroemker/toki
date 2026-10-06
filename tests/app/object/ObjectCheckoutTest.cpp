#include "app/object/domain/Object.h"
#include "app/object/domain/ObjectView.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"
#include "n64/RspResult.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using base::usize;

namespace {

std::string describe(const std::vector<base::Diagnostic>& diagnostics) {
    std::map<std::string, usize> kinds;
    for (const base::Diagnostic& diagnostic : diagnostics) {
        ++kinds[diagnostic.message.substr(0, 120)];
    }
    std::string described;
    for (const auto& [message, count] : kinds) {
        if (described.size() < 4000) {
            described += std::to_string(count) + "x " + message + "\n";
        }
    }
    return described;
}

} // namespace

TEST_CASE("objects of a real checkout read and draw what they first show", "[object][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(loaded.has_value());
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    hackeroot::AssetImageReader reader(workspace);

    for (const std::string name :
         {"object_kw1", "object_link_boy", "gameplay_keep", "object_vase"}) {
        const auto& objects = workspace->getAssets().objects;
        const auto asset = std::ranges::find(objects, name, &hackeroot::AssetDirectory::name);
        REQUIRE(asset != objects.end());

        const toki::object::Object object = toki::object::readObject(reader, *asset);
        INFO(name << ": " << describe(object.diagnostics));
        CHECK(std::ranges::none_of(object.diagnostics, [](const base::Diagnostic& diagnostic) {
            return diagnostic.severity == base::Severity::Error;
        }));
        REQUIRE_FALSE(object.assets.empty());

        const toki::object::ObjectSelection selection = toki::object::makeFirstSelection(object);
        REQUIRE((selection.skeleton || selection.displayList));
        const toki::object::ObjectContent made =
            toki::object::makeObjectContent(object, selection, std::nullopt);
        INFO("content: " << describe(made.diagnostics));
        REQUIRE(made.bounds.has_value());
        const toki::viewport::Frame frame = toki::viewport::runFrame(
            made.content, toki::viewport::OrbitCamera{}, {.width = 640, .height = 480});
        INFO("frame: " << describe(frame.diagnostics));
        const auto triangles =
            std::ranges::count_if(frame.rspResult.outputs, [](const n64::RspOutput& output) {
                return std::holds_alternative<n64::Triangle>(output);
            });
        WARN(name << ": " << object.assets.size() << " assets, "
                  << (selection.animation  ? "animated skeleton"
                      : selection.skeleton ? "skeleton"
                                           : "display list")
                  << ", " << triangles << " triangles, " << frame.diagnostics.size()
                  << " frame diagnostics, read in " << object.elapsed.count() << " ms");
        CHECK(triangles > 0);
    }
}

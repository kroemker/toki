#include "hackeroot/Spec.h"

#include "base/Diagnostic.h"
#include "hackeroot/Assets.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace {

[[nodiscard]] hackeroot::Workspace loadFixture() {
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace = hackeroot::Workspace::load(
        std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout", {});
    REQUIRE(workspace.has_value());
    return std::move(*workspace);
}

[[nodiscard]] hackeroot::AssetDirectory findObject(const hackeroot::Workspace& workspace,
                                                   const std::string& name) {
    const auto& objects = workspace.getAssets().objects;
    const auto found = std::ranges::find(objects, name, &hackeroot::AssetDirectory::name);
    REQUIRE(found != objects.end());
    return *found;
}

} // namespace

TEST_CASE("the spec's segments are read with their conditionals answered", "[hackeroot][spec]") {
    const hackeroot::Workspace workspace = loadFixture();
    const hackeroot::Spec& spec = workspace.getSpec();

    REQUIRE(spec.segments.size() == 2);
    const hackeroot::SpecSegment* keep = spec.findSegment("gameplay_keep");
    REQUIRE(keep != nullptr);
    REQUIRE(keep->includes ==
            std::vector<std::string>{"assets/objects/gameplay_keep/gameplay_keep.o"});
    REQUIRE(spec.findSegment("never_built") == nullptr);
}

TEST_CASE("an object's files are the ones the spec links into its segment", "[hackeroot][spec]") {
    const hackeroot::Workspace workspace = loadFixture();

    const hackeroot::ObjectFiles keep =
        hackeroot::findObjectFiles(workspace, findObject(workspace, "gameplay_keep"));
    const hackeroot::ObjectFiles custom =
        hackeroot::findObjectFiles(workspace, findObject(workspace, "object_custom"));

    REQUIRE(keep.diagnostics.empty());
    REQUIRE(keep.files.size() == 1);
    REQUIRE(keep.files.front().filename() == "gameplay_keep.c");
    REQUIRE(keep.files.front().string().contains("extracted"));
    REQUIRE(custom.files.size() == 1);
    REQUIRE(custom.diagnostics.size() == 1);
    REQUIRE(custom.diagnostics.front().message.contains("the spec has no segment object_custom"));
}

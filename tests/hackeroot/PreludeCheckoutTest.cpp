#include "base/Diagnostic.h"
#include "base/Types.h"
#include "cparser/CType.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/AssetSource.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

using base::usize;

namespace {

struct KnownSize {
    std::string_view type;
    usize size;
};

constexpr std::array KnownSizes = {
    KnownSize{"Gfx", 8},
    KnownSize{"Vtx", 16},
    KnownSize{"Mtx", 64},
    KnownSize{"SceneCmd", 8},
    KnownSize{"ActorEntry", 0x10},
    KnownSize{"Vec3s", 6},
    KnownSize{"RoomShapeCullableEntry", 0x10},
    KnownSize{"RoomShapeCullable", 0xC},
    KnownSize{"RoomShapeDListsEntry", 8},
    KnownSize{"Lights1", 0x18},
};

} // namespace

TEST_CASE("a room's headers read under the build's configuration with N64 layouts",
          "[hackeroot][prelude][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const auto spot00 = std::ranges::find(workspace->getAssets().scenes, std::string("spot00"),
                                          &hackeroot::AssetDirectory::name);
    REQUIRE(spot00 != workspace->getAssets().scenes.end());

    const std::expected<cparser::TranslationUnit, base::Diagnostic> room =
        hackeroot::preprocessAsset(*workspace, spot00->directory / "spot00_room_0.c");
    REQUIRE(room.has_value());
    REQUIRE_FALSE(room->unresolvedIncludes.empty());

    const auto started = std::chrono::steady_clock::now();
    const hackeroot::Prelude prelude = hackeroot::readPrelude(*workspace, room->unresolvedIncludes);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    usize loud = 0;
    std::string listed;
    for (const base::Diagnostic& diagnostic : prelude.diagnostics) {
        if (diagnostic.severity != base::Severity::Note) {
            listed += diagnostic.location.file.filename().string() + ":" +
                      std::to_string(diagnostic.location.line) + " " + diagnostic.message + "\n";
            ++loud;
        }
    }
    WARN(room->unresolvedIncludes.size()
         << " headers included, " << prelude.sources.count() << " read, "
         << prelude.types.countTypedefs() << " typedefs in " << elapsed.count() << " ms, " << loud
         << " warnings");
    for (const KnownSize& known : KnownSizes) {
        const cparser::CType* type = prelude.types.findTypedef(known.type);
        INFO(known.type);
        REQUIRE(type != nullptr);
        REQUIRE(type->complete);
        REQUIRE(type->size == known.size);
    }
    INFO(listed);
    REQUIRE(loud == 0);
}

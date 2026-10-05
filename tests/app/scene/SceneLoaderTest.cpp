#include "app/scene/domain/SceneLoader.h"

#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace {

constexpr std::chrono::milliseconds PollInterval(50);
constexpr std::chrono::seconds Patience(30);

constexpr const char* SceneSource = R"(#include "reload.h"

ActorEntry reload_sceneEntries[] = {
    { 0, { 100, 0, -200 }, { 0, 0, 0 }, 0x0FFF },
};

SceneCmd reload_sceneHeader[] = {
    { 0x00, 1, (u32)reload_sceneEntries },
    { 0x04, 1, 0 },
    { 0x14, 0, 0 },
};
)";

constexpr const char* HeaderSource = R"(#ifndef RELOAD_H
#define RELOAD_H

#include "room.h"

typedef struct SceneCmd {
    u8 code;
    u8 data1;
    u32 data2;
} SceneCmd;

typedef struct ActorEntry {
    s16 id;
    s16 pos[3];
    s16 rot[3];
    s16 params;
} ActorEntry;

#endif
)";

std::string makeRoomSource(const int entries) {
    std::string source = R"(#include "reload.h"

extern RoomShapeNormal reload_room_0Shape;

SceneCmd reload_room_0Header[] = {
    { 0x0A, 0, (u32)&reload_room_0Shape },
    { 0x14, 0, 0 },
};

Vtx reload_room_0Vtx[] = {
    VTX(0, 0, 0, 0, 0, 255, 255, 255, 255),
};

Gfx reload_room_0DL[] = {
    gsSPVertex(reload_room_0Vtx, 1, 0),
    gsSPEndDisplayList(),
};

RoomShapeDListsEntry reload_room_0Entries[] = {
)";
    for (int i = 0; i < entries; ++i) {
        source += "    { reload_room_0DL, NULL },\n";
    }
    source += R"(};

RoomShapeNormal reload_room_0Shape = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(reload_room_0Entries),
    reload_room_0Entries,
    reload_room_0Entries + ARRAY_COUNT(reload_room_0Entries),
};
)";
    return source;
}

/// A copy of the fixture checkout in the temp directory with a scene of its
/// own, so a test can edit the scene.
class CheckoutCopy {
public:
    CheckoutCopy() : root(std::filesystem::temp_directory_path() / "toki_tests" / "scene_loader") {
        std::error_code failure;
        std::filesystem::remove_all(root, failure);
        std::filesystem::create_directories(root.parent_path(), failure);
        std::filesystem::copy(std::filesystem::path(TOKI_TEST_FIXTURES) /
                                  "hackeroot/fixtures/checkout",
                              root, std::filesystem::copy_options::recursive, failure);
        std::filesystem::create_directories(getSceneFolder(), failure);
        std::ofstream(root / "include/reload.h") << HeaderSource;
        std::ofstream(getSceneFolder() / "reload_scene.c") << SceneSource;
        writeRoom(1);
    }

    CheckoutCopy(const CheckoutCopy&) = delete;
    CheckoutCopy(CheckoutCopy&&) = delete;
    CheckoutCopy& operator=(const CheckoutCopy&) = delete;
    CheckoutCopy& operator=(CheckoutCopy&&) = delete;

    ~CheckoutCopy() {
        std::error_code failure;
        std::filesystem::remove_all(root, failure);
    }

    [[nodiscard]] const std::filesystem::path& getRoot() const {
        return root;
    }

    [[nodiscard]] std::filesystem::path getSceneFolder() const {
        return root / "assets/scenes/reload";
    }

    void writeRoom(const int entries) const {
        const std::filesystem::path path = getSceneFolder() / "reload_room_0.c";
        const bool existed = std::filesystem::exists(path);
        std::filesystem::file_time_type before{};
        if (existed) {
            before = std::filesystem::last_write_time(path);
        }
        std::ofstream(path, std::ios::trunc) << makeRoomSource(entries);
        if (existed) {
            std::filesystem::last_write_time(path, before + std::chrono::hours(1));
        }
    }

private:
    std::filesystem::path root;
};

[[nodiscard]] std::optional<toki::scene::Scene> waitForScene(toki::scene::SceneLoader& loader) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + Patience;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::optional<toki::scene::Scene> scene = loader.update()) {
            return scene;
        }
        std::this_thread::sleep_for(PollInterval);
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("an open scene is read again when one of its files changes", "[scene][loader]") {
    const CheckoutCopy checkout;
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(checkout.getRoot(), {});
    REQUIRE(loaded.has_value());
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    const auto& scenes = workspace->getAssets().scenes;
    const auto asset =
        std::ranges::find(scenes, std::string("reload"), &hackeroot::AssetDirectory::name);
    REQUIRE(asset != scenes.end());

    toki::scene::SceneLoader loader;
    loader.open(workspace, *asset);
    const std::optional<toki::scene::Scene> first = waitForScene(loader);
    REQUIRE(first.has_value());
    std::string described;
    for (const base::Diagnostic& diagnostic : first->diagnostics) {
        described += base::describe(diagnostic) + "\n";
    }
    INFO(described);
    REQUIRE(first->layers.front().rooms.size() == 1);
    REQUIRE(first->layers.front().rooms[0].displayLists.size() == 1);
    REQUIRE(first->layers.front().header.playerEntries.size() == 1);
    REQUIRE(first->layers.front().header.playerEntries[0].position ==
            std::array<base::s16, 3>{100, 0, -200});

    checkout.writeRoom(3);
    const std::optional<toki::scene::Scene> second = waitForScene(loader);

    REQUIRE(second.has_value());
    REQUIRE(second->layers.front().rooms.size() == 1);
    REQUIRE(second->layers.front().rooms[0].displayLists.size() == 3);
}

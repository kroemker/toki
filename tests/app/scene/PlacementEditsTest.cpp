#include "app/scene/domain/PlacementEdits.h"

#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Types.h"
#include "hackeroot/Tables.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using base::u8;
using base::usize;

namespace {

z64::ImageObject makeObject(const u8 segment, std::string name, std::string type,
                            std::vector<u8> bytes, std::vector<z64::Relocation> relocations = {}) {
    return z64::ImageObject{
        .segment = segment,
        .name = std::move(name),
        .type = std::move(type),
        .isArray = true,
        .alignment = 4,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)}};
}

std::vector<u8> makeEntries(const usize count) {
    std::vector<u8> bytes;
    for (usize i = 0; i < count; ++i) {
        const std::vector<u8> entry{
            0x00, static_cast<u8>(0x10 + i), 0, 1, 0, 2, 0, 3, 0, 0, 0, 0, 0, 0, 0, 7};
        bytes.insert(bytes.end(), entry.begin(), entry.end());
    }
    return bytes;
}

/// A scene with one transition actor whose sides use cameras 5 and 6, and one
/// room whose main header and layer 1 header share an actor list of
/// `actorCount` entries.
toki::scene::Scene makeScene(const usize actorCount = 2) {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject(0, "sSetupDL", "u8", {0, 0, 0, 0, 0, 0, 0, 0})}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(2, "scene", "SceneCmd", {0x0E, 1, 0, 0, 0,    0, 0, 0, 0x00, 1, 0, 0,
                                             0,    0, 0, 0, 0x14, 0, 0, 0, 0,    0, 0, 0},
                    {{.offset = 4, .symbol = "doors"}, {.offset = 12, .symbol = "entries"}}),
         makeObject(2, "doors", "TransitionActorEntry",
                    {0, 5, 1, 6, 0, 9, 0, 1, 0, 2, 0, 3, 0, 0, 0, 0}),
         makeObject(2, "entries", "ActorEntry", makeEntries(1))},
        {})));
    const auto count = static_cast<u8>(actorCount);
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(3, "room", "SceneCmd", {0x18, 0, 0, 0, 0,    0, 0, 0, 0x01, count, 0, 0,
                                            0,    0, 0, 0, 0x14, 0, 0, 0, 0,    0,     0, 0},
                    {{.offset = 4, .symbol = "alternates"}, {.offset = 12, .symbol = "actors"}}),
         makeObject(3, "alternates", "SceneCmd*", {0, 0, 0, 0},
                    {{.offset = 0, .symbol = "roomAlt"}}),
         makeObject(3, "roomAlt", "SceneCmd",
                    {0x01, count, 0, 0, 0, 0, 0, 0, 0x14, 0, 0, 0, 0, 0, 0, 0},
                    {{.offset = 4, .symbol = "actors"}}),
         makeObject(3, "actors", "ActorEntry", makeEntries(actorCount))},
        {})));
    return scene;
}

toki::scene::RoomHeader readRoom(const toki::scene::SceneModel& model, const char* header) {
    const std::vector<std::shared_ptr<const z64::MemoryImage>> images = model.layOut();
    return toki::scene::readRoomHeader(*images[2], *images[2]->findAddress(header));
}

constexpr toki::scene::ActorPlacement Moved{
    .id = 0x22, .position = {100, -200, 300}, .rotation = {0, 0x4000, 0}, .params = -1};

} // namespace

TEST_CASE("an actor set in one layer is set in a list of its own", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::setPlacement(
        model, {.kind = toki::scene::PlacementKind::Actor, .layer = 1}, 1, Moved));

    const toki::scene::RoomHeader alternate = readRoom(model, "roomAlt");
    REQUIRE(alternate.actors.size() == 2);
    REQUIRE(alternate.actors[1].id == 0x22);
    REQUIRE(alternate.actors[1].position == std::array<base::s16, 3>{100, -200, 300});
    REQUIRE(alternate.actors[1].rotation[1] == 0x4000);
    REQUIRE(alternate.actors[1].params == -1);
    REQUIRE(readRoom(model, "room").actors[1].id == 0x11);
    REQUIRE(model.findSymbol("room_header01_actorList") != nullptr);
    REQUIRE(model.isEdited());
}

TEST_CASE("an actor added to a list another layer shares leaves that layer as it was",
          "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    const auto index =
        toki::scene::addPlacement(model, {.kind = toki::scene::PlacementKind::Actor}, Moved);

    REQUIRE(index == usize{2});
    REQUIRE(readRoom(model, "room").actors.size() == 3);
    REQUIRE(readRoom(model, "room").actors[2].id == 0x22);
    REQUIRE(readRoom(model, "roomAlt").actors.size() == 2);
}

TEST_CASE("an actor removed leaves the others in order", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene(3));

    REQUIRE(toki::scene::removePlacement(model, {.kind = toki::scene::PlacementKind::Actor}, 0));

    const toki::scene::RoomHeader room = readRoom(model, "room");
    REQUIRE(room.actors.size() == 2);
    REQUIRE(room.actors[0].id == 0x11);
    REQUIRE(room.actors[1].id == 0x12);
    REQUIRE(readRoom(model, "roomAlt").actors.size() == 3);
}

TEST_CASE("the last actor removed takes its command, and the list once nothing points at it",
          "[scene][edit]") {
    toki::scene::SceneModel model(makeScene(1));
    const toki::scene::PlacementList main{.kind = toki::scene::PlacementKind::Actor};
    const toki::scene::PlacementList alternate{.kind = toki::scene::PlacementKind::Actor,
                                               .layer = 1};

    REQUIRE(toki::scene::removePlacement(model, main, 0));
    REQUIRE(readRoom(model, "room").actors.empty());
    REQUIRE(model.findSymbol("actors") != nullptr);
    REQUIRE(toki::scene::removePlacement(model, alternate, 0));

    REQUIRE(model.findSymbol("actors") == nullptr);
    REQUIRE(model.findSymbol("room")->bytes.size() == 16);
    REQUIRE_FALSE(toki::scene::addPlacement(model, main, Moved));
}

TEST_CASE("a transition actor set keeps the cameras of its sides", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::setTransitionActor(model, 0, 0,
                                            {.rooms = {2, 3},
                                             .id = 0x2E,
                                             .position = {4, 5, 6},
                                             .rotationY = -0x8000,
                                             .params = 0x0102}));

    const std::vector<u8>& bytes = model.findSymbol("doors")->bytes;
    REQUIRE(bytes == std::vector<u8>{2, 5, 3, 6, 0, 0x2E, 0, 4, 0, 5, 0, 6, 0x80, 0, 1, 2});
}

TEST_CASE("a player entry is set but not removed", "[scene][edit]") {
    toki::scene::SceneModel model(makeScene());
    const toki::scene::PlacementList entries{.kind = toki::scene::PlacementKind::PlayerEntry};

    REQUIRE(toki::scene::setPlacement(model, entries, 0, Moved));
    REQUIRE(model.findSymbol("entries")->bytes[1] == 0x22);
    REQUIRE_FALSE(toki::scene::removePlacement(model, entries, 0));
    REQUIRE_FALSE(toki::scene::setPlacement(model, entries, 1, Moved));
}

TEST_CASE("an edit of the open scene shows in its layers, and one that fails changes nothing",
          "[scene][edit]") {
    toki::scene::Scene scene = makeScene();
    toki::scene::readSceneLayers(scene);
    toki::scene::SceneEditor editor;
    const toki::scene::PlacementRef first{.kind = toki::scene::PlacementKind::Actor};

    REQUIRE(editor.apply(
        scene, first, [](toki::scene::SceneModel& model, const toki::scene::PlacementList& list) {
            return toki::scene::setPlacement(model, list, 0, Moved);
        }));
    REQUIRE(scene.layers.front().rooms.front().actors.front().id == 0x22);
    REQUIRE(editor.isEdited());

    const auto images = scene.images;
    REQUIRE_FALSE(editor.apply(
        scene, first, [](toki::scene::SceneModel& model, const toki::scene::PlacementList& list) {
            static_cast<void>(toki::scene::addPlacement(model, list, Moved));
            return toki::scene::setPlacement(model, list, 9, Moved);
        }));
    REQUIRE(scene.images == images);
    REQUIRE(editor.findModel()->findSymbol("actors")->bytes.size() == 32);
}

TEST_CASE("an edit is undone and redone as a step, and undoing every step leaves none",
          "[scene][edit]") {
    toki::scene::Scene scene = makeScene();
    toki::scene::readSceneLayers(scene);
    const auto read = scene.layers.front().rooms.front().actors.front();
    toki::scene::SceneEditor editor;
    const toki::scene::PlacementRef first{.kind = toki::scene::PlacementKind::Actor};
    const auto set = [](toki::scene::SceneModel& model, const toki::scene::PlacementList& list) {
        return toki::scene::setPlacement(model, list, 0, Moved);
    };
    const auto add = [](toki::scene::SceneModel& model, const toki::scene::PlacementList& list) {
        return toki::scene::addPlacement(model, list, Moved).transform([](auto) {});
    };
    REQUIRE_FALSE(editor.canUndo());
    REQUIRE(editor.apply(scene, first, set));
    REQUIRE(editor.apply(scene, first, add, true));
    REQUIRE(scene.layers.front().rooms.front().actors.size() == 3);

    REQUIRE(editor.undo(scene));
    REQUIRE(scene.layers.front().rooms.front().actors.size() == 2);
    REQUIRE(scene.layers.front().rooms.front().actors.front().id == read.id);
    REQUIRE_FALSE(editor.isEdited());
    REQUIRE_FALSE(editor.canUndo());

    REQUIRE(editor.redo(scene));
    REQUIRE(scene.layers.front().rooms.front().actors.size() == 3);
    REQUIRE(editor.isEdited());
    REQUIRE_FALSE(editor.redo(scene));
}

TEST_CASE("objects are added to the end of a room's object list", "[scene][edit]") {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject(0, "sSetupDL", "u8", {0, 0, 0, 0, 0, 0, 0, 0})}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject(2, "scene", "SceneCmd", {0x14, 0, 0, 0, 0, 0, 0, 0})}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(3, "room", "SceneCmd", {0x0B, 1, 0, 0, 0, 0, 0, 0, 0x14, 0, 0, 0, 0, 0, 0, 0},
                    {{.offset = 4, .symbol = "objects"}}),
         makeObject(3, "objects", "s16", {0x00, 0x05})},
        {})));
    toki::scene::SceneModel model(scene);
    const std::array<base::s16, 2> added{0x0102, 0x0003};

    REQUIRE(toki::scene::addRoomObjects(model, 0, 0, added));

    REQUIRE(readRoom(model, "room").objects == std::vector<base::s16>{5, 0x0102, 3});
}

TEST_CASE("an actor's object is missing when no keep and no object list of its room loads it",
          "[scene][edit]") {
    toki::scene::Scene scene;
    toki::scene::SceneLayer layer;
    layer.header.keepObjectId = 2;
    layer.rooms.push_back(toki::scene::RoomHeader{.objects = {3}});
    scene.layers.push_back(std::move(layer));
    hackeroot::Tables tables;
    tables.objects = {{.id = 1, .name = "gameplay_keep"},
                      {.id = 2, .name = "gameplay_field_keep"},
                      {.id = 3, .name = "object_kanban"},
                      {.id = 4, .name = "object_box"}};
    const toki::scene::PlacementRef actor{.kind = toki::scene::PlacementKind::Actor};

    REQUIRE(toki::scene::findMissingObject(scene, actor, "object_box", tables) == base::s16{4});
    REQUIRE_FALSE(toki::scene::findMissingObject(scene, actor, "object_kanban", tables));
    REQUIRE_FALSE(toki::scene::findMissingObject(scene, actor, "gameplay_keep", tables));
    REQUIRE_FALSE(toki::scene::findMissingObject(scene, actor, "gameplay_field_keep", tables));
    REQUIRE_FALSE(toki::scene::findMissingObject(scene, actor, "object_unknown", tables));
}

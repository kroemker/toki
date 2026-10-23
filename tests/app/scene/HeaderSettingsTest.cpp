#include "app/scene/domain/HeaderSettings.h"

#include "app/scene/domain/HeaderNames.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Types.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/TypeParser.h"
#include "z64/DecompDialect.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using base::s16;
using base::s64;
using base::u8;
using toki::scene::HeaderSetting;

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

const std::vector<u8> End{0x14, 0, 0, 0, 0, 0, 0, 0};

std::vector<u8> join(std::initializer_list<std::vector<u8>> commands) {
    std::vector<u8> bytes;
    for (const std::vector<u8>& command : commands) {
        bytes.insert(bytes.end(), command.begin(), command.end());
    }
    return bytes;
}

/// A scene whose header sets its sound and skybox, and one room with a main
/// header and headers for layers 1 and 2 that are one and the same, with
/// `layerThree` a fourth entry: the room's main header, or none.
toki::scene::Scene makeScene(const std::optional<std::string>& layerThree = std::nullopt) {
    toki::scene::Scene scene;
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(
        z64::layOutImage({makeObject(0, "sSetupDL", "u8", {0, 0, 0, 0, 0, 0, 0, 0})}, {})));
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(2, "scene", "SceneCmd",
                    join({{0x15, 2, 0, 0, 0, 0, 0x13, 0x2C}, {0x11, 0, 0, 0, 1, 0, 1, 0}, End}))},
        {})));
    std::vector<z64::Relocation> alternates{{.offset = 0, .symbol = "roomShared"},
                                            {.offset = 4, .symbol = "roomShared"}};
    if (layerThree) {
        alternates.push_back({.offset = 8, .symbol = *layerThree});
    }
    scene.images.push_back(std::make_shared<const z64::MemoryImage>(z64::layOutImage(
        {makeObject(3, "room", "SceneCmd",
                    join({{0x18, 0, 0, 0, 0, 0, 0, 0},
                          {0x08, 1, 0, 0, 0, 0, 0x05, 0x02},
                          {0x05, 0, 0, 0, 0xF6, 0x0A, 0x80, 0x20},
                          End}),
                    {{.offset = 4, .symbol = "alternates"}}),
         makeObject(3, "alternates", "SceneCmd*", std::vector<u8>(alternates.size() * 4),
                    alternates),
         makeObject(3, "roomShared", "SceneCmd", join({{0x16, 0, 0, 0, 0, 0, 0, 7}, End}))},
        {})));
    return scene;
}

std::vector<toki::scene::HeaderCommand> readRoomCommands(const toki::scene::SceneModel& model,
                                                         const std::string& header) {
    const std::vector<std::shared_ptr<const z64::MemoryImage>> images = model.layOut();
    return toki::scene::readRoomHeader(*images[2], *images[2]->findAddress(header)).commands;
}

std::vector<toki::scene::HeaderCommand> readSceneCommands(const toki::scene::SceneModel& model) {
    return toki::scene::readSceneHeader(*model.layOut()[1]).commands;
}

} // namespace

TEST_CASE("a setting is read out of the bits of its command", "[scene][header]") {
    const toki::scene::Scene scene = makeScene();
    const std::vector<toki::scene::HeaderCommand> room =
        toki::scene::readRoomHeader(*scene.images[2]).commands;
    const std::vector<toki::scene::HeaderCommand> sceneCommands =
        toki::scene::readSceneHeader(*scene.images[1]).commands;

    REQUIRE(toki::scene::findSetting(sceneCommands, HeaderSetting::SoundSpec) == 2);
    REQUIRE(toki::scene::findSetting(sceneCommands, HeaderSetting::NatureAmbience) == 0x13);
    REQUIRE(toki::scene::findSetting(sceneCommands, HeaderSetting::Sequence) == 0x2C);
    REQUIRE(toki::scene::findSetting(sceneCommands, HeaderSetting::Skybox) == 1);
    REQUIRE(toki::scene::findSetting(sceneCommands, HeaderSetting::LightMode) == 1);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::RoomType) == 1);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::Environment) == 2);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::LensMode) == 1);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::DisablesWarpSongs) == 1);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::WindX) == -10);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::WindY) == 10);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::WindZ) == -128);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::WindStrength) == 0x20);
    REQUIRE_FALSE(toki::scene::findSetting(room, HeaderSetting::Echo));
}

TEST_CASE("a setting set changes its own bits and no other", "[scene][header]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::Environment, 7));
    REQUIRE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::LensMode, 0));
    REQUIRE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::WindZ, 100));
    REQUIRE(toki::scene::setHeaderSetting(model, {}, HeaderSetting::Sequence, 0x7F));

    const std::vector<toki::scene::HeaderCommand> room = readRoomCommands(model, "room");
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::Environment) == 7);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::LensMode) == 0);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::DisablesWarpSongs) == 1);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::RoomType) == 1);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::WindZ) == 100);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::WindX) == -10);
    const std::vector<toki::scene::HeaderCommand> scene = readSceneCommands(model);
    REQUIRE(toki::scene::findSetting(scene, HeaderSetting::Sequence) == 0x7F);
    REQUIRE(toki::scene::findSetting(scene, HeaderSetting::NatureAmbience) == 0x13);
    REQUIRE(model.isEdited());
}

TEST_CASE("a command a header lacks is added before its end", "[scene][header]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::TimeSpeed, -3));
    REQUIRE(toki::scene::addSettingCommand(model, {.room = 0}, HeaderSetting::Echo));

    const std::vector<toki::scene::HeaderCommand> room = readRoomCommands(model, "room");
    REQUIRE(room.size() == 5);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::TimeSpeed) == -3);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::Hour) == 0xFF);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::Minute) == 0xFF);
    REQUIRE(toki::scene::findSetting(room, HeaderSetting::Echo) == 0);
    REQUIRE(model.findSymbol("room")->bytes.size() == 6 * 8);
}

TEST_CASE("a header two layers share is copied for the one edited", "[scene][header]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::setHeaderSetting(model, {.layer = 2, .room = 0}, HeaderSetting::Echo, 3));

    REQUIRE(toki::scene::findSetting(readRoomCommands(model, "roomShared"), HeaderSetting::Echo) ==
            7);
    REQUIRE(toki::scene::findSetting(readRoomCommands(model, "room_header02"),
                                     HeaderSetting::Echo) == 3);
    const z64::SymbolData* alternates = model.findSymbol("alternates");
    REQUIRE(alternates->relocations[0].symbol == "roomShared");
    REQUIRE(alternates->relocations[1].symbol == "room_header02");
}

TEST_CASE("a main header another layer points at keeps its place, and that layer is given a copy",
          "[scene][header]") {
    toki::scene::SceneModel model(makeScene("room"));

    REQUIRE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::Echo, 3));

    REQUIRE(toki::scene::findSetting(readRoomCommands(model, "room"), HeaderSetting::Echo) == 3);
    const std::vector<toki::scene::HeaderCommand> copy = readRoomCommands(model, "room_header03");
    REQUIRE_FALSE(toki::scene::findSetting(copy, HeaderSetting::Echo));
    REQUIRE(toki::scene::findSetting(copy, HeaderSetting::WindStrength) == 0x20);
    REQUIRE(copy.size() == 2);
    REQUIRE(model.findSymbol("alternates")->relocations[2].symbol == "room_header03");
}

TEST_CASE("a room with no header for a layer is edited in its main header", "[scene][header]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE(toki::scene::setHeaderSetting(model, {.layer = 3, .room = 0}, HeaderSetting::Echo, 1));

    REQUIRE(toki::scene::findSetting(readRoomCommands(model, "room"), HeaderSetting::Echo) == 1);
}

TEST_CASE("a setting out of its range or in the wrong header is not set", "[scene][header]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE_FALSE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::WindX, 128));
    REQUIRE_FALSE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::LensMode, 2));
    REQUIRE_FALSE(toki::scene::setHeaderSetting(model, {}, HeaderSetting::Echo, 1));
    REQUIRE_FALSE(toki::scene::setHeaderSetting(model, {.room = 0}, HeaderSetting::Skybox, 1));
    REQUIRE_FALSE(model.isEdited());
    REQUIRE(toki::scene::getSettingRange(HeaderSetting::TimeSpeed).minimum == -128);
    REQUIRE(toki::scene::getSettingRange(HeaderSetting::MapLocation).maximum == 0xFFFF'FFFF);
}

TEST_CASE("an object list is made, reordered and removed whole", "[scene][header]") {
    toki::scene::SceneModel model(makeScene());
    const std::array<s16, 2> objects{0x0014, 0x0102};
    const std::array<s16, 2> swapped{0x0102, 0x0014};

    REQUIRE(toki::scene::setRoomObjects(model, 0, 0, objects));
    REQUIRE(toki::scene::readRoomHeader(*model.layOut()[2]).objects ==
            std::vector<s16>{0x0014, 0x0102});
    REQUIRE(model.findSymbol("room_header00_objectList") != nullptr);

    REQUIRE(toki::scene::setRoomObjects(model, 0, 0, swapped));
    REQUIRE(toki::scene::readRoomHeader(*model.layOut()[2]).objects ==
            std::vector<s16>{0x0102, 0x0014});

    REQUIRE(toki::scene::setRoomObjects(model, 0, 0, {}));
    REQUIRE(toki::scene::readRoomHeader(*model.layOut()[2]).objects.empty());
    REQUIRE(model.findSymbol("room_header00_objectList") == nullptr);
    REQUIRE(model.findSymbol("room")->bytes.size() == 4 * 8);
}

TEST_CASE("the names of settings' values come from the checkout's enumerations and macros",
          "[scene][header]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = cparser::preprocessText(
        base, "names.h",
        "#define SCENE_CAM_TYPE_DEFAULT 0\n"
        "#define SCENE_CAM_TYPE_FIXED (0x30)\n"
        "#define SCENE_CAM_TYPE_SHIFT(x) (x)\n"
        "typedef enum SkyboxId { SKYBOX_NONE, SKYBOX_NORMAL_SKY, SKYBOX_MAX } SkyboxId;\n"
        "typedef enum { NA_BGM_GENERAL_SFX, NA_BGM_NO_MUSIC = 0x7F } NA_BGM;\n",
        cparser::PreprocessOptions{.dialect = &z64::getDecompDialect(), .expandMacros = true});
    const cparser::TypeParseResult parsed =
        cparser::parseTypes(unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});

    const toki::scene::HeaderNames names = toki::scene::readHeaderNames(parsed.types, unit.macros);

    REQUIRE(std::vector(names.findValues(HeaderSetting::Skybox).begin(),
                        names.findValues(HeaderSetting::Skybox).end()) ==
            std::vector<toki::scene::NamedValue>{{"SKYBOX_NONE", 0}, {"SKYBOX_NORMAL_SKY", 1}});
    REQUIRE(names.findValues(HeaderSetting::Sequence).back() ==
            toki::scene::NamedValue{"NA_BGM_NO_MUSIC", 0x7F});
    REQUIRE(std::vector(names.findValues(HeaderSetting::CameraType).begin(),
                        names.findValues(HeaderSetting::CameraType).end()) ==
            std::vector<toki::scene::NamedValue>{{"SCENE_CAM_TYPE_DEFAULT", 0},
                                                 {"SCENE_CAM_TYPE_FIXED", 0x30}});
    REQUIRE(names.findValues(HeaderSetting::RoomType).empty());
}

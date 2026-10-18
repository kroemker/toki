#include "app/scene/domain/SceneLayout.h"

#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "support/WritingHarness.h"
#include "z64/MemoryImage.h"
#include "z64/SymbolData.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using base::u8;
using toki::scene::FilePart;

namespace {

constexpr std::string_view Header =
    "typedef unsigned char u8;\n"
    "typedef short s16;\n"
    "typedef unsigned short u16;\n"
    "typedef int s32;\n"
    "typedef unsigned int u32;\n"
    "typedef float f32;\n"
    "typedef unsigned int uintptr_t;\n"
    "typedef struct { u8 code; u8 data1; u32 data2; } SCmdBase;\n"
    "typedef union { SCmdBase base; } SceneCmd;\n"
    "typedef struct { uintptr_t vromStart; uintptr_t vromEnd; } RomFile;\n"
    "typedef struct { s16 count; void* polys; } CollisionHeader;\n"
    "typedef union { s32 i; f32 f; } CutsceneData;\n"
    "typedef struct { u8 type; u8 numEntries; void* entries; void* entriesEnd; } "
    "RoomShapeNormal;\n"
    "typedef struct { void* opa; void* xlu; } RoomShapeDListsEntry;\n"
    "typedef struct { u32 w0, w1; } Gwords;\n"
    "typedef union { Gwords words; long long int force; } Gfx;\n"
    "typedef struct { s16 ob[3]; u16 flag; s16 tc[2]; u8 cn[4]; } Vtx;\n"
    "typedef enum SceneCommandTypeID {\n"
    "    SCENE_CMD_ID_COLLISION_HEADER = 0x03,\n"
    "    SCENE_CMD_ID_ROOM_LIST = 0x04,\n"
    "    SCENE_CMD_ID_ROOM_SHAPE = 0x0A,\n"
    "    SCENE_CMD_ID_END = 0x14,\n"
    "    SCENE_CMD_ID_CUTSCENE_DATA = 0x17,\n"
    "    SCENE_CMD_ID_ANIMATED_MATERIAL_LIST = 0x1A\n"
    "} SceneCommandTypeID;\n"
    "#define NULL ((void*)0)\n";

/// A scene with a collision header, a cutscene, a command only HackerOoT has
/// and a symbol nothing reaches, and one room drawn from one display list.
class Harness : public testing::WritingHarness {
public:
    Harness()
        : WritingHarness(
              Header,
              {{"test_scene_collisionHeader", {.type = "CollisionHeader", .elementSize = 8}},
               {"test_scene_polys", {.type = "s16", .isArray = true, .elementSize = 2}},
               {"test_scene_roomList", {.type = "RomFile", .isArray = true, .elementSize = 8}},
               {"gTestCs", {.type = "CutsceneData", .isArray = true, .elementSize = 4}},
               {"test_scene_materials", {.type = "u8", .isArray = true, .elementSize = 1}},
               {"_test_room_0SegmentRomStart", {.type = "u8", .isArray = true, .elementSize = 1}},
               {"_test_room_0SegmentRomEnd", {.type = "u8", .isArray = true, .elementSize = 1}},
               {"test_room_0_shapeHeader", {.type = "RoomShapeNormal", .elementSize = 12}},
               {"test_room_0_entries",
                {.type = "RoomShapeDListsEntry", .isArray = true, .elementSize = 8}},
               {"test_room_0_dl", {.type = "Gfx", .isArray = true, .elementSize = 8}},
               {"test_room_0_vtx", {.type = "Vtx", .isArray = true, .elementSize = 16}}}) {}

    toki::scene::Scene makeScene() {
        toki::scene::Scene scene;
        scene.images.push_back(std::make_shared<const z64::MemoryImage>());
        scene.images.push_back(layOut(
            2, {"SceneCmd test_scene_header00[] = { { 0x03, 0, (u32)&test_scene_collisionHeader }, "
                "{ 0x04, 1, (u32)test_scene_roomList }, { 0x17, 0, (u32)gTestCs }, "
                "{ 0x1A, 0, (u32)test_scene_materials }, { 0x14, 0, 0 } };\n",
                "CollisionHeader test_scene_collisionHeader = { 1, test_scene_polys };\n",
                "s16 test_scene_polys[] = { 1, 2 };\n",
                "RomFile test_scene_roomList[] = { { (uintptr_t)_test_room_0SegmentRomStart, "
                "(uintptr_t)_test_room_0SegmentRomEnd } };\n",
                "CutsceneData gTestCs[] = { 1, 2 };\n", "u8 test_scene_materials[] = { 5 };\n",
                "u8 test_scene_unused[] = { 7 };\n"}));
        scene.images.push_back(layOut(
            3, {"SceneCmd test_room_0_header00[] = { { 0x0A, 0, (u32)&test_room_0_shapeHeader }, "
                "{ 0x14, 0, 0 } };\n",
                "RoomShapeNormal test_room_0_shapeHeader = { 0, 1, test_room_0_entries, "
                "test_room_0_entries + 1 };\n",
                "RoomShapeDListsEntry test_room_0_entries[] = { { test_room_0_dl, NULL } };\n",
                "Gfx test_room_0_dl[] = { { 0x01001002, (uintptr_t)test_room_0_vtx } };\n",
                "Vtx test_room_0_vtx[] = { { { 1, 2, 3 }, 0, { 4, 5 }, { 6, 7, 8, 9 } } };\n"}));
        return scene;
    }

private:
    std::shared_ptr<const z64::MemoryImage> layOut(const u8 segment,
                                                   const std::vector<std::string>& sources) {
        std::vector<z64::ImageObject> objects;
        for (const std::string& source : sources) {
            z64::SymbolData symbol = encode(source);
            symbol.segment = segment;
            symbol.alignment = 8;
            objects.push_back(z64::makeImageObject(symbol));
        }
        return std::make_shared<const z64::MemoryImage>(z64::layOutImage(std::move(objects), {}));
    }
};

const toki::scene::WrittenFile* findFile(const toki::scene::WrittenScene& scene,
                                         const std::string_view name) {
    const auto found = std::ranges::find(scene.files, name, &toki::scene::WrittenFile::name);
    return found == scene.files.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("each symbol goes to the file of what reaches it first", "[scene][layout]") {
    Harness harness;
    const toki::scene::SceneModel model(harness.makeScene());

    const toki::scene::SceneLayout layout =
        toki::scene::assignSceneFiles(model, harness.getTypes());

    REQUIRE(layout.places.size() == 2);
    const std::vector<toki::scene::SymbolPlace>& scene = layout.places[0];
    REQUIRE(scene[0].part == FilePart::Main);
    REQUIRE(scene[1].part == FilePart::Collision);
    REQUIRE(scene[2].part == FilePart::Collision);
    REQUIRE(scene[3].part == FilePart::Main);
    REQUIRE(scene[4].part == FilePart::Cutscene);
    REQUIRE(scene[5].part == FilePart::Main);
    REQUIRE(scene[5].condition == std::optional<std::string>("ENABLE_ANIMATED_MATERIALS"));
    REQUIRE(scene[6].part == FilePart::Main);
    REQUIRE_FALSE(scene[6].condition.has_value());
    REQUIRE(layout.cutsceneCount == 1);
    const std::vector<toki::scene::SymbolPlace>& room = layout.places[1];
    REQUIRE(room[0].part == FilePart::Main);
    REQUIRE(room[1].part == FilePart::ModelInfo);
    REQUIRE(room[2].part == FilePart::ModelInfo);
    REQUIRE(room[3].part == FilePart::Model);
    REQUIRE(room[4].part == FilePart::Model);
}

TEST_CASE("a scene is written in fast64's files, its header declaring what they define",
          "[scene][layout]") {
    Harness harness;
    const toki::scene::SceneModel model(harness.makeScene());
    const toki::scene::SceneLayout layout =
        toki::scene::assignSceneFiles(model, harness.getTypes());
    const z64::DeclarationWriter writer = harness.makeWriter();
    const std::vector<std::string> includes{"ultra64.h", "scene.h"};

    const toki::scene::WrittenScene written = toki::scene::writeSceneFiles(
        model, layout,
        toki::scene::SceneWriting{.name = "test",
                                  .includes = includes,
                                  .writer = &writer,
                                  .lengthDefines = {"test_scene_polys"}});

    REQUIRE(written.diagnostics.empty());
    REQUIRE(written.segments ==
            std::vector<std::vector<std::string>>{
                {"test_scene_main", "test_scene_col", "test_scene_cs_0"},
                {"test_room_0_main", "test_room_0_model_info", "test_room_0_model"}});
    REQUIRE(written.files.size() == 8);

    const std::string& main = findFile(written, "test_scene_main.c")->text;
    REQUIRE(main.starts_with("// toki: scene\n#include \"test_scene.h\"\n\nSceneCmd "
                             "test_scene_header00[5] = {\n"));
    REQUIRE(main.contains("#if ENABLE_ANIMATED_MATERIALS\nu8 test_scene_materials[1] = {\n"));
    REQUIRE_FALSE(main.contains("test_scene_polys[2] ="));
    REQUIRE(findFile(written, "test_scene_col.c")->text.contains("s16 test_scene_polys[2] ="));

    const std::string& header = findFile(written, "test_scene.h")->text;
    REQUIRE(header.starts_with("#ifndef TEST_SCENE_H\n#define TEST_SCENE_H\n\n#include "
                               "\"ultra64.h\"\n#include \"scene.h\"\n"));
    REQUIRE(
        header.contains("#define LENGTH_test_scene_polys 2\nextern s16 test_scene_polys[2];\n"));
    REQUIRE(header.contains("#if ENABLE_ANIMATED_MATERIALS\nextern u8 test_scene_materials[1];\n"
                            "#endif\n"));
    REQUIRE(header.contains("extern u8 _test_room_0SegmentRomStart[];\n"));
    REQUIRE_FALSE(header.contains("test_room_0_dl"));

    REQUIRE(findFile(written, "test_room_0.h")->text.contains("extern Gfx test_room_0_dl[1];\n"));
    REQUIRE(findFile(written, "test_room_0_model.c")
                ->text.starts_with("#include \"test_scene.h\"\n#include \"test_room_0.h\"\n"));
    REQUIRE(
        findFile(written, "test_room_0_model_info.c")->text.contains("&test_room_0_entries[1]"));
}

#include "app/scene/domain/SceneCommandWriter.h"

#include "support/WritingHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {

constexpr std::string_view Header =
    "typedef unsigned char u8;\n"
    "typedef short s16;\n"
    "typedef unsigned int u32;\n"
    "typedef struct { u8 code; u8 data1; u32 data2; } SCmdBase;\n"
    "typedef struct { u8 code; u8 length; void* data; } SCmdActorList;\n"
    "typedef union { SCmdBase base; SCmdActorList actorList; } SceneCmd;\n"
    "typedef struct { s16 id; s16 pos[3]; s16 rot[3]; s16 params; } ActorEntry;\n"
    "typedef enum SceneCommandTypeID {\n"
    "    SCENE_CMD_ID_ACTOR_LIST = 0x01,\n"
    "    SCENE_CMD_ID_SPECIAL_FILES = 0x07,\n"
    "    SCENE_CMD_ID_ROOM_BEHAVIOR = 0x08,\n"
    "    SCENE_CMD_ID_LIGHT_LIST = 0x0C,\n"
    "    SCENE_CMD_ID_TIME_SETTINGS = 0x10,\n"
    "    SCENE_CMD_ID_END = 0x14,\n"
    "    SCENE_CMD_ID_MISC_SETTINGS = 0x19,\n"
    "    SCENE_CMD_ID_ANIMATED_MATERIAL_LIST = 0x1A\n"
    "} SceneCommandTypeID;\n"
    "typedef enum NaviQuestHintFileId { NAVI_QUEST_HINTS_NONE, NAVI_QUEST_HINTS_OVERWORLD } "
    "NaviQuestHintFileId;\n"
    "typedef enum ObjectId { OBJECT_INVALID, OBJECT_GAMEPLAY_KEEP, OBJECT_GAMEPLAY_FIELD_KEEP } "
    "ObjectId;\n"
    "typedef enum RoomType { ROOM_TYPE_NORMAL, ROOM_TYPE_DUNGEON } RoomType;\n"
    "#define NULL ((void*)0)\n"
    "#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((0x01 << (w)) - 1)) << (s)))\n"
    "#define CMD_BBBB(a, b, c, d) (_SHIFTL(a, 24, 8) | _SHIFTL(b, 16, 8) | _SHIFTL(c, 8, 8) | "
    "_SHIFTL(d, 0, 8))\n"
    "#define CMD_W(a) (a)\n"
    "#define CMD_PTR(a) (u32)(a)\n"
    "#define SCENE_CMD_ACTOR_LIST(numActors, actorList) { SCENE_CMD_ID_ACTOR_LIST, numActors, "
    "CMD_PTR(actorList) }\n"
    "#define SCENE_CMD_SPECIAL_FILES(naviQuestHintFileId, keepObjectId) { "
    "SCENE_CMD_ID_SPECIAL_FILES, naviQuestHintFileId, CMD_W(keepObjectId) }\n"
    "#define SCENE_CMD_ROOM_BEHAVIOR(type, environment, showInvisActors, disableWarpSongs) { "
    "SCENE_CMD_ID_ROOM_BEHAVIOR, type, environment | _SHIFTL(showInvisActors, 8, 1) | "
    "_SHIFTL(disableWarpSongs, 10, 1) }\n"
    "#define SCENE_CMD_TIME_SETTINGS(hour, min, timeSpeed) { SCENE_CMD_ID_TIME_SETTINGS, 0, "
    "CMD_BBBB(hour, min, timeSpeed, 0) }\n"
    "#define SCENE_CMD_END() { SCENE_CMD_ID_END, 0, CMD_W(0) }\n"
    "#define SCENE_CMD_MISC_SETTINGS(sceneCamType, worldMapLocation) { "
    "SCENE_CMD_ID_MISC_SETTINGS, sceneCamType, CMD_W(worldMapLocation) }\n"
    "#define SCENE_CMD_ANIMATED_MATERIAL_LIST(matAnimList, camParams) { "
    "SCENE_CMD_ID_ANIMATED_MATERIAL_LIST, camParams, CMD_PTR(matAnimList) }\n";

class Harness : public testing::WritingHarness {
public:
    Harness()
        : WritingHarness(Header,
                         {{"sActors", {.type = "ActorEntry", .isArray = true, .elementSize = 16}},
                          {"sMaterials", {.type = "u8", .isArray = true, .elementSize = 1}}}) {}

    std::string writeCommands(const std::string& source) {
        return writeAgain(
            source, makeWriter({{"SceneCmd", toki::scene::makeSceneCommandWriter(getTypes())}}));
    }
};

} // namespace

TEST_CASE("a scene command is written as its macro, its IDs by name", "[scene][writer]") {
    Harness harness;

    const std::string written = harness.writeCommands(
        "SceneCmd header[] = {\n"
        "    SCENE_CMD_ACTOR_LIST(2, &sActors[1]),\n"
        "    SCENE_CMD_SPECIAL_FILES(NAVI_QUEST_HINTS_OVERWORLD, OBJECT_GAMEPLAY_FIELD_KEEP),\n"
        "    SCENE_CMD_ROOM_BEHAVIOR(ROOM_TYPE_DUNGEON, 3, 1, 0),\n"
        "    SCENE_CMD_TIME_SETTINGS(0xFF, 0xFF, 10),\n"
        "    SCENE_CMD_MISC_SETTINGS(0x30, 5),\n"
        "    SCENE_CMD_ANIMATED_MATERIAL_LIST(sMaterials, 1),\n"
        "    SCENE_CMD_END(),\n"
        "};\n");

    REQUIRE(written ==
            "SceneCmd header[7] = {\n"
            "    SCENE_CMD_ACTOR_LIST(2, &sActors[1]),\n"
            "    SCENE_CMD_SPECIAL_FILES(NAVI_QUEST_HINTS_OVERWORLD, OBJECT_GAMEPLAY_FIELD_KEEP),\n"
            "    SCENE_CMD_ROOM_BEHAVIOR(ROOM_TYPE_DUNGEON, 3, 1, 0),\n"
            "    SCENE_CMD_TIME_SETTINGS(255, 255, 10),\n"
            "    SCENE_CMD_MISC_SETTINGS(48, 5),\n"
            "    SCENE_CMD_ANIMATED_MATERIAL_LIST(sMaterials, 1),\n"
            "    SCENE_CMD_END(),\n"
            "};\n");
}

TEST_CASE("a command its macro cannot spell is written as its words", "[scene][writer]") {
    Harness harness;

    const std::string written = harness.writeCommands("SceneCmd header[] = {\n"
                                                      "    { SCENE_CMD_ID_LIGHT_LIST, 1, 0 },\n"
                                                      "    { SCENE_CMD_ID_END, 0, 7 },\n"
                                                      "    { 0x30, 0, 0 },\n"
                                                      "};\n");

    REQUIRE(written == "SceneCmd header[3] = {\n"
                       "    { 0x0C, 0x01, 0x00000000 },\n"
                       "    { 0x14, 0x00, 0x00000007 },\n"
                       "    { 0x30, 0x00, 0x00000000 },\n"
                       "};\n");
}

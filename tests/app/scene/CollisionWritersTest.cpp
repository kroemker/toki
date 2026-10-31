#include "app/scene/domain/CollisionWriters.h"

#include "support/WritingHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {

constexpr std::string_view Header =
    "typedef unsigned char u8;\n"
    "typedef short s16;\n"
    "typedef unsigned short u16;\n"
    "typedef unsigned int u32;\n"
    "typedef struct { s16 x, y, z; } Vec3s;\n"
    "#define NULL ((void*)0)\n"
    "#define SHT_MAX 32767.0f\n"
    "#define COLPOLY_SNORMAL(x) ((s16)((x) * SHT_MAX))\n"
    "#define COLPOLY_VTX_INDEX(vI) ((vI) & 0x1FFF)\n"
    "#define COLPOLY_VTX(vtxId, flags) ((((flags) & 7) << 13) | ((vtxId) & 0x1FFF))\n"
    "#define COLPOLY_IGNORE_NONE 0\n"
    "#define COLPOLY_IGNORE_CAMERA (1 << 0)\n"
    "#define COLPOLY_IGNORE_ENTITY (1 << 1)\n"
    "#define COLPOLY_IGNORE_PROJECTILES (1 << 2)\n"
    "#define COLPOLY_IS_FLOOR_CONVEYOR (1 << 0)\n"
    "#define WATERBOX_PROPERTIES(bgCamIndex, lightIndex, room, setFlag19) "
    "((((bgCamIndex) & 0xFF) << 0) | (((lightIndex) & 0x1F) << 8) | (((room) & 0x3F) << 13) | "
    "(((setFlag19) & 1) << 19))\n"
    "#define SURFACETYPE0(bgCamIndex, exitIndex, floorType, unk18, wallType, floorProperty, "
    "isSoft, isHorseBlocked) ((((bgCamIndex) & 0xFF) << 0) | (((exitIndex) & 0x1F) << 8) | "
    "(((floorType) & 0x1F) << 13) | (((unk18) & 0x07) << 18) | (((wallType) & 0x1F) << 21) | "
    "(((floorProperty) & 0x0F) << 26) | (((isSoft) & 1) << 30) | (((isHorseBlocked) & 1) << 31))\n"
    "#define SURFACETYPE1(material, floorEffect, lightSetting, echo, canHookshot, conveyorSpeed, "
    "conveyorDirection, unk27) ((((material) & 0x0F) << 0) | (((floorEffect) & 0x03) << 4) | "
    "(((lightSetting) & 0x1F) << 6) | (((echo) & 0x3F) << 11) | (((canHookshot) & 1) << 17) | "
    "(((conveyorSpeed) & 0x07) << 18) | (((conveyorDirection) & 0x3F) << 21) | "
    "(((unk27) & 1) << 27))\n"
    "typedef struct CollisionPoly { u16 type; union { u16 vtxData[3]; struct { u16 flags_vIA; "
    "u16 flags_vIB; u16 vIC; }; }; Vec3s normal; s16 dist; } CollisionPoly;\n"
    "typedef struct SurfaceType { u32 data[2]; } SurfaceType;\n"
    "typedef struct WaterBox { s16 xMin; s16 ySurface; s16 zMin; s16 xLength; s16 zLength; "
    "u32 properties; } WaterBox;\n"
    "typedef struct BgCamInfo { u16 setting; s16 count; Vec3s* bgCamFuncData; } BgCamInfo;\n"
    "typedef enum CameraSettingType { CAM_SET_NONE, CAM_SET_NORMAL0 } CameraSettingType;\n"
    "Vec3s camData[6];\n";

class Harness : public testing::WritingHarness {
public:
    Harness()
        : WritingHarness(Header,
                         {{.name = "camData",
                           .shape = {.type = "Vec3s", .isArray = true, .elementSize = 6}}}) {}

    std::string writeCollision(const std::string& source) {
        return writeAgain(
            source, makeWriter({{"CollisionPoly", toki::scene::makeCollisionPolyWriter()},
                                {"SurfaceType", toki::scene::makeSurfaceTypeWriter()},
                                {"WaterBox", toki::scene::makeWaterBoxWriter()},
                                {"BgCamInfo", toki::scene::makeBgCamInfoWriter(getTypes())}}));
    }
};

} // namespace

TEST_CASE("a collision polygon is written as fast64 writes it, its flags by name and its normal "
          "as the shortest fraction that truncates to it",
          "[scene][writer][collision]") {
    Harness harness;

    REQUIRE(harness.writeCollision("CollisionPoly polys[] = {\n"
                                   "    { 3, { 0x6001, 0x2002, 3 }, { 0x7FFF, -12345, 0 }, -45 },\n"
                                   "    { 0, { 4, 5, 6 }, { 1, -1, -32768 }, 13 },\n"
                                   "};\n") ==
            "CollisionPoly polys[2] = {\n"
            "    { 3, COLPOLY_VTX(1, COLPOLY_IGNORE_CAMERA | COLPOLY_IGNORE_ENTITY), "
            "COLPOLY_VTX(2, COLPOLY_IS_FLOOR_CONVEYOR), COLPOLY_VTX_INDEX(3), { "
            "COLPOLY_SNORMAL(1.0), COLPOLY_SNORMAL(-0.37677), COLPOLY_SNORMAL(0.0) }, -45 },\n"
            "    { 0, COLPOLY_VTX(4, COLPOLY_IGNORE_NONE), COLPOLY_VTX(5, COLPOLY_IGNORE_NONE), "
            "COLPOLY_VTX_INDEX(6), { COLPOLY_SNORMAL(0.00005), COLPOLY_SNORMAL(-0.00005), "
            "COLPOLY_SNORMAL(-1.00005) }, 13 },\n"
            "};\n");
}

TEST_CASE("a surface type is written as its two words' macros, and a waterbox's properties as "
          "theirs",
          "[scene][writer][collision]") {
    Harness harness;

    REQUIRE(harness.writeCollision("SurfaceType types[] = {\n"
                                   "    { SURFACETYPE0(2, 1, 5, 0, 3, 0, 1, 0), "
                                   "SURFACETYPE1(8, 1, 0, 2, 1, 3, 10, 0) },\n"
                                   "};\n") ==
            "SurfaceType types[1] = {\n"
            "    { SURFACETYPE0(2, 1, 5, 0, 3, 0, 1, 0), SURFACETYPE1(8, 1, 0, 2, 1, 3, 10, 0) },\n"
            "};\n");
    REQUIRE(harness.writeCollision("WaterBox boxes[] = {\n"
                                   "    { -150, 0, 19, 300, 300, 0x0007E000 },\n"
                                   "};\n") ==
            "WaterBox boxes[1] = {\n"
            "    { -150, 0, 19, 300, 300, WATERBOX_PROPERTIES(0, 0, 63, 0) },\n"
            "};\n");
}

TEST_CASE("a camera is written with its setting by name and its data as an address",
          "[scene][writer][collision]") {
    Harness harness;

    REQUIRE(harness.writeCollision("BgCamInfo cameras[] = {\n"
                                   "    { 1, 0, NULL },\n"
                                   "    { 7, 3, &camData[3] },\n"
                                   "};\n") == "BgCamInfo cameras[2] = {\n"
                                              "    { CAM_SET_NORMAL0, 0, NULL },\n"
                                              "    { 7, 3, &camData[3] },\n"
                                              "};\n");
}

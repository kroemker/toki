#include "CollisionForm.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/Scene.h"

#include <imgui.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

namespace toki::scene {

namespace {

constexpr float LabelWidth = 8.0F;
constexpr std::array<const char*, 3> KindNames{"Floor", "Wall", "Ceiling"};
constexpr u8 IgnoresCamera = 1U << 0;
constexpr u8 IgnoresEntities = 1U << 1;
constexpr u8 IgnoresProjectiles = 1U << 2;
constexpr u8 IsFloorConveyor = 1U << 0;
constexpr u32 WaterBoxFlag19 = 1U << 19;
constexpr f32 NormalOne = 32767.0F;
constexpr f32 DegreesPerConveyorStep = 360.0F / 64.0F;

void drawTitle(const char* icon, const std::string& name, const std::string& detail,
               const usize selected) {
    ImGui::TextColored(core::palette::Accent, "%s", icon);
    ImGui::SameLine();
    ImGui::TextUnformatted(name.c_str());
    ImGui::TextColored(core::palette::TextMuted, "%s", detail.c_str());
    if (selected > 1) {
        ImGui::TextColored(core::palette::Accent, "%zu selected; shown is the one picked last.",
                           selected);
    }
    ImGui::Spacing();
}

void drawRow(const std::string_view label, const std::string_view help, const std::string& value) {
    core::drawPropertyLabel(label, help);
    ImGui::TextUnformatted(value.c_str());
}

[[nodiscard]] std::string makeYesNo(const bool value) {
    return value ? "Yes" : "No";
}

[[nodiscard]] std::string makeIgnored(const u8 flags) {
    std::string ignored;
    for (const auto& [flag, name] :
         {std::pair{IgnoresCamera, "Camera"}, std::pair{IgnoresEntities, "Entities"},
          std::pair{IgnoresProjectiles, "Projectiles"}}) {
        if ((flags & flag) != 0) {
            ignored += ignored.empty() ? name : std::format(", {}", name);
        }
    }
    return ignored.empty() ? "Nothing" : ignored;
}

[[nodiscard]] std::string makeCamera(const Collision& collision, const u8 index) {
    if (index >= collision.cameras.size()) {
        return std::format("{} (not in the list)", index);
    }
    return std::format("{} · setting {}", index, collision.cameras[index].setting);
}

[[nodiscard]] std::string makeExit(const SceneLayer& layer, const u8 exitIndex) {
    if (exitIndex == 0) {
        return "None";
    }
    const usize index = exitIndex - 1U;
    if (index >= layer.header.exits.size()) {
        return std::format("{} (not in the exit list)", index);
    }
    return std::format("{} · entrance 0x{:04X}", index, layer.header.exits[index]);
}

void drawVertices(const Collision& collision, const CollisionPolygon& polygon) {
    for (usize corner = 0; corner < 3; ++corner) {
        const u16 index = getVertexIndex(polygon, corner);
        std::string value = std::format("#{}", index);
        if (index < collision.vertices.size()) {
            const std::array<s16, 3>& vertex = collision.vertices[index];
            value += std::format("  ({}, {}, {})", vertex[0], vertex[1], vertex[2]);
        }
        drawRow(std::format("Vertex {}", corner + 1),
                "Its index in the vertex list, and where it is.", value);
    }
}

void drawPolygonSection(const Collision& collision, const CollisionPolygon& polygon) {
    if (!ImGui::CollapsingHeader("Polygon", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##polygon", LabelWidth)) {
        return;
    }
    drawRow("Kind",
            "Which of the game's lists it is sorted into by its normal: a floor's is within "
            "60° of straight up, a ceiling's within about 37° of straight down, and a wall is "
            "the rest.",
            KindNames[static_cast<usize>(findPolygonKind(polygon))]);
    drawVertices(collision, polygon);
    drawRow("Ignores",
            "What passes through it, from the flags in its first vertex word "
            "(COLPOLY_IGNORE_*).",
            makeIgnored(getVertexFlags(polygon, 0)));
    drawRow("Is Floor Conveyor",
            "Whether a conveyor surface type moves what stands on it, from the flags in its "
            "second vertex word (COLPOLY_IS_FLOOR_CONVEYOR).",
            makeYesNo((getVertexFlags(polygon, 1) & IsFloorConveyor) != 0));
    drawRow("Normal", "The unit normal the polygon stores.",
            std::format("({:.3f}, {:.3f}, {:.3f})", polygon.normal[0] / NormalOne,
                        polygon.normal[1] / NormalOne, polygon.normal[2] / NormalOne));
    drawRow("Distance", "The plane's distance from the origin along the normal.",
            std::format("{}", polygon.distance));
    core::endProperties();
}

void drawSurfaceRows(const Collision& collision, const SceneLayer& layer,
                     const SurfaceFields& fields) {
    drawRow("Camera", "The index into the collision's camera list, and that camera's setting.",
            makeCamera(collision, fields.bgCamIndex));
    drawRow("Exit", "The index into the scene's exit list, and the entrance it leads to.",
            makeExit(layer, fields.exitIndex));
    drawRow("Floor Type",
            "SurfaceType_GetFloorType: what standing on it does, such as hurting or sinking.",
            std::format("{}", fields.floorType));
    drawRow("Floor Property",
            "SurfaceType_GetFloorProperty: what Link does at its edge or landing on it, such as "
            "voiding out or grabbing the ledge.",
            std::format("{}", fields.floorProperty));
    drawRow("Wall Type",
            "SurfaceType_GetWallType: what Link can do against it, such as climbing or crawling.",
            std::format("{}", fields.wallType));
    drawRow("Material", "SurfaceType_GetMaterial: the sound and effects of walking on it.",
            std::format("{}", fields.material));
    drawRow("Floor Effect", "SurfaceType_GetFloorEffect, which Player reads as Link walks on it.",
            std::format("{}", fields.floorEffect));
    drawRow("Light Setting",
            "SurfaceType_GetLightSetting: the light setting standing on it switches to.",
            std::format("{}", fields.lightSetting));
    drawRow("Echo", "SurfaceType_GetEcho: the echo of sounds made on it.",
            std::format("{}", fields.echo));
    drawRow("Hookshotable", "Whether the hookshot and longshot hold on to it.",
            makeYesNo(fields.canHookshot));
    drawRow("Soft",
            "SurfaceType_IsSoft: whether what stands on it sinks in, the ground check "
            "finding it one unit lower.",
            makeYesNo(fields.isSoft));
    drawRow("Horse Blocked", "SurfaceType_IsHorseBlocked: whether Epona cannot pass over it.",
            makeYesNo(fields.isHorseBlocked));
    drawRow("Conveyor Speed", "How fast it moves what stands on it; zero for not at all.",
            std::format("{}", fields.conveyorSpeed));
    drawRow("Conveyor Direction", "Which way it moves what stands on it.",
            std::format("{:.1f}°", fields.conveyorDirection * DegreesPerConveyorStep));
    drawRow("Unknown 18", "Bit 18 of the first word, which the game reads but does not name.",
            std::format("{}", fields.unk18));
    drawRow("Unknown 27", "Bit 27 of the second word, which the game reads but does not name.",
            makeYesNo(fields.unk27));
}

void drawSurfaceSection(const Collision& collision, const SceneLayer& layer, const u16 type) {
    if (!ImGui::CollapsingHeader("Surface Type", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##surface", LabelWidth)) {
        return;
    }
    drawRow("Index",
            "Which of the collision's surface types the polygon has. Polygons that "
            "share it are tinted.",
            std::format("{}", type));
    if (type >= collision.surfaceTypes.size()) {
        drawRow("Settings", "", "Past the end of the surface type list");
        core::endProperties();
        return;
    }
    const SurfaceType& surface = collision.surfaceTypes[type];
    drawSurfaceRows(collision, layer, readSurfaceFields(surface));
    drawRow("Words", "The surface type's two words, as the game reads them.",
            std::format("0x{:08X} 0x{:08X}", surface.data[0], surface.data[1]));
    core::endProperties();
}

} // namespace

void CollisionForm::drawPolygon(const Scene& scene, const CollisionPolygonRef& where,
                                const usize selected) const {
    const Collision* collision = findLayerCollision(scene, where.layer);
    if (collision == nullptr || where.polygon >= collision->polygons.size()) {
        return;
    }
    const SceneLayer& layer = scene.layers[where.layer];
    const CollisionPolygon& polygon = collision->polygons[where.polygon];
    drawTitle(core::icons::Triangle, std::format("Polygon {}", where.polygon),
              std::format("{} of {} · {}", KindNames[static_cast<usize>(findPolygonKind(polygon))],
                          collision->polygons.size(), makeLayerName(layer.index)),
              selected);
    drawPolygonSection(*collision, polygon);
    drawSurfaceSection(*collision, layer, polygon.type);
}

void CollisionForm::drawWaterBox(const Scene& scene, const WaterBoxRef& where,
                                 const usize selected) const {
    const Collision* collision = findLayerCollision(scene, where.layer);
    if (collision == nullptr || where.box >= collision->waterBoxes.size()) {
        return;
    }
    const WaterBox& box = collision->waterBoxes[where.box];
    drawTitle(core::icons::Waves, std::format("Waterbox {}", where.box),
              std::format("{} of {} · {}", where.box + 1, collision->waterBoxes.size(),
                          makeLayerName(scene.layers[where.layer].index)),
              selected);
    if (!ImGui::CollapsingHeader("Waterbox", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##waterBox", LabelWidth)) {
        return;
    }
    drawRow("X", "From its lowest X to its highest, in world units.",
            std::format("{} to {}", box.xMin, box.xMin + box.xLength));
    drawRow("Z", "From its lowest Z to its highest, in world units.",
            std::format("{} to {}", box.zMin, box.zMin + box.zLength));
    drawRow("Surface Y", "The height of the water's surface. It reaches down without end.",
            std::format("{}", box.ySurface));
    const u8 room = getWaterBoxRoom(box);
    drawRow("Room", "The room it is water in, while Link is in that room.",
            room == WaterBoxAllRooms ? std::string("All rooms") : std::format("{}", room));
    drawRow("Light Setting", "The light setting being in it switches to.",
            std::format("{}", getWaterBoxLightSetting(box)));
    drawRow("Camera", "The index into the collision's camera list, and that camera's setting.",
            makeCamera(*collision, getWaterBoxCamera(box)));
    drawRow("Flag 19", "Bit 19 of its properties, which the game reads but does not name.",
            makeYesNo((box.properties & WaterBoxFlag19) != 0));
    drawRow("Properties", "Its properties word, as the game reads it.",
            std::format("0x{:08X}", box.properties));
    core::endProperties();
}

} // namespace toki::scene

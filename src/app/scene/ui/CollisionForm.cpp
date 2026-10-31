#include "CollisionForm.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/HeaderNames.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SurfaceRules.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr float LabelWidth = 8.0F;
constexpr std::array<const char*, 3> KindNames{"Floor", "Wall", "Ceiling"};
constexpr f32 NormalOne = 32767.0F;
constexpr f32 DegreesPerConveyorStep = 360.0F / 64.0F;

/// A polygon flag as its row shows it.
struct FlagRow {
    PolygonFlag flag = PolygonFlag::IgnoreCamera;
    const char* label = "";
    const char* help = "";
};

constexpr std::array<FlagRow, 4> FlagRows{{
    {.flag = PolygonFlag::IgnoreCamera,
     .label = "Ignored by Camera",
     .help = "COLPOLY_IGNORE_CAMERA, in the first vertex word: the camera passes through it."},
    {.flag = PolygonFlag::IgnoreEntity,
     .label = "Ignored by Entities",
     .help = "COLPOLY_IGNORE_ENTITY, in the first vertex word: Link and actors pass through it."},
    {.flag = PolygonFlag::IgnoreProjectiles,
     .label = "Ignored by Projectiles",
     .help = "COLPOLY_IGNORE_PROJECTILES, in the first vertex word: arrows, seeds and the "
             "hookshot pass through it."},
    {.flag = PolygonFlag::Conveyor,
     .label = "Is Floor Conveyor",
     .help = "COLPOLY_IS_FLOOR_CONVEYOR, in the second vertex word: whether a conveyor surface "
             "type moves what stands on it."},
}};

void drawRow(const std::string_view label, const std::string_view help, const std::string& value) {
    core::drawPropertyLabel(label, help);
    ImGui::TextUnformatted(value.c_str());
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

[[nodiscard]] std::vector<NamedValue> makeExitValues(const SceneLayer& layer) {
    std::vector<NamedValue> values{{.value = 0, .label = "None"}};
    for (usize i = 0; i < layer.header.exits.size() && i < 0x1F; ++i) {
        values.push_back(
            {.value = static_cast<s64>(i + 1),
             .label = std::format("{} · entrance 0x{:04X}", i, layer.header.exits[i])});
    }
    return values;
}

[[nodiscard]] std::string_view findWarning(const IgnoredSettings& ignored,
                                           const SurfaceField field) {
    const auto found = ignored.find(field);
    return found != ignored.end() ? std::string_view(found->second) : std::string_view();
}

} // namespace

void CollisionForm::drawPolygons(const Scene& scene,
                                 const std::span<const CollisionPolygonRef> polygons,
                                 const bool isBound) {
    if (polygons.empty()) {
        return;
    }
    const CollisionPolygonRef& active = polygons.back();
    const Collision* collision = findLayerCollision(scene, active.layer);
    if (collision == nullptr || active.polygon >= collision->polygons.size()) {
        return;
    }
    const SceneLayer& layer = scene.layers[active.layer];
    PolygonSelection selection{.layer = active.layer, .gameLayer = layer.index};
    for (const CollisionPolygonRef& polygon : polygons) {
        if (polygon.polygon < collision->polygons.size()) {
            selection.polygons.push_back(polygon.polygon);
        }
    }
    if (CollisionRows::drawSharing(scene, active.layer)) {
        pick([gameLayer =
                  layer.index](SceneModel& model) { return makeOwnCollision(model, gameLayer); },
             std::nullopt);
    }
    drawFlagSection(*collision, selection, isBound);
    drawSurfaceSection(scene, layer, *collision, selection, isBound);
    drawPolygonSection(*collision, selection);
}

void CollisionForm::drawFlagSection(const Collision& collision, const PolygonSelection& selection,
                                    const bool isBound) {
    if (!ImGui::CollapsingHeader("Passes Through", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##flags", LabelWidth)) {
        return;
    }
    ImGui::BeginDisabled(isBound);
    for (const FlagRow& row : FlagRows) {
        const auto isSet = [&](const usize index) {
            return hasPolygonFlag(collision.polygons[index], row.flag);
        };
        const bool isMixed = std::ranges::any_of(selection.polygons, isSet) &&
                             !std::ranges::all_of(selection.polygons, isSet);
        const std::optional<std::string> ignored =
            findIgnoredFlag(collision, collision.polygons[selection.polygons.back()], row.flag);
        core::drawPropertyLabel(row.label,
                                isBound ? std::format("{}\n\nThe faces' material is bound, so "
                                                      "this is set by its binding.",
                                                      row.help)
                                        : std::string(row.help),
                                ignored.value_or(std::string()));
        if (const std::optional<bool> toggled = CollisionRows::drawToggle(
                std::format("##{}", row.label), isSet(selection.polygons.back()), isMixed)) {
            pick(
                [gameLayer = selection.gameLayer, polygons = selection.polygons, flag = row.flag,
                 isOn = *toggled](SceneModel& model) {
                    return setPolygonFlag(model, gameLayer, polygons, flag, isOn);
                },
                std::nullopt);
        }
    }
    ImGui::EndDisabled();
    core::endProperties();
}

void CollisionForm::drawPolygonSection(const Collision& collision,
                                       const PolygonSelection& selection) {
    const std::string header =
        selection.polygons.size() > 1
            ? std::format("Polygon {} of {} Picked###polygon", selection.polygons.back(),
                          selection.polygons.size())
            : std::format("Polygon {}###polygon", selection.polygons.back());
    if (!ImGui::CollapsingHeader(header.c_str()) ||
        !core::beginProperties("##polygon", LabelWidth)) {
        return;
    }
    const CollisionPolygon& polygon = collision.polygons[selection.polygons.back()];
    drawRow("Kind",
            "Which of the game's lists it is sorted into by its normal: a floor's is within "
            "60° of straight up, a ceiling's within about 37° of straight down, and a wall is "
            "the rest.",
            KindNames[static_cast<usize>(findPolygonKind(polygon))]);
    drawVertices(collision, polygon);
    drawRow("Normal", "The unit normal the polygon stores.",
            std::format("({:.3f}, {:.3f}, {:.3f})", polygon.normal[0] / NormalOne,
                        polygon.normal[1] / NormalOne, polygon.normal[2] / NormalOne));
    drawRow("Distance", "The plane's distance from the origin along the normal.",
            std::format("{}", polygon.distance));
    core::endProperties();
}

void CollisionForm::drawSurfaceSection(const Scene& scene, const SceneLayer& layer,
                                       const Collision& collision,
                                       const PolygonSelection& selection, const bool isBound) {
    if (!ImGui::CollapsingHeader("Collision Material Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const CollisionMaterial material =
        getPolygonMaterial(collision.polygons[selection.polygons.back()]);
    if (!std::ranges::all_of(selection.polygons, [&](const usize polygon) {
            return getPolygonMaterial(collision.polygons[polygon]) == material;
        })) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(core::palette::TextMuted, "%s",
                           "The picked faces are of several collision materials. Pick the faces "
                           "of one, under Materials in the Scene panel, to edit its settings.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (!core::beginProperties("##surface", LabelWidth)) {
        return;
    }
    drawOwnCopy(collision, selection, material.type, isBound);
    if (material.type < collision.surfaceTypes.size()) {
        drawSurfaceRows(scene, layer, collision, selection, material.type);
    }
    core::endProperties();
}

void CollisionForm::drawOwnCopy(const Collision& collision, const PolygonSelection& selection,
                                const u16 type, const bool isBound) {
    const std::vector<usize> counts = countSurfacePolygons(collision);
    const usize sharing = type < counts.size() ? counts[type] : 0;
    core::drawPropertyLabel(
        "Surface Type",
        std::format("The surface type the picked faces are made of, which {} polygons have, "
                    "tinted in the viewport. An edit of its settings below changes every polygon "
                    "of the same collision material, this surface type with the same flags, and "
                    "no other: one shared with other flags is copied first.",
                    sharing));
    ImGui::TextUnformatted(makeSurfaceTypeLabel(collisionNames, collision, type).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(isBound || type >= collision.surfaceTypes.size());
    if (ImGui::SmallButton("Own Copy")) {
        pick(
            [gameLayer = selection.gameLayer, polygons = selection.polygons,
             surface = collision.surfaceTypes[type]](
                SceneModel& model) -> std::expected<void, base::Diagnostic> {
                const std::expected<usize, base::Diagnostic> added =
                    addSurfaceType(model, gameLayer, surface);
                if (!added) {
                    return std::unexpected(added.error());
                }
                return setPolygonSurfaceType(model, gameLayer, polygons, static_cast<u16>(*added));
            },
            std::nullopt);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", isBound ? "The faces' material is bound, so their collision is "
                                          "set by its binding."
                                        : "Gives the picked faces a surface type of their own, "
                                          "copied from this one, so that editing it changes no "
                                          "other polygon.");
}

std::optional<s64> CollisionForm::drawNamedField(const std::string_view label,
                                                 const std::string_view help,
                                                 const SurfaceSetting setting, const s64 value,
                                                 const s64 maximum,
                                                 const std::string_view warning) {
    const SurfaceValueName* current =
        collisionNames != nullptr ? collisionNames->findName(setting, value) : nullptr;
    core::drawPropertyLabel(label,
                            current != nullptr && !current->tooltip.empty()
                                ? std::format("{}\n\n{}: {}", help, current->name, current->tooltip)
                                : std::string(help),
                            warning);
    std::vector<NamedValue> values;
    for (s64 i = 0; i <= maximum; ++i) {
        const SurfaceValueName* named =
            collisionNames != nullptr ? collisionNames->findName(setting, i) : nullptr;
        values.push_back(named != nullptr
                             ? NamedValue{.name = named->constant, .value = i, .label = named->name}
                             : NamedValue{.value = i, .label = std::format("{}", i)});
    }
    return collisionRows.drawNamedChoice(std::format("##{}", label), values, value);
}

void CollisionForm::pickSurfaceType(const Collision& collision, const PolygonSelection& selection,
                                    const SurfaceType& surface) {
    pick(
        [gameLayer = selection.gameLayer, collision,
         material = getPolygonMaterial(collision.polygons[selection.polygons.back()]),
         surface](SceneModel& model) -> std::expected<void, base::Diagnostic> {
            const std::expected<CollisionMaterial, base::Diagnostic> set =
                setMaterialSurface(model, gameLayer, collision, material, surface);
            if (!set) {
                return std::unexpected(set.error());
            }
            return {};
        },
        std::nullopt);
}

void CollisionForm::drawSurfaceRows(const Scene& scene, const SceneLayer& layer,
                                    const Collision& collision, const PolygonSelection& selection,
                                    const u16 type) {
    const SurfaceType& surface = collision.surfaceTypes[type];
    const SurfaceFields fields = readSurfaceFields(surface);
    const IgnoredSettings ignored = listIgnoredSettings(collision, type, collisionNames);
    SurfaceFields changed = drawSurfaceLinks(scene, layer, collision, selection, fields, ignored);
    changed = drawSurfaceNamedRows(changed, ignored);
    changed = drawSurfaceOtherRows(changed, ignored);
    if (changed != fields) {
        pickSurfaceType(collision, selection, makeSurfaceType(changed, surface));
    }
}

SurfaceFields CollisionForm::drawSurfaceLinks(const Scene& scene, const SceneLayer& layer,
                                              const Collision& collision,
                                              const PolygonSelection& selection,
                                              const SurfaceFields& fields,
                                              const IgnoredSettings& ignored) {
    SurfaceFields changed = fields;
    core::drawPropertyLabel("Camera",
                            "The index into the collision's camera list, which sets how the "
                            "camera follows Link on it.");
    if (const std::optional<u8> picked =
            collisionRows.drawCameraChoice(scene, collision, fields.bgCamIndex)) {
        changed.bgCamIndex = *picked;
    }
    if (fields.bgCamIndex < collision.cameras.size()) {
        core::drawPropertyLabel("Camera Setting",
                                "The setting of the camera it names, which every surface type "
                                "and waterbox naming that camera shares.");
        if (const std::optional<u16> setting = collisionRows.drawCameraSettingChoice(
                scene, collision.cameras[fields.bgCamIndex].setting)) {
            pick(
                [gameLayer = selection.gameLayer, camera = fields.bgCamIndex,
                 setting = *setting](SceneModel& model) {
                    return setCameraSetting(model, gameLayer, camera, setting);
                },
                std::nullopt);
        }
    }
    core::drawPropertyLabel("Exit",
                            "The scene's exit list entry Link leaves by on it, and the entrance "
                            "it leads to.",
                            findWarning(ignored, SurfaceField::Exit));
    if (const std::optional<s64> picked =
            collisionRows.drawNamedChoice("##exit", makeExitValues(layer), fields.exitIndex)) {
        changed.exitIndex = static_cast<u8>(*picked);
    }
    return changed;
}

SurfaceFields CollisionForm::drawSurfaceNamedRows(const SurfaceFields& fields,
                                                  const IgnoredSettings& ignored) {
    SurfaceFields changed = fields;
    if (const std::optional<s64> picked = drawNamedField(
            "Floor Type",
            "SurfaceType_GetFloorType: what standing on it does, such as hurting or sinking.",
            SurfaceSetting::FloorType, fields.floorType, 0x1F,
            findWarning(ignored, SurfaceField::FloorType))) {
        changed.floorType = static_cast<u8>(*picked);
    }
    if (const std::optional<s64> picked = drawNamedField(
            "Floor Property",
            "SurfaceType_GetFloorProperty: what Link does at its edge or landing on it.",
            SurfaceSetting::FloorProperty, fields.floorProperty, 0x0F,
            findWarning(ignored, SurfaceField::FloorProperty))) {
        changed.floorProperty = static_cast<u8>(*picked);
    }
    if (const std::optional<s64> picked = drawNamedField(
            "Wall Type", "SurfaceType_GetWallType: what Link can do against it, such as crawling.",
            SurfaceSetting::WallType, fields.wallType, 0x1F)) {
        changed.wallType = static_cast<u8>(*picked);
    }
    if (const std::optional<s64> picked = drawNamedField(
            "Material", "SurfaceType_GetMaterial: the sound and effects of walking on it.",
            SurfaceSetting::Material, fields.material, 0x0F)) {
        changed.material = static_cast<u8>(*picked);
    }
    if (const std::optional<s64> picked = drawNamedField(
            "Floor Effect", "SurfaceType_GetFloorEffect, which Player reads as Link walks on it.",
            SurfaceSetting::FloorEffect, fields.floorEffect, 0x03,
            findWarning(ignored, SurfaceField::FloorEffect))) {
        changed.floorEffect = static_cast<u8>(*picked);
    }
    return changed;
}

SurfaceFields CollisionForm::drawSurfaceOtherRows(const SurfaceFields& fields,
                                                  const IgnoredSettings& ignored) {
    SurfaceFields changed = fields;
    core::drawPropertyLabel(
        "Light Setting",
        "SurfaceType_GetLightSetting: the light setting standing on it switches to.");
    if (const std::optional<s64> typed =
            collisionRows.drawNumber("##lightSetting", fields.lightSetting, 0, 0x1F)) {
        changed.lightSetting = static_cast<u8>(*typed);
    }
    core::drawPropertyLabel("Echo", "SurfaceType_GetEcho: the echo of sounds made on it.");
    if (const std::optional<s64> typed = collisionRows.drawNumber("##echo", fields.echo, 0, 0x3F)) {
        changed.echo = static_cast<u8>(*typed);
    }
    core::drawPropertyLabel("Hookshotable", "Whether the hookshot and longshot hold on to it.");
    if (const std::optional<bool> toggled =
            CollisionRows::drawToggle("##hookshot", fields.canHookshot)) {
        changed.canHookshot = *toggled;
    }
    core::drawPropertyLabel("Soft",
                            "SurfaceType_IsSoft: whether what stands on it sinks in, the ground "
                            "check finding it one unit lower.");
    if (const std::optional<bool> toggled = CollisionRows::drawToggle("##soft", fields.isSoft)) {
        changed.isSoft = *toggled;
    }
    core::drawPropertyLabel("Horse Blocked",
                            "SurfaceType_IsHorseBlocked: whether Epona cannot pass over it.");
    if (const std::optional<bool> toggled =
            CollisionRows::drawToggle("##horse", fields.isHorseBlocked)) {
        changed.isHorseBlocked = *toggled;
    }
    if (const std::optional<s64> picked = drawNamedField(
            "Conveyor Speed", "How fast it moves what stands on it.", SurfaceSetting::ConveyorSpeed,
            fields.conveyorSpeed, 0x07, findWarning(ignored, SurfaceField::ConveyorSpeed))) {
        changed.conveyorSpeed = static_cast<u8>(*picked);
    }
    core::drawPropertyLabel(
        "Conveyor Direction",
        std::format("Which way it moves what stands on it, in 64ths of a turn: {:.1f}°.",
                    fields.conveyorDirection * DegreesPerConveyorStep),
        findWarning(ignored, SurfaceField::ConveyorDirection));
    if (const std::optional<s64> typed =
            collisionRows.drawNumber("##conveyorDirection", fields.conveyorDirection, 0, 0x3F)) {
        changed.conveyorDirection = static_cast<u8>(*typed);
    }
    core::drawPropertyLabel("Unknown 18",
                            "Bits 18 to 20 of the first word, which only func_80041D70 reads, "
                            "and nothing calls it. Every vanilla surface type leaves them 0.",
                            findWarning(ignored, SurfaceField::Unknown18));
    if (const std::optional<s64> typed = collisionRows.drawNumber("##unk18", fields.unk18, 0, 7)) {
        changed.unk18 = static_cast<u8>(*typed);
    }
    core::drawPropertyLabel(
        "Contact Damage",
        "Bit 27 of the second word, which func_80042108 reads. On a wall, Link takes damage "
        "on contact. On a floor of type 2 or 3, Link takes damage without catching fire.",
        findWarning(ignored, SurfaceField::ContactDamage));
    if (const std::optional<bool> toggled = CollisionRows::drawToggle("##unk27", fields.unk27)) {
        changed.unk27 = *toggled;
    }
    return changed;
}

void CollisionForm::drawSurfaceTypesMenu(const Scene& scene, const usize layer) {
    const Collision* collision = findLayerCollision(scene, layer);
    if (collision == nullptr) {
        return;
    }
    const usize gameLayer = scene.layers[layer].index;
    if (ImGui::MenuItem("Add Surface Type")) {
        pick(
            [gameLayer](SceneModel& model) -> std::expected<void, base::Diagnostic> {
                const std::expected<usize, base::Diagnostic> added =
                    addSurfaceType(model, gameLayer, SurfaceType{});
                if (!added) {
                    return std::unexpected(added.error());
                }
                return {};
            },
            std::nullopt);
    }
    const std::vector<usize> counts = countSurfacePolygons(*collision);
    const auto unused = static_cast<usize>(std::ranges::count(counts, usize{0}));
    if (ImGui::MenuItem(std::format("Remove {} Unused", unused).c_str(), nullptr, false,
                        unused > 0 && unused < counts.size())) {
        pick(
            [gameLayer](SceneModel& model) -> std::expected<void, base::Diagnostic> {
                const std::expected<usize, base::Diagnostic> removed =
                    removeUnusedSurfaceTypes(model, gameLayer);
                if (!removed) {
                    return std::unexpected(removed.error());
                }
                return {};
            },
            std::nullopt);
    }
    ImGui::SetItemTooltip("Removes every surface type no polygon has, renumbering the polygons' "
                          "surface types after them.");
}

} // namespace toki::scene

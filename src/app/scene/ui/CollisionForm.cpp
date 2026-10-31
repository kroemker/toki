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
    MaterialSelection selection{.layer = active.layer,
                                .gameLayer = scene.layers[active.layer].index,
                                .material = getPolygonMaterial(collision->polygons[active.polygon]),
                                .isBound = isBound};
    for (const CollisionPolygonRef& polygon : polygons) {
        if (polygon.polygon >= collision->polygons.size()) {
            continue;
        }
        if (getPolygonMaterial(collision->polygons[polygon.polygon]) != selection.material) {
            if (ImGui::CollapsingHeader("Collision Material Settings",
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextColored(core::palette::TextMuted, "%s",
                                   "The picked faces are of several collision materials. Select "
                                   "one under Materials in the Scene panel to edit its settings.");
                ImGui::PopTextWrapPos();
            }
            return;
        }
        selection.picked.push_back(polygon.polygon);
    }
    drawSection(scene, *collision, selection);
}

void CollisionForm::drawMaterial(const Scene& scene, const usize layer,
                                 const CollisionMaterial& material) {
    const Collision* collision = findLayerCollision(scene, layer);
    if (collision == nullptr) {
        return;
    }
    drawSection(scene, *collision,
                MaterialSelection{
                    .layer = layer, .gameLayer = scene.layers[layer].index, .material = material});
}

void CollisionForm::drawSection(const Scene& scene, const Collision& collision,
                                const MaterialSelection& selection) {
    if (!ImGui::CollapsingHeader("Collision Material Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (CollisionRows::drawSharing(scene, selection.layer)) {
        pick([gameLayer = selection.gameLayer](
                 SceneModel& model) { return makeOwnCollision(model, gameLayer); },
             std::nullopt);
    }
    if (!core::beginProperties("##collisionMaterial", LabelWidth)) {
        return;
    }
    drawSurfaceTypeRow(collision, selection);
    drawFlagRows(collision, selection);
    if (selection.material.type < collision.surfaceTypes.size()) {
        drawSurfaceRows(scene, scene.layers[selection.layer], collision, selection);
    }
    core::endProperties();
}

void CollisionForm::drawSurfaceTypeRow(const Collision& collision,
                                       const MaterialSelection& selection) {
    const u16 type = selection.material.type;
    const std::vector<usize> counts = countSurfacePolygons(collision);
    const usize sharing = type < counts.size() ? counts[type] : 0;
    core::drawPropertyLabel(
        "Surface Type",
        std::format("The surface type the material is of, which {} solid faces have, tinted in "
                    "the viewport. An edit of the settings below changes every solid face of "
                    "the material, and no other: a surface type shared with other flags is "
                    "copied first.",
                    sharing));
    ImGui::TextUnformatted(makeSurfaceTypeLabel(collisionNames, collision, type).c_str());
    if (selection.picked.empty()) {
        return;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selection.isBound || type >= collision.surfaceTypes.size());
    if (ImGui::SmallButton("Own Copy")) {
        pick(
            [this, gameLayer = selection.gameLayer, polygons = selection.picked,
             flags = selection.material.flags, surface = collision.surfaceTypes[type]](
                SceneModel& model) -> std::expected<void, base::Diagnostic> {
                const std::expected<usize, base::Diagnostic> added =
                    addSurfaceType(model, gameLayer, surface);
                if (!added) {
                    return std::unexpected(added.error());
                }
                const CollisionMaterial made{.type = static_cast<u16>(*added), .flags = flags};
                movedMaterial = made;
                return setPolygonSurfaceType(model, gameLayer, polygons, made.type);
            },
            std::nullopt);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", selection.isBound
                                    ? "The faces' render material is bound, so their collision "
                                      "is set by its binding."
                                    : "Gives the picked faces a collision material of their own, "
                                      "copied from this one, so that editing it changes no other "
                                      "solid face.");
}

void CollisionForm::drawFlagRows(const Collision& collision, const MaterialSelection& selection) {
    const auto first =
        std::ranges::find_if(collision.polygons, [&](const CollisionPolygon& polygon) {
            return getPolygonMaterial(polygon) == selection.material;
        });
    for (const FlagRow& row : FlagRows) {
        const std::optional<std::string> ignored =
            first != collision.polygons.end() ? findIgnoredFlag(collision, *first, row.flag)
                                              : std::nullopt;
        core::drawPropertyLabel(row.label,
                                std::format("{}\n\nWhat passes through is part of the material: "
                                            "a change sets it on every solid face made of it.",
                                            row.help),
                                ignored.value_or(std::string()));
        if (const std::optional<bool> toggled = CollisionRows::drawToggle(
                std::format("##{}", row.label), hasMaterialFlag(selection.material, row.flag))) {
            pick(
                [this, gameLayer = selection.gameLayer, collision, material = selection.material,
                 flags = setMaterialFlag(selection.material, row.flag, *toggled).flags](
                    SceneModel& model) -> std::expected<void, base::Diagnostic> {
                    const std::expected<CollisionMaterial, base::Diagnostic> set =
                        setMaterialFlags(model, gameLayer, collision, material, flags);
                    if (!set) {
                        return std::unexpected(set.error());
                    }
                    movedMaterial = *set;
                    return {};
                },
                std::nullopt);
        }
    }
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

void CollisionForm::pickSurfaceType(const Collision& collision, const MaterialSelection& selection,
                                    const SurfaceType& surface) {
    pick(
        [this, gameLayer = selection.gameLayer, collision, material = selection.material,
         surface](SceneModel& model) -> std::expected<void, base::Diagnostic> {
            const std::expected<CollisionMaterial, base::Diagnostic> set =
                setMaterialSurface(model, gameLayer, collision, material, surface);
            if (!set) {
                return std::unexpected(set.error());
            }
            movedMaterial = *set;
            return {};
        },
        std::nullopt);
}

void CollisionForm::drawSurfaceRows(const Scene& scene, const SceneLayer& layer,
                                    const Collision& collision,
                                    const MaterialSelection& selection) {
    const u16 type = selection.material.type;
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
                                              const MaterialSelection& selection,
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
    ImGui::SetItemTooltip("Removes every surface type no solid face has, renumbering the solid "
                          "faces' surface types after them.");
}

} // namespace toki::scene

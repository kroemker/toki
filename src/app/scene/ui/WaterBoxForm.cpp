#include "WaterBoxForm.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/HeaderNames.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"

#include <imgui.h>

#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr float LabelWidth = 8.0F;
constexpr s16 NewBoxHalfSize = 100;
constexpr s64 LowestCoordinate = std::numeric_limits<s16>::min();
constexpr s64 HighestCoordinate = std::numeric_limits<s16>::max();

[[nodiscard]] std::vector<NamedValue> makeRoomValues(const SceneLayer& layer) {
    std::vector<NamedValue> values{{.value = WaterBoxAllRooms, .label = "All Rooms"}};
    for (usize room = 0; room < layer.rooms.size() && room < WaterBoxAllRooms; ++room) {
        values.push_back({.value = static_cast<s64>(room), .label = std::format("Room {}", room)});
    }
    return values;
}

/// One of a waterbox's numbers as its row shows it, and where it is.
struct BoxNumber {
    const char* label = "";
    const char* help = "";
    s16 WaterBox::*member = nullptr;
    s64 minimum = LowestCoordinate;
};

constexpr std::array<BoxNumber, 5> BoxNumbers{{
    {.label = "X Min", .help = "Its lowest X, in world units.", .member = &WaterBox::xMin},
    {.label = "Z Min", .help = "Its lowest Z, in world units.", .member = &WaterBox::zMin},
    {.label = "X Length",
     .help = "How far it reaches along X from its lowest X.",
     .member = &WaterBox::xLength,
     .minimum = 0},
    {.label = "Z Length",
     .help = "How far it reaches along Z from its lowest Z.",
     .member = &WaterBox::zLength,
     .minimum = 0},
    {.label = "Surface Y",
     .help = "The height of the water's surface. It reaches down without end.",
     .member = &WaterBox::ySurface},
}};

} // namespace

void WaterBoxForm::draw(const Scene& scene, const WaterBoxRef& where, const usize selected) {
    const Collision* collision = findLayerCollision(scene, where.layer);
    if (collision == nullptr || where.box >= collision->waterBoxes.size()) {
        return;
    }
    const SceneLayer& layer = scene.layers[where.layer];
    const WaterBox& box = collision->waterBoxes[where.box];
    CollisionRows::drawTitle(core::icons::Waves, std::format("Waterbox {}", where.box),
                             std::format("{} of {} · {}", where.box + 1,
                                         collision->waterBoxes.size(), makeLayerName(layer.index)),
                             selected);
    if (CollisionRows::drawSharing(scene, where.layer)) {
        pick([gameLayer =
                  layer.index](SceneModel& model) { return makeOwnCollision(model, gameLayer); },
             std::nullopt);
    }
    if (!ImGui::CollapsingHeader("Waterbox", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##waterBox", LabelWidth)) {
        return;
    }
    WaterBox changed = box;
    for (const BoxNumber& number : BoxNumbers) {
        core::drawPropertyLabel(number.label, number.help);
        if (const std::optional<s64> typed =
                collisionRows.drawNumber(std::format("##{}", number.label), box.*number.member,
                                         number.minimum, HighestCoordinate)) {
            changed.*number.member = static_cast<s16>(*typed);
        }
    }
    const u8 room = getWaterBoxRoom(box);
    const u8 lightSetting = getWaterBoxLightSetting(box);
    const u8 camera = getWaterBoxCamera(box);
    const bool setsFlag19 = hasWaterBoxFlag19(box);
    core::drawPropertyLabel("Room", "The room it is water in, while Link is in that room.");
    if (const std::optional<s64> picked =
            collisionRows.drawNamedChoice("##room", makeRoomValues(layer), room)) {
        changed.properties = makeWaterBoxProperties(camera, lightSetting, static_cast<u8>(*picked),
                                                    setsFlag19, box.properties);
    }
    core::drawPropertyLabel("Light Setting", "The light setting being in it switches to.");
    if (const std::optional<s64> typed =
            collisionRows.drawNumber("##lightSetting", lightSetting, 0, 0x1F)) {
        changed.properties = makeWaterBoxProperties(camera, static_cast<u8>(*typed), room,
                                                    setsFlag19, box.properties);
    }
    core::drawPropertyLabel("Camera", "The index into the collision's camera list, which sets how "
                                      "the camera follows Link in it.");
    if (const std::optional<u8> picked =
            collisionRows.drawCameraChoice(scene, *collision, camera)) {
        changed.properties =
            makeWaterBoxProperties(*picked, lightSetting, room, setsFlag19, box.properties);
    }
    if (camera < collision->cameras.size()) {
        core::drawPropertyLabel("Camera Setting",
                                "The setting of the camera it names, which every surface type "
                                "and waterbox naming that camera shares.");
        if (const std::optional<u16> setting =
                collisionRows.drawCameraSettingChoice(scene, collision->cameras[camera].setting)) {
            pick(
                [gameLayer = layer.index, camera, setting = *setting](SceneModel& model) {
                    return setCameraSetting(model, gameLayer, camera, setting);
                },
                std::nullopt);
        }
    }
    core::drawPropertyLabel("Flag 19",
                            "Bit 19 of its properties, which the game reads but does not name.");
    if (const std::optional<bool> toggled = CollisionRows::drawToggle("##flag19", setsFlag19)) {
        changed.properties =
            makeWaterBoxProperties(camera, lightSetting, room, *toggled, box.properties);
    }
    core::endProperties();
    if (changed != box) {
        pickBox(scene, where, changed);
    }
}

void WaterBoxForm::drawMenu(const Scene& scene, const WaterBoxRef& where) {
    const Collision* collision = findLayerCollision(scene, where.layer);
    if (collision == nullptr || where.box >= collision->waterBoxes.size()) {
        return;
    }
    if (ImGui::MenuItem("Duplicate")) {
        pickAdd(scene, where.layer, collision->waterBoxes[where.box]);
    }
    if (ImGui::MenuItem("Delete")) {
        pick([gameLayer = scene.layers[where.layer].index,
              box = where.box](SceneModel& model) { return removeWaterBox(model, gameLayer, box); },
             std::nullopt);
    }
}

void WaterBoxForm::drawGroupMenu(const Scene& scene, const usize layer,
                                 const std::array<s16, 3>& position) {
    if (findLayerCollision(scene, layer) == nullptr || !ImGui::MenuItem("Add Waterbox")) {
        return;
    }
    pickAdd(scene, layer,
            WaterBox{.xMin = static_cast<s16>(position[0] - NewBoxHalfSize),
                     .ySurface = position[1],
                     .zMin = static_cast<s16>(position[2] - NewBoxHalfSize),
                     .xLength = NewBoxHalfSize * 2,
                     .zLength = NewBoxHalfSize * 2,
                     .properties = makeWaterBoxProperties(0, 0, WaterBoxAllRooms, false)});
}

void WaterBoxForm::pickBox(const Scene& scene, const WaterBoxRef& where, const WaterBox& box) {
    pick([gameLayer = scene.layers[where.layer].index, index = where.box,
          box](SceneModel& model) { return setWaterBox(model, gameLayer, index, box); },
         std::nullopt);
}

void WaterBoxForm::pickAdd(const Scene& scene, const usize layer, const WaterBox& box) {
    const Collision* collision = findLayerCollision(scene, layer);
    const usize index = collision != nullptr ? collision->waterBoxes.size() : 0;
    pick(
        [gameLayer = scene.layers[layer].index,
         box](SceneModel& model) -> std::expected<void, base::Diagnostic> {
            const std::expected<usize, base::Diagnostic> added = addWaterBox(model, gameLayer, box);
            if (!added) {
                return std::unexpected(added.error());
            }
            return {};
        },
        WaterBoxRef{.layer = layer, .box = index});
}

} // namespace toki::scene

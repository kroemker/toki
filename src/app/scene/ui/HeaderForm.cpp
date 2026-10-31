#include "HeaderForm.h"

#include "app/assets/domain/ObjectNames.h"
#include "app/core/ui/ConstantSelectable.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/HeaderNames.h"
#include "app/scene/domain/HeaderSettings.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

using enum HeaderSetting;
using Kind = HeaderForm::SettingKind;

constexpr float LabelWidth = 8.0F;
/// How many values a dropdown lists before it offers a filter.
constexpr usize FilteredValues = 12;

constexpr std::array<HeaderForm::SettingRow, 3> SoundRows{{
    {Sequence, "Music",
     "The sequence the scene plays, from SCENE_CMD_SOUND_SETTINGS. NA_BGM_NO_MUSIC plays none.",
     Kind::Named},
    {NatureAmbience, "Nature Ambience",
     "The ambient sounds the scene plays, such as birds by day and crickets by night.",
     Kind::Named},
    {SoundSpec, "Sound Spec", "Which of the audio heap's layouts the scene's sounds load into.",
     Kind::Number},
}};

constexpr std::array<HeaderForm::SettingRow, 3> SkyboxRows{{
    {Skybox, "Skybox", "What is drawn behind the scene, from SCENE_CMD_SKYBOX_SETTINGS.",
     Kind::Named},
    {SkyboxConfig, "Skybox Config",
     "Which of the skybox's textures it starts with, where the time of day picks the light.",
     Kind::Number},
    {LightMode, "Light Mode",
     "Whether the time of day picks the light setting, or the game sets one.", Kind::Named},
}};

constexpr std::array<HeaderForm::SettingRow, 2> SpecialFileRows{{
    {KeepObject, "Keep Object",
     "The object the game loads into segment 5 for the whole scene, such as "
     "OBJECT_GAMEPLAY_DANGEON_KEEP, from SCENE_CMD_SPECIAL_FILES.",
     Kind::Named},
    {NaviHints, "Navi Hints", "Which of Navi's quest hint files the scene loads.", Kind::Named},
}};

constexpr std::array<HeaderForm::SettingRow, 2> MiscRows{{
    {CameraType, "Camera Type",
     "How the camera behaves throughout the scene, from SCENE_CMD_MISC_SETTINGS.", Kind::Named},
    {MapLocation, "Map Location", "Where the pause screen's map marks Link as being.",
     Kind::Number},
}};

constexpr std::array<HeaderForm::SettingSection, 4> SceneSections{{
    {"Sound", SoundRows},
    {"Skybox", SkyboxRows},
    {"Special Files", SpecialFileRows},
    {"Camera and Map", MiscRows},
}};

constexpr std::array<HeaderForm::SettingRow, 4> BehaviorRows{{
    {RoomType, "Room Type",
     "What kind of room the game takes it for, from SCENE_CMD_ROOM_BEHAVIOR: a dungeon room "
     "blocks the Sun's Song, for one.",
     Kind::Named},
    {Environment, "Environment", "Heat, cold, or a stretch of the view, while Link is here.",
     Kind::Named},
    {LensMode, "Lens Mode",
     "Whether the actors the Lens of Truth reveals are hidden until it is used, or shown.",
     Kind::Named},
    {DisablesWarpSongs, "Disable Warp Songs", "Whether warp songs fail to play in the room.",
     Kind::Toggle},
}};

constexpr std::array<HeaderForm::SettingRow, 3> TimeRows{{
    {Hour, "Hour",
     "The hour set on entering the room, from SCENE_CMD_TIME_SETTINGS. 255, with the minute "
     "255 as well, leaves the time as it was.",
     Kind::Number},
    {Minute, "Minute", "The minute set on entering the room, or 255 to leave the time.",
     Kind::Number},
    {TimeSpeed, "Time Speed", "How fast time passes in the room. 0 stops it.", Kind::Number},
}};

constexpr std::array<HeaderForm::SettingRow, 2> SkyRows{{
    {DisablesSky, "Disable Sky",
     "Whether the skybox is hidden in the room, from SCENE_CMD_SKYBOX_DISABLES.", Kind::Toggle},
    {DisablesSunMoon, "Disable Sun and Moon", "Whether the sun and the moon are hidden.",
     Kind::Toggle},
}};

constexpr std::array<HeaderForm::SettingRow, 4> WindRows{{
    {WindX, "Direction X", "The wind's direction along X, from SCENE_CMD_WIND_SETTINGS.",
     Kind::Number},
    {WindY, "Direction Y", "The wind's direction along Y.", Kind::Number},
    {WindZ, "Direction Z", "The wind's direction along Z.", Kind::Number},
    {WindStrength, "Strength", "How strongly the wind blows. 0 is none.", Kind::Number},
}};

constexpr std::array<HeaderForm::SettingRow, 1> EchoRows{{
    {Echo, "Echo", "How much reverb sounds get in the room, from SCENE_CMD_ECHO_SETTINGS.",
     Kind::Number},
}};

constexpr std::array<HeaderForm::SettingSection, 5> RoomSections{{
    {"Behavior", BehaviorRows},
    {"Time", TimeRows},
    {"Sky", SkyRows},
    {"Wind", WindRows},
    {"Echo", EchoRows},
}};

[[nodiscard]] std::string makeObjectName(const hackeroot::Tables* tables, const s16 id) {
    const auto index = static_cast<usize>(static_cast<u16>(id));
    if (tables != nullptr && index < tables->objects.size() &&
        !tables->objects[index].enumName.empty()) {
        return assets::makeObjectLabel(tables->objects[index]);
    }
    return std::format("0x{:04X}", index);
}

void drawSelectedCount(const usize selected) {
    if (selected > 1) {
        ImGui::TextColored(core::palette::Accent, "%zu selected; shown is the one picked last.",
                           selected);
    }
}

} // namespace

void HeaderForm::drawScene(Scene& scene, const SceneRef& where, const usize selected) {
    if (where.layer >= scene.layers.size()) {
        return;
    }
    const SceneLayer& layer = scene.layers[where.layer];
    ImGui::TextColored(core::palette::Accent, "%s", core::icons::MapTrifold);
    ImGui::SameLine();
    ImGui::TextUnformatted(scene.name.c_str());
    ImGui::TextColored(core::palette::TextMuted, "%s",
                       std::format("Scene · {}", makeLayerName(layer.index)).c_str());
    drawSelectedCount(selected);
    ImGui::Spacing();
    const HeaderRef header{.layer = layer.index};
    for (const SettingSection& section : SceneSections) {
        drawSection(section, layer.header.commands, scene.headerNames, header);
    }
    if (const Collision* collision = findLayerCollision(scene, where.layer);
        collision != nullptr && ImGui::CollapsingHeader("Collision") &&
        core::beginProperties("##collision", LabelWidth)) {
        core::drawPropertyLabel("Polygons", "The polygons of the layer's collision, of its solid "
                                            "faces and of none.");
        ImGui::Text("%zu", collision->polygons.size());
        core::drawPropertyLabel("Vertices", "The vertices its polygons have.");
        ImGui::Text("%zu", collision->vertices.size());
        core::drawPropertyLabel("Bounds", "The box the game culls the collision by, as it "
                                          "stores it.");
        ImGui::Text("%d, %d, %d to %d, %d, %d", collision->minBounds[0], collision->minBounds[1],
                    collision->minBounds[2], collision->maxBounds[0], collision->maxBounds[1],
                    collision->maxBounds[2]);
        core::endProperties();
    }
    applyPending(scene);
}

void HeaderForm::drawRoom(Scene& scene, const RoomRef& where, const usize selected) {
    if (where.layer >= scene.layers.size() ||
        where.room >= scene.layers[where.layer].rooms.size()) {
        return;
    }
    const SceneLayer& layer = scene.layers[where.layer];
    const RoomHeader& header = layer.rooms[where.room];
    ImGui::TextColored(core::palette::Accent, "%s", core::icons::Square);
    ImGui::SameLine();
    ImGui::TextUnformatted(std::format("Room {}", where.room).c_str());
    ImGui::TextColored(core::palette::TextMuted, "%s", makeLayerName(layer.index).c_str());
    drawSelectedCount(selected);
    if (layer.index != 0 && header.address == scene.layers.front().rooms[where.room].address) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(core::palette::Warning, "%s",
                           "The room has no header of its own in this layer, so the game reads "
                           "its main header here, and an edit changes that.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Room", ImGuiTreeNodeFlags_DefaultOpen) &&
        core::beginProperties("##room", LabelWidth)) {
        constexpr std::array<const char*, 3> ShapeNames{"Normal", "Image", "Cullable"};
        core::drawPropertyLabel("Shape", "How the room's mesh is drawn, from its "
                                         "SCENE_CMD_ROOM_SHAPE: every part, a prerendered "
                                         "image, or the parts the camera sees.");
        ImGui::TextUnformatted(ShapeNames[static_cast<usize>(header.shapeType)]);
        core::drawPropertyLabel("Parts", "The display list pairs of the room's mesh.");
        ImGui::Text("%zu", header.displayLists.size());
        core::drawPropertyLabel("Actors", "The actors the room places in this layer.");
        ImGui::Text("%zu", header.actors.size());
        core::endProperties();
    }
    const HeaderRef ref{.layer = layer.index, .room = where.room};
    for (const SettingSection& section : RoomSections) {
        drawSection(section, header.commands, scene.headerNames, ref);
    }
    drawObjects(header, ref);
    applyPending(scene);
}

void HeaderForm::drawSection(const SettingSection& section,
                             const std::span<const HeaderCommand> commands,
                             const HeaderNames& names, const HeaderRef& where) {
    if (!ImGui::CollapsingHeader(section.title, ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const HeaderSetting first = section.rows.front().setting;
    if (!findSetting(commands, first)) {
        const std::string_view macro = getCommandMacro(getSettingCommand(first));
        ImGui::TextColored(core::palette::TextMuted, "The header has no %.*s.",
                           static_cast<int>(macro.size()), macro.data());
        ImGui::SameLine();
        ImGui::PushID(section.title);
        if (ImGui::SmallButton("Add")) {
            pending = [where, first](SceneModel& model) {
                return addSettingCommand(model, where, first);
            };
        }
        ImGui::PopID();
        return;
    }
    if (!core::beginProperties(std::format("##{}", section.title).c_str(), LabelWidth)) {
        return;
    }
    for (const SettingRow& row : section.rows) {
        drawSetting(row, findSetting(commands, row.setting).value_or(0), names, where);
    }
    core::endProperties();
}

void HeaderForm::drawSetting(const SettingRow& row, const s64 value, const HeaderNames& names,
                             const HeaderRef& where) {
    core::drawPropertyLabel(row.label, row.help);
    ImGui::PushID(static_cast<int>(row.setting));
    switch (row.kind) {
    case SettingKind::Toggle: {
        bool isOn = value != 0;
        if (ImGui::Checkbox("##toggle", &isOn)) {
            setSetting(where, row.setting, isOn ? 1 : 0);
        }
        break;
    }
    case SettingKind::Named:
        if (const std::span<const NamedValue> values = names.findValues(row.setting);
            !values.empty()) {
            drawNamed(row, value, values, where);
            break;
        }
        drawNumber(row, value, where);
        break;
    case SettingKind::Number:
        drawNumber(row, value, where);
        break;
    }
    ImGui::PopID();
}

void HeaderForm::drawNumber(const SettingRow& row, const s64 value, const HeaderRef& where) {
    s64 shown = held && held->setting == row.setting ? held->value : value;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputScalar("##number", ImGuiDataType_S64, &shown);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const SettingRange range = getSettingRange(row.setting);
        setSetting(where, row.setting, std::clamp(shown, range.minimum, range.maximum));
        held.reset();
    } else if (ImGui::IsItemActive()) {
        held = HeldValue{.setting = row.setting, .value = shown};
    } else if (held && held->setting == row.setting) {
        held.reset();
    }
}

void HeaderForm::drawNamed(const SettingRow& row, const s64 value,
                           const std::span<const NamedValue> values, const HeaderRef& where) {
    const auto current = std::ranges::find(values, value, &NamedValue::value);
    const std::string label =
        current != values.end() ? current->label : std::format("{} (unnamed)", value);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo("##named", label.c_str(), ImGuiComboFlags_HeightLarge)) {
        return;
    }
    const bool isFiltered = values.size() > FilteredValues;
    if (isFiltered) {
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Filter", &filter);
    }
    for (usize i = 0; i < values.size(); ++i) {
        const NamedValue& named = values[i];
        if (isFiltered && !core::isConstantFilteredIn(named.label, named.name, filter)) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));
        if (core::drawConstantSelectable(named.label, named.name, named.value == value) &&
            named.value != value) {
            setSetting(where, row.setting, named.value);
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

void HeaderForm::drawObjects(const RoomHeader& header, const HeaderRef& where) {
    if (!ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    const hackeroot::Tables* tables = workspace != nullptr ? &workspace->getTables() : nullptr;
    if (header.objects.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "The room loads no objects of its own.");
    }
    for (usize i = 0; i < header.objects.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        drawObjectRow(header.objects, i, tables, where);
        ImGui::PopID();
    }
    if (tables != nullptr) {
        drawObjectPicker(header.objects, *tables, where);
    }
}

void HeaderForm::drawObjectRow(const std::vector<s16>& objects, const usize index,
                               const hackeroot::Tables* tables, const HeaderRef& where) {
    const float buttons = ImGui::GetFrameHeight() * 3.0F + ImGui::GetStyle().ItemSpacing.x * 2.0F;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(makeObjectName(tables, objects[index]).c_str());
    if (const auto object = static_cast<usize>(static_cast<u16>(objects[index]));
        tables != nullptr && object < tables->objects.size()) {
        ImGui::SetItemTooltip("%s", tables->objects[object].enumName.c_str());
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons);
    ImGui::BeginDisabled(index == 0);
    if (ImGui::SmallButton(core::icons::ArrowUp)) {
        std::vector<s16> moved = objects;
        std::swap(moved[index - 1], moved[index]);
        setObjects(where, std::move(moved));
    }
    ImGui::SetItemTooltip("Load it earlier.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 == objects.size());
    if (ImGui::SmallButton(core::icons::ArrowDown)) {
        std::vector<s16> moved = objects;
        std::swap(moved[index], moved[index + 1]);
        setObjects(where, std::move(moved));
    }
    ImGui::SetItemTooltip("Load it later.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton(core::icons::X)) {
        std::vector<s16> removed = objects;
        removed.erase(removed.begin() + static_cast<std::ptrdiff_t>(index));
        setObjects(where, std::move(removed));
    }
    ImGui::SetItemTooltip(
        "Remove it from the list. An actor whose object is not loaded is not spawned.");
}

void HeaderForm::drawObjectPicker(const std::vector<s16>& objects, const hackeroot::Tables& tables,
                                  const HeaderRef& where) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo("##addObject", "Add an object", ImGuiComboFlags_HeightLarge)) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        filter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter", &filter);
    for (const hackeroot::ObjectEntry& object : tables.objects) {
        const auto id = static_cast<s16>(object.id);
        const std::string label = assets::makeObjectLabel(object);
        if (object.isUnset || std::ranges::contains(objects, id) ||
            !core::isConstantFilteredIn(label, object.enumName, filter)) {
            continue;
        }
        ImGui::PushID(static_cast<int>(object.id));
        if (core::drawConstantSelectable(label, object.enumName, false)) {
            std::vector<s16> added = objects;
            added.push_back(id);
            setObjects(where, std::move(added));
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

void HeaderForm::setSetting(const HeaderRef& where, const HeaderSetting setting, const s64 value) {
    pending = [where, setting, value](SceneModel& model) {
        return setHeaderSetting(model, where, setting, value);
    };
}

void HeaderForm::setObjects(const HeaderRef& where, std::vector<s16> objects) {
    pending = [where, objects = std::move(objects)](SceneModel& model) {
        return setRoomObjects(model, where.layer, *where.room, objects);
    };
}

void HeaderForm::applyPending(Scene& scene) {
    if (pending) {
        apply(scene, std::exchange(pending, nullptr));
    }
}

} // namespace toki::scene

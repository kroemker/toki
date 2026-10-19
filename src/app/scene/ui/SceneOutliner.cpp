#include "SceneOutliner.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/ActorDescription.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneView.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

constexpr float InfoWidth = 6.0F;
constexpr float VisibilityWidth = 1.6F;
constexpr ImGuiTreeNodeFlags GroupFlags =
    ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_AllowOverlap |
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
constexpr ImGuiTreeNodeFlags LeafFlags = ImGuiTreeNodeFlags_SpanAllColumns |
                                         ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Leaf |
                                         ImGuiTreeNodeFlags_NoTreePushOnOpen;
constexpr ImVec4 RoomColor{0.498F, 0.706F, 1.0F, 1.0F};
constexpr ImVec4 DoorColor{0.788F, 0.612F, 1.0F, 1.0F};
constexpr ImVec4 EntranceColor{1.0F, 0.549F, 0.420F, 1.0F};

[[nodiscard]] bool containsIgnoringCase(const std::string_view text, const std::string_view part) {
    const auto lower = [](const char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    };
    return part.empty() || std::ranges::search(text, part, {}, lower, lower).begin() != text.end();
}

/// Starts a row whose name cell is a node without a label, so its menu and
/// tooltip attach to the node; `drawLabel` draws the label after them. True
/// when the node is open.
bool beginNode(const char* id, const ImGuiTreeNodeFlags flags) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    return ImGui::TreeNodeEx(id, flags, "%s", "");
}

/// `icon` in `color`, then `name`, muted when `isDimmed`.
void drawLabel(const char* icon, const ImVec4& color, const std::string& name,
               const bool isDimmed = false) {
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::TextColored(isDimmed ? core::palette::TextDisabled : color, "%s", icon);
    ImGui::SameLine();
    ImGui::TextColored(isDimmed ? core::palette::TextDisabled : core::palette::Text, "%s",
                       name.c_str());
}

bool drawNode(const char* id, const ImGuiTreeNodeFlags flags, const char* icon, const ImVec4& color,
              const std::string& name) {
    const bool isOpen = beginNode(id, flags);
    drawLabel(icon, color, name);
    return isOpen;
}

void drawInfo(const std::string& info) {
    ImGui::TableNextColumn();
    ImGui::TextColored(core::palette::TextMuted, "%s", info.c_str());
}

/// A kind of the scene Toki does not read yet: a row that says so.
void drawUnread(const char* icon, const char* name, const char* help) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TreeNodeEx(name, LeafFlags, "%s", "");
    ImGui::SetItemTooltip("%s", help);
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::TextColored(core::palette::TextDisabled, "%s %s", icon, name);
    drawInfo("not read");
}

} // namespace

void SceneOutliner::draw(const Scene& scene, SceneView& view, const hackeroot::Tables* tables) {
    const std::optional<PlacementRef> selected =
        hooks.findSelected ? hooks.findSelected() : std::nullopt;
    revealing = selected != revealed ? selected : std::nullopt;
    revealed = selected;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter actors", &filter);
    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
    if (!ImGui::BeginTable("##outliner", 3, Flags)) {
        return;
    }
    const float font = ImGui::GetFontSize();
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthFixed, font * InfoWidth);
    ImGui::TableSetupColumn("shown", ImGuiTableColumnFlags_WidthFixed, font * VisibilityWidth);
    const SceneLayer& layer = scene.layers[view.layer];
    if (drawNode("##scene", GroupFlags | ImGuiTreeNodeFlags_DefaultOpen, core::icons::MapTrifold,
                 RoomColor, scene.name)) {
        drawInfo(std::format("{} rooms", layer.rooms.size()));
        drawRoomsShown(view);
        for (usize room = 0; room < layer.rooms.size(); ++room) {
            drawRoom(scene, layer, view, room, tables);
        }
        drawTransitionActors(scene, layer, view);
        drawEntrances(scene, layer, view);
        drawLightSettings(layer, view);
        drawUnread(core::icons::Polygon, "Collision",
                   "Toki does not read collision yet. A save writes it back as it was.");
        drawUnread(core::icons::Path, "Paths",
                   "Toki does not read paths yet. A save writes them back as they were.");
        drawUnread(core::icons::FilmSlate, "Cutscenes",
                   "Toki does not read cutscenes yet. A save writes them back as raw words.");
        drawUnread(core::icons::SignOut, "Exits",
                   "Toki does not read the exit list yet. A save writes it back as it was.");
        ImGui::TreePop();
    } else {
        drawInfo(std::format("{} rooms", layer.rooms.size()));
        drawRoomsShown(view);
    }
    ImGui::EndTable();
}

void SceneOutliner::drawRoomsShown(SceneView& view) const {
    ImGui::TableNextColumn();
    const bool isAnyHidden = std::ranges::contains(view.hiddenRooms, true);
    if (ImGui::SmallButton(isAnyHidden ? core::icons::EyeSlash : core::icons::Eye)) {
        std::ranges::fill(view.hiddenRooms, !isAnyHidden);
        if (hooks.showScene) {
            hooks.showScene();
        }
    }
    ImGui::SetItemTooltip(isAnyHidden ? "Some rooms are hidden. Click to show all rooms."
                                      : "Click to hide all rooms.");
}

void SceneOutliner::drawRoom(const Scene& scene, const SceneLayer& layer, SceneView& view,
                             const usize room, const hackeroot::Tables* tables) {
    const RoomHeader& header = layer.rooms[room];
    const bool isHidden = room < view.hiddenRooms.size() && view.hiddenRooms[room];
    const auto shown = std::ranges::count_if(
        header.actors, [this](const ActorPlacement& actor) { return isFilteredIn(actor.id); });
    if (!filter.empty() && shown == 0) {
        return;
    }
    ImGui::PushID(static_cast<int>(room));
    if (!filter.empty() || isRevealing(PlacementKind::Actor, room)) {
        ImGui::SetNextItemOpen(true);
    }
    ImGuiTreeNodeFlags flags = GroupFlags | (room == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    if (hooks.isSelected && hooks.isSelected(RoomRef{.layer = view.layer, .room = room})) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const bool isOpen = beginNode("##room", flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && hooks.pickRoom) {
        hooks.pickRoom(room);
    }
    if (hooks.drawRoomMenu) {
        hooks.drawRoomMenu(room, header);
    }
    drawLabel(core::icons::Square, RoomColor, std::format("Room {}", room), isHidden);
    drawInfo(std::format("{}", shown));
    ImGui::TableNextColumn();
    if (ImGui::SmallButton(isHidden ? core::icons::EyeSlash : core::icons::Eye) &&
        room < view.hiddenRooms.size()) {
        view.hiddenRooms[room] = !isHidden;
        if (hooks.showScene) {
            hooks.showScene();
        }
    }
    ImGui::SetItemTooltip(isHidden ? "Hidden. Click to show the room." : "Click to hide the room.");
    if (isOpen) {
        const std::vector<std::string> loaded = tables != nullptr
                                                    ? listLoadedObjects(layer, room, *tables)
                                                    : std::vector<std::string>{};
        for (usize i = 0; i < header.actors.size(); ++i) {
            const ActorPlacement& actor = header.actors[i];
            if (!isFilteredIn(actor.id)) {
                continue;
            }
            drawPlacement(PlacementRow{
                .where =
                    {.kind = PlacementKind::Actor, .layer = view.layer, .room = room, .index = i},
                .actorId = actor.id,
                .position = actor.position,
                .values = makePlacementValues(scene.actors, static_cast<u16>(actor.params),
                                              actor.rotation, findLayerAge(layer.index)),
                .loadedObjects = tables != nullptr
                                     ? std::optional<std::span<const std::string>>(loaded)
                                     : std::nullopt,
            });
        }
        if (filter.empty()) {
            drawObjects(header, tables);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void SceneOutliner::drawObjects(const RoomHeader& header, const hackeroot::Tables* tables) {
    const bool isOpen = beginNode("##objects", GroupFlags);
    ImGui::SetItemTooltip("The objects the room's object list loads in this layer.");
    drawLabel(core::icons::Package, core::palette::TextMuted, "Objects");
    drawInfo(std::format("{}", header.objects.size()));
    if (!isOpen) {
        return;
    }
    for (usize i = 0; i < header.objects.size(); ++i) {
        const auto id = static_cast<usize>(static_cast<u16>(header.objects[i]));
        const std::string name = tables != nullptr && id < tables->objects.size()
                                     ? tables->objects[id].enumName
                                     : std::format("0x{:04X}", id);
        ImGui::PushID(static_cast<int>(i));
        drawNode("##object", LeafFlags, core::icons::Package, core::palette::TextMuted, name);
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawTransitionActors(const Scene& scene, const SceneLayer& layer,
                                         SceneView& view) {
    const std::vector<TransitionActorPlacement>& actors = layer.header.transitionActors;
    const auto shown = std::ranges::count_if(
        actors, [this](const TransitionActorPlacement& actor) { return isFilteredIn(actor.id); });
    if (!filter.empty() && shown == 0) {
        return;
    }
    if (!filter.empty() || isRevealing(PlacementKind::TransitionActor, std::nullopt)) {
        ImGui::SetNextItemOpen(true);
    }
    const bool isOpen = drawNode("##transitionActors", GroupFlags, core::icons::Door, DoorColor,
                                 "Transition Actors");
    drawInfo(std::format("{}", shown));
    if (!isOpen) {
        return;
    }
    for (usize i = 0; i < actors.size(); ++i) {
        const TransitionActorPlacement& actor = actors[i];
        if (!isFilteredIn(actor.id)) {
            continue;
        }
        drawPlacement(PlacementRow{
            .where = {.kind = PlacementKind::TransitionActor, .layer = view.layer, .index = i},
            .actorId = actor.id,
            .position = actor.position,
            .values = makePlacementValues(scene.actors, static_cast<u16>(actor.params),
                                          {0, actor.rotationY, 0}, findLayerAge(layer.index)),
        });
    }
    ImGui::TreePop();
}

void SceneOutliner::drawEntrances(const Scene& scene, const SceneLayer& layer, SceneView& view) {
    if (!filter.empty()) {
        return;
    }
    if (isRevealing(PlacementKind::PlayerEntry, std::nullopt)) {
        ImGui::SetNextItemOpen(true);
    }
    const std::vector<ActorPlacement>& entries = layer.header.playerEntries;
    const bool isOpen =
        drawNode("##entrances", GroupFlags, core::icons::FlagBanner, EntranceColor, "Entrances");
    drawInfo(std::format("{}", entries.size()));
    if (!isOpen) {
        return;
    }
    for (usize i = 0; i < entries.size(); ++i) {
        const ActorPlacement& entry = entries[i];
        drawPlacement(PlacementRow{
            .where = {.kind = PlacementKind::PlayerEntry, .layer = view.layer, .index = i},
            .actorId = entry.id,
            .position = entry.position,
            .values = makePlacementValues(scene.actors, static_cast<u16>(entry.params),
                                          entry.rotation, findLayerAge(layer.index)),
            .label = std::format("Entry {}{}", i, i == view.shownEntrance ? "  ·  Link" : ""),
        });
    }
    ImGui::TreePop();
}

void SceneOutliner::drawLightSettings(const SceneLayer& layer, SceneView& view) {
    if (!filter.empty()) {
        return;
    }
    const std::vector<LightSetting>& settings = layer.header.lightSettings;
    const bool isOpen = drawNode("##lights", GroupFlags, core::icons::Sun, core::palette::Warning,
                                 "Light Settings");
    drawInfo(std::format("{}", settings.size()));
    if (!isOpen) {
        return;
    }
    for (usize i = 0; i < settings.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const ImGuiTreeNodeFlags flags =
            LeafFlags | (i == view.lightSetting ? ImGuiTreeNodeFlags_Selected : 0);
        ImGui::TreeNodeEx("##light", flags, "%s", "");
        if (ImGui::IsItemClicked() && i != view.lightSetting) {
            view.lightSetting = i;
            if (hooks.showScene) {
                hooks.showScene();
            }
        }
        ImGui::SetItemTooltip("Click to light the scene with it.");
        ImGui::SameLine(0.0F, 0.0F);
        const std::array<u8, 3>& ambient = settings[i].ambientColor;
        ImGui::ColorButton(
            "##ambient",
            ImVec4(ambient[0] / 255.0F, ambient[1] / 255.0F, ambient[2] / 255.0F, 1.0F),
            ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
        ImGui::SameLine();
        ImGui::TextUnformatted(std::format("Setting {}", i).c_str());
        drawInfo(i == view.lightSetting ? "shown" : "");
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawPlacement(const PlacementRow& row) {
    ImGui::PushID(static_cast<int>(row.where.index));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (hooks.drawPlacement) {
        hooks.drawPlacement(row);
    }
    if (revealing && *revealing == row.where) {
        ImGui::SetScrollHereY();
    }
    drawInfo(std::format("{:04X}", row.values.params));
    ImGui::PopID();
}

bool SceneOutliner::isFilteredIn(const s16 actorId) const {
    return filter.empty() ||
           (hooks.findName && containsIgnoringCase(hooks.findName(actorId), filter));
}

bool SceneOutliner::isRevealing(const PlacementKind kind, const std::optional<usize> room) const {
    return revealing && revealing->kind == kind && (!room || revealing->room == *room);
}

} // namespace toki::scene

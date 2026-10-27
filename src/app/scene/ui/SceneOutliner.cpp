#include "SceneOutliner.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/ActorDescription.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionDrawing.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/HeaderNames.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/domain/SceneView.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <span>
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
constexpr ImVec4 PathColor{0.851F, 0.424F, 0.878F, 1.0F};
constexpr ImVec4 CollisionColor{0.878F, 0.627F, 0.251F, 1.0F};
constexpr ImVec4 WaterColor{0.314F, 0.612F, 1.0F, 1.0F};
constexpr ImVec4 MaterialColor{0.565F, 0.792F, 0.494F, 1.0F};

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

/// A square of `color` in place of a row's icon, as what it is drawn in.
void drawSwatch(const std::array<u8, 3>& color) {
    constexpr f32 Inset = 0.2F;
    constexpr f32 Rounding = 2.0F;
    ImGui::SameLine(0.0F, 0.0F);
    const f32 size = ImGui::GetTextLineHeight();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(corner.x + (size * Inset), corner.y + (size * Inset)),
        ImVec2(corner.x + (size * (1.0F - Inset)), corner.y + (size * (1.0F - Inset))),
        IM_COL32(color[0], color[1], color[2], 0xFF), Rounding);
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
    revealingSurface =
        view.tintedSurfaceType != revealedSurface ? view.tintedSurfaceType : std::nullopt;
    revealedSurface = view.tintedSurfaceType;
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
    ImGuiTreeNodeFlags sceneFlags = GroupFlags | ImGuiTreeNodeFlags_DefaultOpen;
    if (hooks.isSelected && hooks.isSelected(SceneRef{.layer = view.layer})) {
        sceneFlags |= ImGuiTreeNodeFlags_Selected;
    }
    const bool isSceneOpen = beginNode("##scene", sceneFlags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && hooks.pickScene) {
        hooks.pickScene();
    }
    ImGui::SetItemTooltip("The scene's settings: its sound, skybox, keep object and camera.");
    drawLabel(core::icons::MapTrifold, RoomColor, scene.name);
    if (isSceneOpen) {
        drawInfo(std::format("{} rooms", layer.rooms.size()));
        drawRoomsShown(view);
        for (usize room = 0; room < layer.rooms.size(); ++room) {
            drawRoom(scene, layer, view, room, tables);
        }
        drawTransitionActors(scene, layer, view);
        drawEntrances(scene, layer, view);
        drawLightSettings(layer, view);
        drawPaths(layer, view);
        drawCollision(scene, view);
        drawWaterBoxes(scene, view);
        drawMaterials();
        drawUnread(core::icons::FilmSlate, "Cutscenes",
                   "Toki does not read cutscenes yet. A save writes them back as raw words.");
        drawUnread(core::icons::SignOut, "Exits",
                   "Toki reads the exit list only for the exits of collision. A save writes it "
                   "back as it was.");
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
    const bool isRowClicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
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
    const bool isEyeHovered = ImGui::IsItemHovered();
    ImGui::SetItemTooltip(isHidden ? "Hidden. Click to show the room." : "Click to hide the room.");
    if (isRowClicked && !isEyeHovered && hooks.pickRoom) {
        hooks.pickRoom(room);
    }
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
    const bool isOpen = beginNode("##lights", GroupFlags);
    ImGui::SetItemTooltip("The layer's light settings. Right-click to add one.");
    drawGroupMenu(OutlinerGroup::LightSettings);
    drawLabel(core::icons::Sun, core::palette::Warning, "Light Settings");
    drawInfo(std::format("{}", settings.size()));
    if (!isOpen) {
        return;
    }
    for (usize i = 0; i < settings.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const SceneNode node = LightSettingRef{.layer = view.layer, .index = i};
        const bool isSelected = hooks.isSelected && hooks.isSelected(node);
        beginNode("##light", LeafFlags | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
        if (ImGui::IsItemClicked() && hooks.pickNode) {
            hooks.pickNode(node);
        }
        ImGui::SetItemTooltip("Click to edit it and light the scene with it.");
        drawNodeMenu(node);
        ImGui::SameLine(0.0F, 0.0F);
        const std::array<u8, 3>& ambient = settings[i].ambientColor;
        ImGui::ColorButton(
            "##ambient",
            ImVec4(ambient[0] / 255.0F, ambient[1] / 255.0F, ambient[2] / 255.0F, 1.0F),
            ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
        ImGui::SameLine();
        ImGui::TextUnformatted(makeLightSettingName(layer.header, i).c_str());
        drawInfo(i == view.lightSetting ? "shown" : "");
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawPaths(const SceneLayer& layer, SceneView& view) {
    if (!filter.empty()) {
        return;
    }
    const std::vector<ScenePath>& paths = layer.header.paths;
    const bool isOpen = beginNode("##paths", GroupFlags);
    ImGui::SetItemTooltip("The layer's paths, which actors that follow one name by index. "
                          "Right-click to add one.");
    drawGroupMenu(OutlinerGroup::Paths);
    drawLabel(core::icons::Path, PathColor, "Paths");
    drawInfo(std::format("{}", paths.size()));
    drawShownSwitch(view.showsPaths, "the paths");
    if (!isOpen) {
        return;
    }
    for (usize path = 0; path < paths.size(); ++path) {
        ImGui::PushID(static_cast<int>(path));
        const SceneNode node = PathRef{.layer = view.layer, .path = path};
        const bool isSelected = hooks.isSelected && hooks.isSelected(node);
        const bool isPathOpen =
            beginNode("##path", GroupFlags | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && hooks.pickNode) {
            hooks.pickNode(node);
        }
        drawNodeMenu(node);
        drawLabel(core::icons::Path, PathColor, std::format("Path {}", path));
        drawInfo(std::format("{} points", paths[path].points.size()));
        if (isPathOpen) {
            drawPathPoints(paths[path], node);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawPathPoints(const ScenePath& path, const SceneNode& pathNode) {
    const auto& [layer, index] = std::get<PathRef>(pathNode);
    for (usize i = 0; i < path.points.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const SceneNode node = PathPointRef{.layer = layer, .path = index, .point = i};
        const bool isSelected = hooks.isSelected && hooks.isSelected(node);
        beginNode("##point", LeafFlags | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
        if (ImGui::IsItemClicked() && hooks.pickNode) {
            hooks.pickNode(node);
        }
        const std::array<s16, 3>& point = path.points[i];
        ImGui::SetItemTooltip("At %d, %d, %d", point[0], point[1], point[2]);
        drawNodeMenu(node);
        drawLabel(core::icons::Dot, PathColor, std::format("Point {}", i));
        drawInfo("");
        ImGui::PopID();
    }
}

void SceneOutliner::drawShownSwitch(bool& isShown, const std::string_view what) const {
    ImGui::TableNextColumn();
    ImGui::PushID(what.data(), what.data() + what.size());
    const bool isClicked = ImGui::SmallButton(isShown ? core::icons::Eye : core::icons::EyeSlash);
    ImGui::PopID();
    if (isClicked) {
        isShown = !isShown;
        if (hooks.showScene) {
            hooks.showScene();
        }
    }
    ImGui::SetItemTooltip("%s",
                          (isShown ? std::format("Click to hide {} in the viewport.", what)
                                   : std::format("Hidden. Click to show {} in the viewport.", what))
                              .c_str());
}

void SceneOutliner::drawCollision(const Scene& scene, SceneView& view) {
    const Collision* collision = findLayerCollision(scene, view.layer);
    if (!filter.empty() || collision == nullptr) {
        return;
    }
    if (revealingSurface) {
        ImGui::SetNextItemOpen(true);
    }
    const bool isOpen = beginNode("##collision", GroupFlags);
    ImGui::SetItemTooltip("The layer's collision. Shown, it is drawn see-through over the rooms "
                          "shown, or opaque with every room hidden; the viewport bar's Overlays "
                          "choose its colours.");
    drawLabel(core::icons::Polygon, CollisionColor, "Collision");
    drawInfo(std::format("{} polys", collision->polygons.size()));
    drawShownSwitch(view.showsCollision, "the collision");
    if (!isOpen) {
        return;
    }
    beginNode("##vertices", LeafFlags);
    ImGui::SetItemTooltip("Bounds from %d, %d, %d to %d, %d, %d.", collision->minBounds[0],
                          collision->minBounds[1], collision->minBounds[2], collision->maxBounds[0],
                          collision->maxBounds[1], collision->maxBounds[2]);
    drawLabel(core::icons::DotsNine, core::palette::TextMuted, "Vertices");
    drawInfo(std::format("{}", collision->vertices.size()));
    drawSurfaceTypes(*collision, view);
    drawCameras(scene, *collision);
    ImGui::TreePop();
}

void SceneOutliner::drawMaterials() {
    const std::vector<SceneMaterial>* materials =
        hooks.findMaterials ? hooks.findMaterials() : nullptr;
    if (!filter.empty()) {
        return;
    }
    if (materials == nullptr) {
        drawUnread(core::icons::Palette, "Materials",
                   "The materials the rooms are drawn with are read once the viewport selects by "
                   "faces, edges or vertices.");
        return;
    }
    const bool isOpen = beginNode("##materials", GroupFlags);
    ImGui::SetItemTooltip("What the rooms' faces are drawn under, one row for every room's copy "
                          "of it. Click one to select its faces; right-click to assign, rename or "
                          "duplicate it.");
    drawLabel(core::icons::Palette, MaterialColor, "Materials");
    drawInfo(std::format("{}", materials->size()));
    if (!isOpen) {
        return;
    }
    for (usize index = 0; index < materials->size(); ++index) {
        const SceneMaterial& material = (*materials)[index];
        ImGui::PushID(static_cast<int>(index));
        beginNode("##material", LeafFlags);
        if (ImGui::IsItemClicked() && hooks.pickMaterial) {
            hooks.pickMaterial(index);
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(material.name.c_str());
            if (!material.texture.empty()) {
                ImGui::TextColored(core::palette::TextMuted, "Texture %s",
                                   material.texture.c_str());
            }
            ImGui::TextColored(core::palette::TextMuted, "%s, %s",
                               material.isTranslucent ? "Translucent" : "Opaque",
                               material.isLit ? "lit" : "vertex coloured");
            for (const MaterialRoom& room : material.rooms) {
                ImGui::TextColored(core::palette::TextMuted, "Room %zu: %zu faces%s%s", room.room,
                                   room.faces, room.list.empty() ? "" : ", set by ",
                                   room.list.c_str());
            }
            ImGui::EndTooltip();
        }
        if (hooks.drawMaterialMenu && ImGui::BeginPopupContextItem("##materialMenu")) {
            hooks.drawMaterialMenu(index);
            ImGui::EndPopup();
        }
        drawLabel(material.texture.empty() ? core::icons::Drop : core::icons::Checkerboard,
                  MaterialColor, material.name, material.countFaces() == 0);
        drawInfo(
            material.rooms.size() == 1
                ? std::format("{} faces", material.countFaces())
                : std::format("{} faces, {} rooms", material.countFaces(), material.rooms.size()));
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawSurfaceTypes(const Collision& collision, const SceneView& view) {
    if (revealingSurface) {
        ImGui::SetNextItemOpen(true);
    }
    const bool isOpen = beginNode("##surfaces", GroupFlags);
    ImGui::SetItemTooltip("The settings polygons share. Click one to select its polygons. "
                          "Right-click to add one or remove those no polygon has.");
    drawGroupMenu(OutlinerGroup::SurfaceTypes);
    drawLabel(core::icons::Stack, CollisionColor, "Surface Types");
    drawInfo(std::format("{}", collision.surfaceTypes.size()));
    if (!isOpen) {
        return;
    }
    const std::vector<usize> counts = countSurfacePolygons(collision);
    for (usize i = 0; i < collision.surfaceTypes.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool isSelected = view.tintedSurfaceType == i;
        beginNode("##surface", LeafFlags | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
        if (isSelected && revealingSurface) {
            ImGui::SetScrollHereY();
        }
        if (ImGui::IsItemClicked() && hooks.pickNodes) {
            std::vector<SceneNode> polygons;
            for (usize polygon = 0; polygon < collision.polygons.size(); ++polygon) {
                if (collision.polygons[polygon].type == i) {
                    polygons.emplace_back(
                        CollisionPolygonRef{.layer = view.layer, .polygon = polygon});
                }
            }
            hooks.pickNodes(polygons);
        }
        if (collisionNames != nullptr) {
            ImGui::SetItemTooltip(
                "%s",
                makeSurfaceSummary(*collisionNames, readSurfaceFields(collision.surfaceTypes[i]))
                    .c_str());
        }
        drawSwatch(findSurfaceTypeColor(i));
        ImGui::SameLine();
        ImGui::TextUnformatted(makeSurfaceTypeLabel(collisionNames, collision, i).c_str());
        drawInfo(std::format("{} polys", counts[i]));
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawWaterBoxes(const Scene& scene, SceneView& view) {
    const Collision* found = findLayerCollision(scene, view.layer);
    if (!filter.empty() || found == nullptr) {
        return;
    }
    const Collision& collision = *found;
    const usize layer = view.layer;
    const bool isOpen = beginNode("##waterBoxes", GroupFlags);
    ImGui::SetItemTooltip("Where there is water, from each box's surface down. Shown whichever "
                          "rooms are hidden, as the collision is. Right-click to add one.");
    drawGroupMenu(OutlinerGroup::WaterBoxes);
    drawLabel(core::icons::Waves, WaterColor, "Waterboxes");
    drawInfo(std::format("{}", collision.waterBoxes.size()));
    drawShownSwitch(view.showsWaterBoxes, "the waterboxes");
    if (!isOpen) {
        return;
    }
    for (usize i = 0; i < collision.waterBoxes.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const SceneNode node = WaterBoxRef{.layer = layer, .box = i};
        const bool isSelected = hooks.isSelected && hooks.isSelected(node);
        beginNode("##waterBox", LeafFlags | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
        if (ImGui::IsItemClicked() && hooks.pickNodes) {
            hooks.pickNodes(std::span(&node, 1));
        }
        drawNodeMenu(node);
        const WaterBox& box = collision.waterBoxes[i];
        ImGui::SetItemTooltip("Surface at %d.", box.ySurface);
        drawLabel(core::icons::Waves, WaterColor, std::format("Waterbox {}", i));
        const u8 room = getWaterBoxRoom(box);
        drawInfo(room == WaterBoxAllRooms ? std::string("all rooms")
                                          : std::format("room {}", room));
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawCameras(const Scene& scene, const Collision& collision) const {
    const bool isOpen = beginNode("##cameras", GroupFlags);
    ImGui::SetItemTooltip("The camera settings surface types and waterboxes name by index.");
    drawLabel(core::icons::VideoCamera, core::palette::TextMuted, "Cameras");
    drawInfo(std::format("{}", collision.cameras.size()));
    if (!isOpen) {
        return;
    }
    for (usize i = 0; i < collision.cameras.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const CollisionCamera& camera = collision.cameras[i];
        beginNode("##camera", LeafFlags);
        ImGui::SetItemTooltip("Setting %u, data at 0x%08X, count %d.", camera.setting, camera.data,
                              camera.count);
        const auto named = std::ranges::find(scene.headerNames.cameraSettings, s64{camera.setting},
                                             &NamedValue::value);
        drawLabel(core::icons::VideoCamera, core::palette::TextMuted,
                  named != scene.headerNames.cameraSettings.end()
                      ? std::format("Camera {} · {}", i, named->label)
                      : std::format("Camera {} · setting {}", i, camera.setting));
        drawInfo("");
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void SceneOutliner::drawGroupMenu(const OutlinerGroup group) const {
    if (hooks.drawGroupMenu && ImGui::BeginPopupContextItem()) {
        hooks.drawGroupMenu(group);
        ImGui::EndPopup();
    }
}

void SceneOutliner::drawNodeMenu(const SceneNode& node) const {
    if (hooks.drawNodeMenu && ImGui::BeginPopupContextItem()) {
        hooks.drawNodeMenu(node);
        ImGui::EndPopup();
    }
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

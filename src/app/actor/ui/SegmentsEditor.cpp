#include "SegmentsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array SegmentKinds{object::AssetKind::Texture, object::AssetKind::DisplayList,
                                  object::AssetKind::Palette};
constexpr int FirstSegment = 8;
constexpr int LastSegment = 12;
constexpr int LastTile = 7;
constexpr int LastCoordinate = 0x7FF;
constexpr int LargestSide = 1024;
constexpr u16 DefaultSide = 32;
constexpr std::array<const char*, 2> TargetLabels{"Symbol", "Built"};

[[nodiscard]] bool editTarget(PartSegment& segment) {
    const bool isBuilt = std::holds_alternative<BuiltList>(segment.target);
    core::drawPropertyLabel(std::format("Segment {}", segment.segment).c_str());
    bool isChanged = false;
    if (ImGui::BeginCombo("##target", TargetLabels[isBuilt ? 1 : 0])) {
        if (ImGui::Selectable(TargetLabels[0], !isBuilt) && isBuilt) {
            segment.target = std::string();
            isChanged = true;
        }
        ImGui::SetItemTooltip("Pointed at a symbol of the object, as at an eye texture");
        if (ImGui::Selectable(TargetLabels[1], isBuilt) && !isBuilt) {
            segment.target = BuiltList{};
            isChanged = true;
        }
        ImGui::SetItemTooltip("Pointed at a list built as the game builds one, as Gfx_EnvColor "
                              "or Gfx_TwoTexScroll do");
        ImGui::EndCombo();
    }
    return isChanged;
}

[[nodiscard]] bool editPair(const char* label, std::array<int, 2>& values, const int first,
                            const int last) {
    core::drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool isChanged = ImGui::InputInt2("##value", values.data());
    ImGui::PopID();
    for (int& value : values) {
        value = std::clamp(value, first, last);
    }
    return isChanged;
}

[[nodiscard]] bool editTile(TileSize& size) {
    bool isChanged = false;
    core::drawPropertyLabel("Tile");
    int tile = size.tile;
    if (ImGui::InputInt("##tile", &tile)) {
        size.tile = static_cast<u8>(std::clamp(tile, 0, LastTile));
        isChanged = true;
    }
    std::array<int, 2> side{size.width, size.height};
    if (editPair("Texels", side, 1, LargestSide)) {
        size.width = static_cast<u16>(side[0]);
        size.height = static_cast<u16>(side[1]);
        isChanged = true;
    }
    ImGui::SetItemTooltip("How many texels wide and high the tile is");
    std::array<int, 2> start{size.x, size.y};
    if (editPair("Starts at", start, 0, LastCoordinate)) {
        size.x = static_cast<u16>(start[0]);
        size.y = static_cast<u16>(start[1]);
        isChanged = true;
    }
    ImGui::SetItemTooltip("Where the texture starts on the tile, in quarter texels, as the "
                          "scroll has moved it");
    return isChanged;
}

[[nodiscard]] bool editBuiltList(BuiltList& built) {
    bool isChanged = editOptionalColor("Primitive", built.colors.primitive);
    isChanged = editOptionalColor("Environment", built.colors.environment) || isChanged;
    std::optional<usize> removed;
    for (usize i = 0; i < built.tiles.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        isChanged = editTile(built.tiles[i]) || isChanged;
        core::drawPropertyLabel("");
        if (ImGui::SmallButton("Remove tile")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        built.tiles.erase(built.tiles.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    core::drawPropertyLabel("");
    if (ImGui::SmallButton("+ Tile size")) {
        built.tiles.push_back(TileSize{.width = DefaultSide, .height = DefaultSide});
        isChanged = true;
    }
    ImGui::SetItemTooltip("Size a tile, as a texture scroll does");
    return isChanged;
}

[[nodiscard]] ImVec4 makeBadgeColor(const DrawColors& colors) {
    const std::optional<std::array<u8, 4>>& color =
        colors.environment ? colors.environment : colors.primitive;
    if (!color) {
        return core::palette::TextMuted;
    }
    return {static_cast<f32>((*color)[0]) / 255.0F, static_cast<f32>((*color)[1]) / 255.0F,
            static_cast<f32>((*color)[2]) / 255.0F, 1.0F};
}

} // namespace

bool editSegments(std::vector<PartSegment>& segments, const std::optional<std::string>& object,
                  const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < segments.size(); ++i) {
        PartSegment& segment = segments[i];
        ImGui::PushID(static_cast<int>(i));
        isChanged = editTarget(segment) || isChanged;
        if (auto* symbol = std::get_if<std::string>(&segment.target)) {
            isChanged = editSymbol("Symbol", *symbol, object, SegmentKinds, context) || isChanged;
        } else {
            isChanged = editBuiltList(std::get<BuiltList>(segment.target)) || isChanged;
        }
        core::drawPropertyLabel("");
        if (ImGui::SmallButton(
                std::format("Point segment {} at nothing", segment.segment).c_str())) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    core::drawPropertyLabel("Segments");
    for (int segment = FirstSegment; segment <= LastSegment; ++segment) {
        if (std::ranges::contains(segments, static_cast<u8>(segment), &PartSegment::segment)) {
            continue;
        }
        const std::string button = std::format("+ {}", segment);
        if (ImGui::SmallButton(button.c_str())) {
            segments.push_back(PartSegment{.segment = static_cast<u8>(segment)});
            isChanged = true;
        }
        ImGui::SetItemTooltip("Point segment %d at a symbol or a built list", segment);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    return isChanged;
}

std::vector<RowBadge> makeSegmentBadges(const std::span<const PartSegment> segments) {
    std::vector<RowBadge> badges;
    const auto symbols = std::ranges::count_if(segments, [](const PartSegment& segment) {
        return std::holds_alternative<std::string>(segment.target);
    });
    if (symbols > 0) {
        badges.push_back(RowBadge{.text = std::format("segments {}", symbols),
                                  .color = core::palette::TextMuted});
    }
    for (const PartSegment& segment : segments) {
        if (const auto* built = std::get_if<BuiltList>(&segment.target)) {
            badges.push_back(RowBadge{.text = std::format("seg {} built", segment.segment),
                                      .color = makeBadgeColor(built->colors)});
        }
    }
    return badges;
}

} // namespace toki::actor

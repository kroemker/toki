#include "SegmentsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array SegmentKinds{object::AssetKind::Texture, object::AssetKind::DisplayList,
                                  object::AssetKind::Palette};
constexpr u8 FirstSegment = 8;
constexpr u8 LastSegment = 12;
constexpr int LastTile = 7;
constexpr int LastCoordinate = 0x7FF;
constexpr int LargestSide = 1024;
constexpr u16 DefaultSide = 32;
constexpr float TileColumnWidth = 3.0F;

enum class Target : u8 {
    Nothing,
    Symbol,
    Built,
    Object,
};

constexpr std::array<const char*, 4> TargetLabels{"Nothing", "A symbol", "A built list",
                                                  "Another object"};
constexpr std::array<const char*, 4> TargetHelp{
    "Left unset, as the game leaves a segment the actor does not set",
    "Pointed at a symbol of the object, as at an eye texture",
    "Pointed at a list built as the game builds one, as Gfx_EnvColor or Gfx_TwoTexScroll do",
    "Pointed into another object the actor loads, at its start or at a symbol in it, as the "
    "Forest Temple's actors point segment 8 at object_mori_tex"};

[[nodiscard]] const PartSegment* findSegment(const std::span<const PartSegment> segments,
                                             const u8 number) {
    const auto found = std::ranges::find(segments, number, &PartSegment::segment);
    return found == segments.end() ? nullptr : &*found;
}

[[nodiscard]] Target getTarget(const PartSegment* segment) {
    if (segment == nullptr) {
        return Target::Nothing;
    }
    if (std::holds_alternative<SegmentObject>(segment->target)) {
        return Target::Object;
    }
    return std::holds_alternative<BuiltList>(segment->target) ? Target::Built : Target::Symbol;
}

void setTarget(std::vector<PartSegment>& segments, const u8 number, const Target target) {
    std::erase_if(segments,
                  [number](const PartSegment& segment) { return segment.segment == number; });
    if (target == Target::Nothing) {
        return;
    }
    PartSegment segment{.segment = number};
    if (target == Target::Built) {
        segment.target = BuiltList{};
    } else if (target == Target::Object) {
        segment.target = SegmentObject{};
    }
    const auto after = std::ranges::upper_bound(segments, number, {}, &PartSegment::segment);
    segments.insert(after, std::move(segment));
}

[[nodiscard]] ImVec4 makeColor(const std::array<u8, 4>& color) {
    return {static_cast<f32>(color[0]) / 255.0F, static_cast<f32>(color[1]) / 255.0F,
            static_cast<f32>(color[2]) / 255.0F, 1.0F};
}

[[nodiscard]] std::string describeSegment(const PartSegment* segment) {
    if (segment == nullptr) {
        return "Not set";
    }
    if (const auto* symbol = std::get_if<std::string>(&segment->target)) {
        return symbol->empty() ? std::string("A symbol, not picked yet") : *symbol;
    }
    if (const auto* other = std::get_if<SegmentObject>(&segment->target)) {
        const std::string object = other->object.empty() ? std::string("…") : other->object;
        return other->symbol.empty() ? std::format("The start of {}", object)
                                     : std::format("{} of {}", other->symbol, object);
    }
    const BuiltList& built = std::get<BuiltList>(segment->target);
    std::string described = "A built list";
    if (built.colors.primitive) {
        described += ", primitive colour";
    }
    if (built.colors.environment) {
        described += ", environment colour";
    }
    if (!built.tiles.empty()) {
        described += std::format(", {} tile sizes", built.tiles.size());
    }
    return described;
}

void drawSlotSummary(ImDrawList& drawList, const PartSegment* segment, const ImVec2 min,
                     const float width, const float y) {
    const float padding = ImGui::GetStyle().FramePadding.x;
    const float space = width - (padding * 2.0F);
    const auto drawCentered = [&](const std::string& text, const ImVec4& color) {
        const std::string fitted = makeFittedText(text, space);
        const float x = min.x + ((width - ImGui::CalcTextSize(fitted.c_str()).x) * 0.5F);
        drawList.AddText(ImVec2(x, y), ImGui::GetColorU32(color), fitted.c_str());
    };
    if (segment == nullptr) {
        drawCentered("—", core::palette::TextDisabled);
        return;
    }
    if (const auto* symbol = std::get_if<std::string>(&segment->target)) {
        drawCentered(symbol->empty() ? std::string("symbol") : *symbol,
                     symbol->empty() ? core::palette::TextMuted : core::palette::Text);
        return;
    }
    if (const auto* other = std::get_if<SegmentObject>(&segment->target)) {
        const std::string& shown = other->symbol.empty() ? other->object : other->symbol;
        drawCentered(shown.empty() ? std::string("object") : shown, core::palette::Warning);
        return;
    }
    const BuiltList& built = std::get<BuiltList>(segment->target);
    std::vector<ImVec4> swatches;
    for (const auto& color : {built.colors.primitive, built.colors.environment}) {
        if (color) {
            swatches.push_back(makeColor(*color));
        }
    }
    const std::string tiles =
        built.tiles.empty() ? std::string() : std::format("{}t", built.tiles.size());
    const float side = ImGui::GetTextLineHeight();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    if (swatches.empty() && tiles.empty()) {
        drawCentered("built", core::palette::TextMuted);
        return;
    }
    const float tilesWidth = tiles.empty() ? 0.0F : ImGui::CalcTextSize(tiles.c_str()).x;
    const float total = (static_cast<float>(swatches.size()) * (side + spacing)) + tilesWidth;
    float x = min.x + std::max((width - total) * 0.5F, padding);
    for (const ImVec4& swatch : swatches) {
        drawList.AddRectFilled(ImVec2(x, y), ImVec2(x + side, y + side), ImGui::GetColorU32(swatch),
                               ImGui::GetStyle().FrameRounding);
        x += side + spacing;
    }
    if (!tiles.empty()) {
        drawList.AddText(ImVec2(x, y), ImGui::GetColorU32(core::palette::Text), tiles.c_str());
    }
}

[[nodiscard]] bool drawSlot(const PartSegment* segment, const u8 number, const bool isSelected,
                            const float width) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 size(width, (line * 2.0F) + (style.FramePadding.y * 2.0F));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ImGui::PushID(number);
    const bool isClicked = ImGui::InvisibleButton("##slot", size);
    const bool isHovered = ImGui::IsItemHovered();
    ImGui::SetItemTooltip("Segment %d: %s", number, describeSegment(segment).c_str());
    ImGui::PopID();

    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec4& fill = isSelected           ? core::palette::AccentMuted
                         : isHovered          ? core::palette::SurfaceHovered
                         : segment != nullptr ? core::palette::SurfaceRaised
                                              : core::palette::Surface;
    drawList.AddRectFilled(min, max, ImGui::GetColorU32(fill), style.FrameRounding);
    drawList.AddRect(min, max,
                     ImGui::GetColorU32(isSelected ? core::palette::Accent : core::palette::Border),
                     style.FrameRounding);
    const std::string label = std::to_string(number);
    const float labelX = min.x + ((width - ImGui::CalcTextSize(label.c_str()).x) * 0.5F);
    drawList.AddText(
        ImVec2(labelX, min.y + style.FramePadding.y),
        ImGui::GetColorU32(segment != nullptr ? core::palette::Text : core::palette::TextMuted),
        label.c_str());
    drawSlotSummary(drawList, segment, min, width, min.y + style.FramePadding.y + line);
    return isClicked;
}

[[nodiscard]] u8 drawSlots(const std::span<const PartSegment> segments, const u8 selected) {
    constexpr int Count = LastSegment - FirstSegment + 1;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width =
        std::max((ImGui::GetContentRegionAvail().x - (spacing * (Count - 1))) / Count, 1.0F);
    u8 picked = selected;
    for (u8 number = FirstSegment; number <= LastSegment; ++number) {
        if (number != FirstSegment) {
            ImGui::SameLine(0.0F, spacing);
        }
        if (drawSlot(findSegment(segments, number), number, number == selected, width)) {
            picked = number;
        }
    }
    return picked;
}

[[nodiscard]] bool editTarget(std::vector<PartSegment>& segments, const u8 number) {
    const Target shown = getTarget(findSegment(segments, number));
    core::drawPropertyLabel(std::format("Segment {}", number));
    bool isChanged = false;
    if (ImGui::BeginCombo("##target", TargetLabels[static_cast<usize>(shown)])) {
        for (usize i = 0; i < TargetLabels.size(); ++i) {
            const auto target = static_cast<Target>(i);
            if (ImGui::Selectable(TargetLabels[i], target == shown) && target != shown) {
                setTarget(segments, number, target);
                isChanged = true;
            }
            ImGui::SetItemTooltip("%s", TargetHelp[i]);
        }
        ImGui::EndCombo();
    }
    return isChanged;
}

[[nodiscard]] bool editPair(const char* id, std::array<int, 2>& values, const int first,
                            const int last) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool isChanged = ImGui::InputInt2(id, values.data());
    for (int& value : values) {
        value = std::clamp(value, first, last);
    }
    return isChanged;
}

[[nodiscard]] bool editTile(TileSize& size) {
    bool isChanged = false;
    ImGui::TableNextColumn();
    int tile = size.tile;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputInt("##tile", &tile, 0, 0)) {
        size.tile = static_cast<u8>(std::clamp(tile, 0, LastTile));
        isChanged = true;
    }
    ImGui::TableNextColumn();
    std::array<int, 2> side{size.width, size.height};
    if (editPair("##texels", side, 1, LargestSide)) {
        size.width = static_cast<u16>(side[0]);
        size.height = static_cast<u16>(side[1]);
        isChanged = true;
    }
    ImGui::SetItemTooltip("How many texels wide and high the tile is");
    ImGui::TableNextColumn();
    std::array<int, 2> start{size.x, size.y};
    if (editPair("##start", start, 0, LastCoordinate)) {
        size.x = static_cast<u16>(start[0]);
        size.y = static_cast<u16>(start[1]);
        isChanged = true;
    }
    ImGui::SetItemTooltip("Where the texture starts on the tile, in quarter texels, as the "
                          "scroll has moved it");
    return isChanged;
}

[[nodiscard]] bool editTiles(std::vector<TileSize>& tiles) {
    bool isChanged = false;
    std::optional<usize> removed;
    if (!tiles.empty() &&
        ImGui::BeginTable("tiles", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
        const float frame = ImGui::GetFrameHeight();
        ImGui::TableSetupColumn("Tile", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * TileColumnWidth);
        ImGui::TableSetupColumn("Texels");
        ImGui::TableSetupColumn("Starts at");
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, frame);
        ImGui::TableHeadersRow();
        for (usize i = 0; i < tiles.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            isChanged = editTile(tiles[i]) || isChanged;
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
            if (ImGui::Button(core::icons::Trash, ImVec2(frame, frame))) {
                removed = i;
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Remove this tile size");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removed) {
        tiles.erase(tiles.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("+ Tile size")) {
        tiles.push_back(TileSize{.width = DefaultSide, .height = DefaultSide});
        isChanged = true;
    }
    ImGui::SetItemTooltip("Size a tile, as a texture scroll does");
    return isChanged;
}

[[nodiscard]] bool editSegmentObject(SegmentObject& target, const EditContext& context) {
    bool isChanged = editObjectName("Object", target.object, context);
    if (target.object.empty()) {
        return isChanged;
    }
    isChanged =
        editSymbol("Symbol", target.symbol, target.object, SegmentKinds, context) || isChanged;
    ImGui::SetItemTooltip("Left empty, the segment points at the object's start");
    return isChanged;
}

[[nodiscard]] bool editSelected(std::vector<PartSegment>& segments, const u8 number,
                                const std::optional<std::string>& object,
                                const EditContext& context) {
    if (!core::beginProperties("segment")) {
        return false;
    }
    bool isChanged = editTarget(segments, number);
    const auto found = std::ranges::find(segments, number, &PartSegment::segment);
    BuiltList* built = nullptr;
    if (found != segments.end()) {
        if (auto* symbol = std::get_if<std::string>(&found->target)) {
            isChanged = editSymbol("Symbol", *symbol, object, SegmentKinds, context) || isChanged;
        } else if (auto* other = std::get_if<SegmentObject>(&found->target)) {
            isChanged = editSegmentObject(*other, context) || isChanged;
        } else {
            built = &std::get<BuiltList>(found->target);
            isChanged = editOptionalColor("Primitive", built->colors.primitive) || isChanged;
            isChanged = editOptionalColor("Environment", built->colors.environment) || isChanged;
        }
    }
    core::endProperties();
    if (built != nullptr) {
        isChanged = editTiles(built->tiles) || isChanged;
    }
    return isChanged;
}

} // namespace

bool editSegments(std::vector<PartSegment>& segments, const std::optional<std::string>& object,
                  const EditContext& context) {
    ImGui::PushID("segments");
    ImGui::SeparatorText("Segments");
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID selectedId = ImGui::GetID("##selected");
    const int fallback = segments.empty() ? FirstSegment : segments.front().segment;
    const auto selected =
        static_cast<u8>(std::clamp(storage->GetInt(selectedId, fallback),
                                   static_cast<int>(FirstSegment), static_cast<int>(LastSegment)));
    const u8 picked = drawSlots(segments, selected);
    storage->SetInt(selectedId, picked);
    const bool isChanged = editSelected(segments, picked, object, context);
    ImGui::PopID();
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
            const std::optional<std::array<u8, 4>>& color =
                built->colors.environment ? built->colors.environment : built->colors.primitive;
            badges.push_back(
                RowBadge{.text = std::format("seg {} built", segment.segment),
                         .color = color ? makeColor(*color) : core::palette::TextMuted});
        }
    }
    return badges;
}

} // namespace toki::actor

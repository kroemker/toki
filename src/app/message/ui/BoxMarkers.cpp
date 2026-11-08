#include "BoxMarkers.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageCodes.h"

#include <algorithm>
#include <cfloat>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

constexpr f32 MarkerHeadGamePixels = 6.0F;
constexpr f32 MarkerHeadMinSize = 12.0F;
constexpr f32 IconShare = 0.8F;
constexpr f32 RoundingShare = 0.25F;
constexpr u8 OutlineAlpha = 160;
constexpr f32 CountShare = 0.6F;

[[nodiscard]] const char* findMarkerIcon(const std::string_view name) {
    if (name == "SHIFT" || name == "BACKGROUND") {
        return core::icons::ArrowsHorizontal;
    }
    if (name == "TEXT_SPEED") {
        return core::icons::Gauge;
    }
    if (name == "QUICKTEXT_ENABLE" || name == "QUICKTEXT_DISABLE") {
        return core::icons::Lightning;
    }
    if (name == "AWAIT_BUTTON_PRESS") {
        return core::icons::HandPointing;
    }
    if (name == "BOX_BREAK_DELAYED" || name == "FADE" || name == "FADE2") {
        return core::icons::Timer;
    }
    if (name == "PERSISTENT") {
        return core::icons::PushPin;
    }
    if (name == "EVENT") {
        return core::icons::Flag;
    }
    if (name == "TEXTID") {
        return core::icons::ArrowBendDownRight;
    }
    if (name == "SFX") {
        return core::icons::SpeakerHigh;
    }
    if (name == "OCARINA") {
        return core::icons::MusicNotes;
    }
    if (name == "UNSKIPPABLE") {
        return core::icons::Lock;
    }
    if (name == "TWO_CHOICE" || name == "THREE_CHOICE") {
        return core::icons::ListBullets;
    }
    return core::icons::Code;
}

void drawIcon(ImDrawList& drawList, const MarkerShape& shape, const char* icon) {
    ImFont* font = ImGui::GetFont();
    const f32 size = (shape.headMax.y - shape.headMin.y) * IconShare;
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, icon);
    drawList.AddText(font, size,
                     ImVec2((shape.headMin.x + shape.headMax.x - extent.x) / 2.0F,
                            (shape.headMin.y + shape.headMax.y - extent.y) / 2.0F),
                     ImGui::GetColorU32(core::palette::Text), icon);
}

} // namespace

std::vector<BoxMarker> findMarkers(const EditableBox& editable, const LaidBox& box,
                                   const std::span<const TextPosition> sources) {
    std::vector<BoxMarker> markers;
    for (const LaidStop& stop : box.stops) {
        if (stop.byte >= sources.size()) {
            continue;
        }
        const std::optional<usize> unit = editable.findUnit(sources[stop.byte]);
        if (!unit || *unit >= editable.getUnits().size()) {
            continue;
        }
        const auto* code = std::get_if<CodePiece>(&editable.getUnits()[*unit].piece);
        if (code == nullptr || code->name == "ITEM_ICON" ||
            std::ranges::any_of(box.characters, [&](const LaidCharacter& character) {
                return character.byte == stop.byte;
            })) {
            continue;
        }
        markers.push_back(BoxMarker{.unit = *unit,
                                    .place = CaretPlace{.x = stop.x, .y = stop.y},
                                    .color = stop.color,
                                    .code = *code});
    }
    return markers;
}

MarkerShape makeMarkerShape(const BoxMarker& marker, const LaidBox& box, const BoxImage& image) {
    const ImVec2 top = image.findScreenPosition(static_cast<f32>(marker.place.x),
                                                static_cast<f32>(marker.place.y));
    const f32 height = static_cast<f32>(findCharacterHeight(box)) * image.scale;
    const f32 half = std::max(MarkerHeadMinSize, MarkerHeadGamePixels * image.scale) / 2.0F;
    return MarkerShape{.headMin = ImVec2(top.x - half, top.y - half),
                       .headMax = ImVec2(top.x + half, top.y + half),
                       .lineTop = top,
                       .lineBottom = ImVec2(top.x, top.y + height)};
}

std::vector<MarkerGroup> groupMarkers(const std::span<const BoxMarker> markers, const LaidBox& box,
                                      const BoxImage& image) {
    std::vector<MarkerGroup> groups;
    for (const BoxMarker& marker : markers) {
        if (!groups.empty()) {
            const CaretPlace& last = groups.back().markers.back().place;
            if (last.x == marker.place.x && last.y == marker.place.y) {
                groups.back().markers.push_back(marker);
                continue;
            }
        }
        groups.push_back(
            MarkerGroup{.markers = {marker}, .shape = makeMarkerShape(marker, box, image)});
    }
    return groups;
}

const MarkerGroup* findGroupAt(const std::span<const MarkerGroup> groups, const ImVec2 point) {
    const auto found = std::ranges::find_if(
        groups, [&](const MarkerGroup& group) { return group.shape.contains(point); });
    return found != groups.end() ? &*found : nullptr;
}

namespace {

void drawMarkerHead(ImDrawList& drawList, const MarkerShape& shape, const BoxMarker& marker,
                    const f32 lineWidth) {
    const f32 rounding = (shape.headMax.x - shape.headMin.x) * RoundingShare;
    if (marker.code.name == "COLOR") {
        const ImU32 color = IM_COL32(marker.color.red, marker.color.green, marker.color.blue, 255);
        if (lineWidth > 0.0F) {
            drawList.AddLine(shape.lineTop, shape.lineBottom, color, lineWidth);
        }
        drawList.AddRectFilled(shape.headMin, shape.headMax, color, rounding);
        drawList.AddRect(shape.headMin, shape.headMax, IM_COL32(0, 0, 0, OutlineAlpha), rounding);
        return;
    }
    const ImU32 color = ImGui::GetColorU32(core::palette::TextMuted);
    if (lineWidth > 0.0F) {
        drawList.AddLine(shape.lineTop, shape.lineBottom, color, lineWidth);
    }
    drawList.AddRectFilled(shape.headMin, shape.headMax,
                           ImGui::GetColorU32(core::palette::SurfaceRaised), rounding);
    drawList.AddRect(shape.headMin, shape.headMax, color, rounding);
    drawIcon(drawList, shape, findMarkerIcon(marker.code.name));
}

void drawCount(ImDrawList& drawList, const MarkerShape& shape, const usize count) {
    const std::string text = std::to_string(count);
    ImFont* font = ImGui::GetFont();
    const f32 size = (shape.headMax.y - shape.headMin.y) * CountShare;
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, text.c_str());
    const f32 radius = std::max(extent.x, extent.y) * 0.6F;
    const ImVec2 center(shape.headMax.x, shape.headMin.y);
    drawList.AddCircleFilled(center, radius, ImGui::GetColorU32(core::palette::Accent));
    drawList.AddText(font, size, ImVec2(center.x - (extent.x / 2.0F), center.y - (extent.y / 2.0F)),
                     ImGui::GetColorU32(core::palette::Text), text.c_str());
}

} // namespace

void drawMarkers(const std::span<const MarkerGroup> groups, const BoxImage& image) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 lineWidth = std::max(1.0F, image.scale * 0.5F);
    for (const MarkerGroup& group : groups) {
        drawMarkerHead(*drawList, group.shape, group.markers.back(), lineWidth);
        if (group.markers.size() > 1) {
            drawCount(*drawList, group.shape, group.markers.size());
        }
    }
    if (!ImGui::IsWindowHovered()) {
        return;
    }
    const MarkerGroup* hovered = findGroupAt(groups, ImGui::GetIO().MousePos);
    if (hovered != nullptr && hovered->markers.size() == 1) {
        ImGui::SetTooltip("%s", describeCode(hovered->markers.front().code).c_str());
    }
}

bool drawMarkerRow(const BoxMarker& marker) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 size = ImGui::GetTextLineHeight();
    const std::string text = describeCode(marker.code);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const f32 width = size + style.ItemInnerSpacing.x + ImGui::CalcTextSize(text.c_str()).x;
    const bool isClicked =
        ImGui::Selectable("##marker", false, ImGuiSelectableFlags_None, ImVec2(width, size));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const MarkerShape head{.headMin = origin,
                           .headMax = ImVec2(origin.x + size, origin.y + size),
                           .lineTop = origin,
                           .lineBottom = origin};
    drawMarkerHead(*drawList, head, marker, 0.0F);
    drawList->AddText(ImVec2(origin.x + size + style.ItemInnerSpacing.x, origin.y),
                      ImGui::GetColorU32(core::palette::Text), text.c_str());
    return isClicked;
}

} // namespace toki::message

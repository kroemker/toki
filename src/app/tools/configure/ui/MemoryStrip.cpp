#include "MemoryStrip.h"

#include "app/core/ui/Theme.h"
#include "app/tools/configure/domain/MemoryLayout.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace toki::configure {

namespace {

constexpr f32 StripHeight = 2.6F;
constexpr f32 InsetFraction = 0.42F;
constexpr f32 HatchSpacing = 8.0F;
constexpr f32 DashLength = 3.0F;
constexpr u32 Mebibyte = 0x100000;
constexpr u32 RamMebibytes = 8;
constexpr ImU32 HatchColor = IM_COL32(255, 255, 255, 14);
constexpr ImU32 MinorLineColor = IM_COL32(255, 255, 255, 36);
constexpr ImU32 MajorLineColor = IM_COL32(255, 255, 255, 107);
constexpr ImVec4 DarkText{0.059F, 0.067F, 0.078F, 1.0F};

struct PlacedRegion {
    usize index = 0;
    ImVec2 min;
    ImVec2 max;
};

[[nodiscard]] bool hasDarkText(const RegionKind kind) {
    return kind != RegionKind::Fixed && kind != RegionKind::Heap && kind != RegionKind::Unused;
}

[[nodiscard]] f32 findX(const f32 left, const f32 width, const u32 address) {
    return left +
           width * static_cast<f32>(address - RamStart) / static_cast<f32>(RamEnd - RamStart);
}

void drawHatch(ImDrawList& drawList, const ImVec2 min, const ImVec2 max) {
    drawList.PushClipRect(min, max, true);
    const f32 height = max.y - min.y;
    for (f32 x = min.x - height; x < max.x; x += HatchSpacing) {
        drawList.AddLine(ImVec2(x, max.y), ImVec2(x + height, min.y), HatchColor);
    }
    drawList.PopClipRect();
}

void drawRegion(ImDrawList& drawList, const MemoryRegion& region, const ImVec2 min,
                const ImVec2 max) {
    drawList.AddRectFilled(min, max, ImGui::GetColorU32(getRegionColor(region.kind)));
    if (region.isRemainder) {
        drawHatch(drawList, min, max);
    }
    drawList.AddLine(ImVec2(max.x, min.y), ImVec2(max.x, max.y),
                     ImGui::GetColorU32(core::palette::Background));
    if (region.isProblem) {
        drawList.AddRect(min, max, ImGui::GetColorU32(core::palette::Danger), 0.0F, 0, 2.0F);
    }
    const f32 padding = core::space::Small + 2.0F;
    const ImVec2 textSize = ImGui::CalcTextSize(region.label.c_str());
    if (textSize.x + 2.0F * padding > max.x - min.x) {
        return;
    }
    const f32 y = region.placement == StripPlacement::Inset ? min.y + 1.0F : min.y + padding;
    drawList.AddText(ImVec2(min.x + padding, y),
                     ImGui::GetColorU32(hasDarkText(region.kind) ? DarkText : core::palette::Text),
                     region.label.c_str());
}

[[nodiscard]] std::vector<PlacedRegion> placeRegions(const MemoryLayout& layout,
                                                     const ImVec2 origin, const ImVec2 size) {
    std::vector<PlacedRegion> placed;
    for (usize i = 0; i < layout.regions.size(); ++i) {
        const MemoryRegion& region = layout.regions[i];
        if (region.placement == StripPlacement::None || region.end <= region.start) {
            continue;
        }
        const f32 top = region.placement == StripPlacement::Inset
                            ? origin.y + size.y * (1.0F - InsetFraction)
                            : origin.y;
        placed.push_back({.index = i,
                          .min = ImVec2(findX(origin.x, size.x, region.start), top),
                          .max = ImVec2(std::max(findX(origin.x, size.x, region.end),
                                                 findX(origin.x, size.x, region.start) + 1.0F),
                                        origin.y + size.y)});
    }
    return placed;
}

void drawGridLines(ImDrawList& drawList, const ImVec2 origin, const ImVec2 size) {
    for (u32 mebibyte = 1; mebibyte < RamMebibytes; ++mebibyte) {
        const f32 x = findX(origin.x, size.x, RamStart + mebibyte * Mebibyte);
        if (RamStart + mebibyte * Mebibyte == BaseRamEnd) {
            for (f32 y = origin.y; y < origin.y + size.y; y += 2.0F * DashLength) {
                drawList.AddLine(ImVec2(x, y),
                                 ImVec2(x, std::min(y + DashLength, origin.y + size.y)),
                                 ImGui::GetColorU32(core::palette::Warning));
            }
            continue;
        }
        drawList.AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y),
                         mebibyte % 2 == 0 ? MajorLineColor : MinorLineColor);
    }
}

[[nodiscard]] std::optional<usize> findOutlined(const MemoryLayout& layout,
                                                const std::vector<PlacedRegion>& placed,
                                                std::optional<usize> inspected) {
    if (inspected && layout.regions[*inspected].isBoundary && *inspected > 0) {
        inspected = *inspected - 1;
    }
    while (inspected) {
        if (std::ranges::contains(placed, *inspected, &PlacedRegion::index)) {
            return inspected;
        }
        inspected = findParentRegion(layout.regions, *inspected);
    }
    return std::nullopt;
}

} // namespace

ImVec4 getRegionColor(const RegionKind kind) {
    switch (kind) {
    case RegionKind::Code:
        return {0.420F, 0.463F, 0.525F, 1.0F};
    case RegionKind::Fixed:
        return {0.294F, 0.325F, 0.373F, 1.0F};
    case RegionKind::Gfx:
        return {0.831F, 0.498F, 0.714F, 1.0F};
    case RegionKind::Audio:
        return {0.663F, 0.549F, 0.941F, 1.0F};
    case RegionKind::Heap:
        return {0.169F, 0.188F, 0.220F, 1.0F};
    case RegionKind::Play:
        return core::palette::Accent;
    case RegionKind::Object:
        return {0.247F, 0.757F, 0.788F, 1.0F};
    case RegionKind::GetItem:
        return {0.725F, 0.886F, 1.0F, 1.0F};
    case RegionKind::Framebuffer:
        return {0.788F, 0.541F, 0.384F, 1.0F};
    case RegionKind::Unused:
        return {0.122F, 0.133F, 0.153F, 1.0F};
    }
    return core::palette::Surface;
}

void drawRegionSwatch(const MemoryRegion& region) {
    const f32 side = ImGui::GetFontSize() * 0.7F;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 min(cursor.x, cursor.y + (ImGui::GetFrameHeight() - side) * 0.5F);
    const ImVec2 max(min.x + side, min.y + side);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(getRegionColor(region.kind)), 2.0F);
    if (region.isRemainder) {
        drawList->AddRect(min, max, ImGui::GetColorU32(core::palette::Border), 2.0F);
    }
    ImGui::Dummy(ImVec2(side, ImGui::GetFrameHeight()));
}

void drawMemoryRuler() {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const f32 width = ImGui::GetContentRegionAvail().x;
    const f32 line = ImGui::GetTextLineHeight();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (u32 mebibyte = 0; mebibyte <= RamMebibytes; mebibyte += 2) {
        const u32 address = RamStart + mebibyte * Mebibyte;
        const std::string scale = address == BaseRamEnd
                                      ? std::format("{} MiB · end without Expansion Pak", mebibyte)
                                      : std::format("{} MiB", mebibyte);
        const std::string where = std::format("0x{:08X}", address);
        const f32 x = findX(origin.x, width, address);
        const auto place = [&](const std::string& text) {
            const f32 textWidth = ImGui::CalcTextSize(text.c_str()).x;
            return std::clamp(x - textWidth * 0.5F, origin.x, origin.x + width - textWidth);
        };
        drawList->AddText(ImVec2(place(scale), origin.y),
                          ImGui::GetColorU32(address == BaseRamEnd ? core::palette::Warning
                                                                   : core::palette::Text),
                          scale.c_str());
        drawList->AddText(ImVec2(place(where), origin.y + line),
                          ImGui::GetColorU32(core::palette::TextMuted), where.c_str());
    }
    ImGui::Dummy(ImVec2(width, 2.0F * line));
}

std::optional<usize> drawMemoryStrip(const MemoryLayout& layout,
                                     const std::optional<usize> inspected) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * StripHeight);
    ImGui::InvisibleButton("##strip", size);
    const bool isHovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    drawList->AddRectFilled(origin, end, ImGui::GetColorU32(getRegionColor(RegionKind::Unused)));

    const std::vector<PlacedRegion> placed = placeRegions(layout, origin, size);
    for (const PlacedRegion& region : placed) {
        drawRegion(*drawList, layout.regions[region.index], region.min, region.max);
    }
    drawGridLines(*drawList, origin, size);
    if (const std::optional<usize> outlined = findOutlined(layout, placed, inspected)) {
        const auto region = std::ranges::find(placed, *outlined, &PlacedRegion::index);
        drawList->AddRect(region->min, region->max, ImGui::GetColorU32(core::palette::Text), 0.0F,
                          0, 2.0F);
    }
    if (!isHovered) {
        return std::nullopt;
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    for (const PlacedRegion& region : placed | std::views::reverse) {
        if (mouse.x >= region.min.x && mouse.x < region.max.x && mouse.y >= region.min.y &&
            mouse.y < region.max.y) {
            return region.index;
        }
    }
    return std::nullopt;
}

void drawMemoryInspector(const MemoryLayout& layout, const std::optional<usize> inspected,
                         const std::string_view mapAge) {
    const f32 height =
        ImGui::GetTextLineHeightWithSpacing() * 2.0F + ImGui::GetStyle().WindowPadding.y * 2.0F;
    if (!ImGui::BeginChild("##inspector", ImVec2(0.0F, height), ImGuiChildFlags_Borders,
                           ImGuiWindowFlags_NoScrollbar)) {
        ImGui::EndChild();
        return;
    }
    if (!inspected) {
        ImGui::TextUnformatted("8 MiB of RDRAM");
        ImGui::TextColored(
            core::palette::TextMuted,
            "System heap %s, Play %s, %s for actors  ·  layout from the build's "
            "map of %.*s",
            makeSizeText(layout.systemHeapSize).c_str(), makeSizeText(layout.playSize).c_str(),
            makeSizeText(layout.actorSize).c_str(), static_cast<int>(mapAge.size()), mapAge.data());
        ImGui::EndChild();
        return;
    }
    const MemoryRegion& region = layout.regions[*inspected];
    const f32 lineEnd = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    drawRegionSwatch(region);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (region.setting.empty()) {
        ImGui::TextUnformatted(region.label.c_str());
    } else {
        ImGui::Text("%s  ·  %s", region.label.c_str(), region.setting.c_str());
    }
    const std::string where = region.isBoundary
                                  ? std::format("0x{:08X}", region.start)
                                  : std::format("0x{:08X} – 0x{:08X}  ·  {}", region.start,
                                                region.end, makeSizeText(region.size));
    ImGui::SameLine();
    ImGui::SetCursorPosX(
        std::max(lineEnd - ImGui::CalcTextSize(where.c_str()).x, ImGui::GetCursorPosX()));
    ImGui::TextColored(region.isProblem ? core::palette::Danger : core::palette::Text, "%s",
                       where.c_str());
    ImGui::TextColored(region.isProblem ? core::palette::Danger : core::palette::TextMuted, "%s",
                       region.reason.c_str());
    ImGui::EndChild();
}

std::string makeSizeText(const s64 bytes) {
    const char* sign = bytes < 0 ? "−" : "";
    const f64 magnitude = static_cast<f64>(bytes < 0 ? -bytes : bytes);
    if (magnitude >= static_cast<f64>(Mebibyte)) {
        return std::format("{}{:.2f} MiB", sign, magnitude / Mebibyte);
    }
    if (magnitude >= 1024.0) {
        return std::format("{}{:.1f} KiB", sign, magnitude / 1024.0);
    }
    return std::format("{}{} B", sign, static_cast<s64>(magnitude));
}

} // namespace toki::configure

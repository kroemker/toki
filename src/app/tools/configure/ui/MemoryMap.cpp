#include "MemoryMap.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/tools/configure/domain/ConfigSettings.h"
#include "app/tools/configure/domain/MemoryLayout.h"
#include "app/tools/configure/ui/MemoryStrip.h"
#include "hackeroot/BuildMap.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace toki::configure {

namespace {

constexpr f32 NameWidth = 14.0F;
constexpr f32 GraphWidth = 9.0F;
constexpr f32 NoteWidth = 11.0F;
constexpr f32 TooltipWidth = 28.0F;
constexpr f32 MiniHeight = 0.45F;

[[nodiscard]] std::string makeAgeText(const std::filesystem::file_time_type written) {
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(
                             std::filesystem::file_time_type::clock::now() - written)
                             .count();
    if (minutes < 1) {
        return "just now";
    }
    if (minutes < 60) {
        return std::format("{} min ago", minutes);
    }
    if (minutes < 60 * 24) {
        return std::format("{} h ago", minutes / 60);
    }
    return std::format("{} days ago", minutes / (60 * 24));
}

[[nodiscard]] const ConfigSetting* findSetting(const ConfigGroup& group, const std::string& name) {
    const auto found = std::ranges::find(
        group.settings, name, [](const ConfigSetting& setting) { return setting.define.name; });
    return found == group.settings.end() ? nullptr : &*found;
}

void drawMiniBar(const s64 size, const s64 vanilla, const ImVec4 color) {
    const f32 width = ImGui::GetContentRegionAvail().x;
    const f32 height = ImGui::GetFontSize() * MiniHeight;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 min(cursor.x, cursor.y + (ImGui::GetFrameHeight() - height) * 0.5F);
    const f64 scale = static_cast<f64>(std::max(size, vanilla)) * 1.15;
    const auto findX = [&](const s64 value) {
        return min.x + width * static_cast<f32>(static_cast<f64>(std::max<s64>(value, 0)) / scale);
    };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height),
                            ImGui::GetColorU32(getRegionColor(RegionKind::Unused)), 2.0F);
    drawList->AddRectFilled(min, ImVec2(findX(size), min.y + height), ImGui::GetColorU32(color),
                            2.0F);
    const f32 tick = findX(vanilla);
    drawList->AddLine(ImVec2(tick, min.y - 3.0F), ImVec2(tick, min.y + height + 3.0F),
                      ImGui::GetColorU32(core::palette::Text), 2.0F);
    ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
    ImGui::SetItemTooltip("vanilla %s", makeSizeText(vanilla).c_str());
}

void drawNote(const MemoryRegion& region, const ConfigSetting* setting) {
    ImGui::AlignTextToFramePadding();
    if (setting != nullptr && isOverridden(setting->define)) {
        ImGui::TextColored(core::palette::Warning, "%s build: %s", core::icons::Warning,
                           setting->define.effective.value_or("undefined").c_str());
        ImGui::SetItemTooltip("config_safeguards.h overrides what is written here");
        return;
    }
    const std::string size =
        region.isBoundary ? std::format("0x{:08X}", region.start) : makeSizeText(region.size);
    ImGui::TextColored(region.isProblem ? core::palette::Danger : core::palette::Text, "%s",
                       size.c_str());
    ImGui::SameLine();
    if (region.setting.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "· %.1f%% of RAM",
                           100.0 * static_cast<f64>(std::max<s64>(region.size, 0)) /
                               static_cast<f64>(RamEnd - RamStart));
    } else if (region.vanillaSize && *region.vanillaSize != 0) {
        const f64 ratio = static_cast<f64>(region.size) / static_cast<f64>(*region.vanillaSize);
        if (ratio > 0.995 && ratio < 1.005) {
            ImGui::TextColored(core::palette::TextMuted, "· vanilla");
        } else {
            ImGui::TextColored(core::palette::TextMuted, "· ×%.*f", ratio < 10.0 ? 2 : 1, ratio);
        }
    }
}

} // namespace

void MemoryMap::draw(const ConfigGroup& group,
                     const std::map<std::string, std::string, std::less<>>& values,
                     const std::expected<hackeroot::BuildMap, std::string>* buildMap,
                     const DrawValue& drawValue) {
    hovered.reset();
    if (buildMap == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Reading the build's map…");
        drawTable(group, nullptr, values, drawValue);
        return;
    }
    if (!*buildMap) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Build once to see the memory layout: it is placed by the build's map. "
                           "(%s)",
                           buildMap->error().c_str());
        drawTable(group, nullptr, values, drawValue);
        return;
    }
    const std::expected<MemoryLayout, std::string> layout =
        makeMemoryLayout(group.settings, values, (*buildMap)->symbols);
    if (!layout) {
        ImGui::TextColored(core::palette::Danger, "%s %s", core::icons::Warning,
                           layout.error().c_str());
        drawTable(group, nullptr, values, drawValue);
        return;
    }
    if (inspected && *inspected >= layout->regions.size()) {
        inspected.reset();
    }
    drawMemoryRuler();
    if (const std::optional<usize> overStrip = drawMemoryStrip(*layout, inspected)) {
        hovered = overStrip;
    }
    drawMemoryInspector(*layout, inspected, makeAgeText((*buildMap)->written));
    drawFilter();
    drawTable(group, &*layout, values, drawValue);
    inspected = hovered;
}

void MemoryMap::drawFilter() {
    if (ImGui::RadioButton("All regions", showsDerived)) {
        showsDerived = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Settings only", !showsDerived)) {
        showsDerived = false;
    }
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted,
                       "  Rows in address order; muted rows follow from the others.");
}

void MemoryMap::drawTable(const ConfigGroup& group, const MemoryLayout* layout,
                          const std::map<std::string, std::string, std::less<>>& values,
                          const DrawValue& drawValue) {
    if (!ImGui::BeginTable("##memory", 5,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    const f32 em = ImGui::GetFontSize();
    ImGui::TableSetupColumn("swatch", ImGuiTableColumnFlags_WidthFixed, em);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, em * NameWidth);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("graph", ImGuiTableColumnFlags_WidthFixed, em * GraphWidth);
    ImGui::TableSetupColumn("note", ImGuiTableColumnFlags_WidthFixed, em * NoteWidth);

    std::vector<std::optional<usize>> rowRegions;
    std::vector<const ConfigSetting*> placed;
    if (layout != nullptr) {
        for (usize i = 0; i < layout->regions.size(); ++i) {
            const MemoryRegion& region = layout->regions[i];
            const ConfigSetting* setting =
                region.setting.empty() ? nullptr : findSetting(group, region.setting);
            if (setting == nullptr && !showsDerived) {
                continue;
            }
            ImGui::PushID(static_cast<int>(i));
            drawRegionRow(*layout, i, setting, values, drawValue);
            ImGui::PopID();
            rowRegions.emplace_back(i);
            placed.push_back(setting);
        }
    }
    for (const ConfigSetting& setting : group.settings) {
        if (std::ranges::contains(placed, &setting)) {
            continue;
        }
        ImGui::PushID(setting.define.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        drawSettingName(setting.label, setting, values, {});
        ImGui::TableNextColumn();
        drawValue(setting);
        ImGui::PopID();
        rowRegions.emplace_back(std::nullopt);
    }
    const int row = ImGui::TableGetHoveredRow();
    if (row >= 0 && static_cast<usize>(row) < rowRegions.size() && rowRegions[row]) {
        hovered = rowRegions[row];
    }
    ImGui::EndTable();
}

void MemoryMap::drawRegionRow(const MemoryLayout& layout, const usize index,
                              const ConfigSetting* setting,
                              const std::map<std::string, std::string, std::less<>>& values,
                              const DrawValue& drawValue) {
    const MemoryRegion& region = layout.regions[index];
    ImGui::TableNextRow();
    if (inspected == index) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ImGui::GetColorU32(core::palette::SurfaceHovered));
    }
    ImGui::TableNextColumn();
    drawRegionSwatch(region);
    ImGui::TableNextColumn();
    if (showsDerived) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             static_cast<f32>(region.depth) * ImGui::GetFontSize());
    }
    if (setting != nullptr) {
        drawSettingName(region.label, *setting, values, region.reason);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(region.isProblem ? core::palette::Danger : core::palette::TextMuted,
                           "%s", region.label.c_str());
        ImGui::SetItemTooltip("%s\n%s", region.reason.c_str(), region.source.c_str());
    }
    ImGui::TableNextColumn();
    if (setting != nullptr) {
        drawValue(*setting);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(core::palette::TextDisabled, "derived");
    }
    ImGui::TableNextColumn();
    if (setting != nullptr && region.vanillaSize) {
        drawMiniBar(region.size, *region.vanillaSize, getRegionColor(region.kind));
    } else if (setting == nullptr) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(core::palette::TextMuted, "0x%08X", region.start);
        ImGui::SetItemTooltip("0x%08X – 0x%08X", region.start, region.end);
    }
    ImGui::TableNextColumn();
    drawNote(region, setting);
}

void MemoryMap::drawSettingName(const std::string& label, const ConfigSetting& setting,
                                const std::map<std::string, std::string, std::less<>>& values,
                                const std::string& reason) {
    const auto edited = values.find(setting.define.name);
    const bool isChanged = edited != values.end() && edited->second != setting.define.written;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(isChanged ? core::palette::Accent : core::palette::Text, "%s",
                       label.c_str());
    if (!ImGui::BeginItemTooltip()) {
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * TooltipWidth);
    if (!reason.empty()) {
        ImGui::TextUnformatted(reason.c_str());
    }
    if (!setting.define.comment.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "%s", setting.define.comment.c_str());
    }
    ImGui::TextColored(core::palette::TextMuted, "%s  ·  %s:%zu", setting.define.name.c_str(),
                       setting.define.file.generic_string().c_str(), setting.define.line);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

} // namespace toki::configure

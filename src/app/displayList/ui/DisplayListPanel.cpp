#include "DisplayListPanel.h"

#include "app/core/domain/TextSearch.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/displayList/Types.h"
#include "app/displayList/domain/FrameInspection.h"
#include "app/displayList/domain/RdpStateText.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "gfx/ImageTexture.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Workspace.h"
#include "n64/CombineMode.h"
#include "n64/CommandDecoder.h"
#include "n64/OtherMode.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "z64/GbiNames.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace toki::displayList {

namespace {

constexpr double RefreshInterval = 0.5;
constexpr float RowsShare = 0.55F;
constexpr float IndentWidth = 1.2F;
constexpr float TooltipWidth = 30.0F;
constexpr f32 PreviewSize = 96.0F;

[[nodiscard]] ImVec4 getKindColor(const n64::CommandKind kind) {
    switch (kind) {
    case n64::CommandKind::Geometry:
        return ImVec4(0.39F, 0.71F, 1.0F, 1.0F);
    case n64::CommandKind::Texture:
        return ImVec4(1.0F, 0.78F, 0.31F, 1.0F);
    case n64::CommandKind::Material:
        return ImVec4(0.78F, 0.51F, 1.0F, 1.0F);
    case n64::CommandKind::RenderMode:
        return ImVec4(0.39F, 0.86F, 0.55F, 1.0F);
    case n64::CommandKind::Transform:
        return ImVec4(1.0F, 0.51F, 0.43F, 1.0F);
    case n64::CommandKind::Control:
        return core::palette::Text;
    case n64::CommandKind::Sync:
    case n64::CommandKind::Other:
        return core::palette::TextMuted;
    }
    return core::palette::Text;
}

void drawWrappedTooltip(const std::string_view first, const std::string_view second) {
    if (!ImGui::BeginItemTooltip()) {
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * TooltipWidth);
    ImGui::TextUnformatted(first.data(), first.data() + first.size());
    if (!second.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(core::palette::Accent, "%.*s", static_cast<int>(second.size()),
                           second.data());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

[[nodiscard]] std::string makeFieldNote(const n64::CommandField& field) {
    std::string note(field.description);
    if (field.kind == n64::FieldKind::GeometryMode) {
        for (u32 bit = 1; bit != 0; bit <<= 1) {
            const std::string_view meaning = n64::getGeometryModeDescription(bit);
            if ((field.value & bit) != 0 && !meaning.empty()) {
                note += std::format("\n• {}", meaning);
            }
        }
    }
    if (field.kind == n64::FieldKind::CombinerInput) {
        note += std::format(
            "\n{}", n64::getCombinerInputDescription(static_cast<n64::CombinerInput>(field.value)));
    }
    return note;
}

} // namespace

DisplayListPanel::DisplayListPanel(const gfx::Device& device,
                                   viewport::ViewportPanel& viewportPanel,
                                   const workspace::WorkspaceService& workspaceService,
                                   const core::Fonts& fonts)
    : Panel("displayList", "Display List", core::icons::ListBullets, core::DockSlot::Right),
      device(device), viewportPanel(viewportPanel), workspaceService(workspaceService),
      fonts(fonts), plainNames(cparser::MacroTable{}) {}

void DisplayListPanel::updateNames() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace != namedWorkspace && !readingNames.valid()) {
        namedWorkspace = workspace;
        names.reset();
        rowTexts.clear();
        if (workspace != nullptr) {
            readingNames = std::async(
                std::launch::async,
                [shared = workspaceService.getSharedWorkspace()]() -> std::optional<z64::GbiNames> {
                    if (shared == nullptr) {
                        return std::nullopt;
                    }
                    const std::expected<hackeroot::Prelude, base::Diagnostic> gbi =
                        hackeroot::readGbi(*shared);
                    return gbi ? std::optional(z64::GbiNames(gbi->macros)) : std::nullopt;
                });
        }
    }
    if (readingNames.valid() &&
        readingNames.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        names = readingNames.get();
        rowTexts.clear();
    }
}

void DisplayListPanel::updateFrame() {
    const viewport::TracedFrame* traced = viewportPanel.findTracedFrame();
    const bool isNewer = traced != nullptr &&
                         (shownFrame == nullptr || traced->generation != shownFrame->generation);
    if (!isNewer || (shownFrame != nullptr && !isFollowing) ||
        ImGui::GetTime() - refreshedAt < RefreshInterval) {
        return;
    }
    refreshedAt = ImGui::GetTime();
    inspection.reset();
    shownFrame = std::make_unique<viewport::TracedFrame>(*traced);
    inspection = std::make_unique<FrameInspection>(shownFrame->frame, shownFrame->content);
    rowTexts.clear();
    areRowsStale = true;
    if (selected && *selected >= inspection->getRows().size()) {
        selected.reset();
    }
}

void DisplayListPanel::draw() {
    updateNames();
    updateFrame();
    if (inspection == nullptr) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Open a scene or an object; its frame appears here once drawn.");
        return;
    }
    drawToolbar();
    if (areRowsStale) {
        listVisibleRows();
    }
    const float rowsHeight = ImGui::GetContentRegionAvail().y * RowsShare;
    if (ImGui::BeginChild("##rows", ImVec2(0.0F, rowsHeight), ImGuiChildFlags_Borders)) {
        drawRows();
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("##details")) {
        drawDetails();
    }
    ImGui::EndChild();
}

void DisplayListPanel::drawToolbar() {
    ImGui::Checkbox("Follow the viewport", &isFollowing);
    ImGui::SetItemTooltip("Read every new frame the viewport draws; off, the frame shown is held");
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "%zu commands, %zu drawn",
                       inspection->getRows().size(), shownFrame->frame.rdpResult.draws.size());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##filter", "Filter by command, as gsSPVertex", &filter)) {
        areRowsStale = true;
    }
}

void DisplayListPanel::listVisibleRows() {
    areRowsStale = false;
    visibleRows.clear();
    const std::vector<InspectedCommand>& rows = inspection->getRows();
    if (!filter.empty()) {
        for (usize row = 0; row < rows.size(); ++row) {
            if (core::containsIgnoringCase(rows[row].command.name, filter)) {
                visibleRows.push_back(VisibleRow{.row = row});
            }
        }
        return;
    }
    std::map<usize, usize> groupStarts;
    const std::vector<LoadGroup>& groups = inspection->getLoadGroups();
    for (usize group = 0; group < groups.size(); ++group) {
        groupStarts.emplace(groups[group].first, group);
    }
    usize row = 0;
    while (row < rows.size()) {
        if (const auto group = groupStarts.find(row); group != groupStarts.end()) {
            visibleRows.push_back(VisibleRow{.row = row, .group = group->second});
            if (!expandedGroups.contains(group->second)) {
                row = groups[group->second].end;
                continue;
            }
            for (usize member = row; member < groups[group->second].end; ++member) {
                visibleRows.push_back(VisibleRow{.row = member});
            }
            row = groups[group->second].end;
            continue;
        }
        visibleRows.push_back(VisibleRow{.row = row});
        const bool isCall = rows[row].end > row + 1;
        row = isCall && !expanded.contains(row) ? rows[row].end : row + 1;
    }
}

const std::string& DisplayListPanel::findRowText(const usize row) {
    auto found = rowTexts.find(row);
    if (found == rowTexts.end()) {
        const z64::GbiNames& named = names ? *names : plainNames;
        found = rowTexts
                    .emplace(row, named.makeCommandText(inspection->getRows()[row].command,
                                                        makeAddressNamer(row)))
                    .first;
    }
    return found->second;
}

z64::FindAddressName DisplayListPanel::makeAddressNamer(const usize row) const {
    return [this, row](const u32 segmented) { return inspection->findAddressName(row, segmented); };
}

void DisplayListPanel::drawRows() {
    ImGui::PushFont(fonts.getMono(), core::Fonts::MonoSize);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visibleRows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            ImGui::PushID(i);
            drawRow(visibleRows[static_cast<usize>(i)]);
            ImGui::PopID();
        }
    }
    ImGui::PopFont();
}

void DisplayListPanel::drawRow(const VisibleRow& visible) {
    const InspectedCommand& row = inspection->getRows()[visible.row];
    const float indent = ImGui::GetFontSize() * IndentWidth;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (static_cast<float>(row.depth) * indent));
    const bool isCall = row.end > visible.row + 1;
    if (visible.group || (isCall && filter.empty())) {
        const bool isOpen = visible.group ? expandedGroups.contains(*visible.group)
                                          : expanded.contains(visible.row);
        if (ImGui::SmallButton(isOpen ? "-" : "+")) {
            std::set<usize>& openRows = visible.group ? expandedGroups : expanded;
            const usize key = visible.group ? *visible.group : visible.row;
            if (isOpen) {
                openRows.erase(key);
            } else {
                openRows.insert(key);
            }
            areRowsStale = true;
        }
        ImGui::SameLine();
    }
    if (visible.group) {
        const LoadGroup& group = inspection->getLoadGroups()[*visible.group];
        ImGui::TextColored(getKindColor(n64::CommandKind::Texture), "%s  (%zu commands)",
                           group.label.c_str(), group.end - group.first);
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, getKindColor(row.command.kind));
    const std::string& text = findRowText(visible.row);
    if (ImGui::Selectable(text.c_str(), selected == visible.row)) {
        selected = visible.row;
    }
    ImGui::PopStyleColor();
    const z64::GbiNames& named = names ? *names : plainNames;
    drawWrappedTooltip(row.command.description, inspection->describeEffect(visible.row, named));
}

void DisplayListPanel::drawDetails() {
    if (!selected) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Pick a command to see its fields and what it did.");
        return;
    }
    const usize row = *selected;
    const InspectedCommand& inspected = inspection->getRows()[row];
    const z64::GbiNames& named = names ? *names : plainNames;
    ImGui::PushFont(fonts.getMono(), core::Fonts::MonoSize);
    ImGui::TextWrapped("%s", findRowText(row).c_str());
    ImGui::PopFont();
    ImGui::TextWrapped("%.*s", static_cast<int>(inspected.command.description.size()),
                       inspected.command.description.data());
    if (const std::string effect = inspection->describeEffect(row, named); !effect.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(core::palette::Accent, "%s %s", core::icons::Lightning, effect.c_str());
    }
    if (!inspected.command.fields.empty()) {
        drawFields(row);
    }
    drawState(row);
}

void DisplayListPanel::drawFields(const usize row) {
    const InspectedCommand& inspected = inspection->getRows()[row];
    const z64::GbiNames& named = names ? *names : plainNames;
    ImGui::SeparatorText("Fields");
    if (!ImGui::BeginTable("##fields", 2,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    for (const n64::CommandField& field : inspected.command.fields) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(field.name.data(), field.name.data() + field.name.size());
        drawWrappedTooltip(makeFieldNote(field), {});
        ImGui::TableNextColumn();
        const std::string value = named.makeValueText(field, makeAddressNamer(row));
        ImGui::TextWrapped("%s", value.c_str());
        drawWrappedTooltip(makeFieldNote(field), {});
    }
    ImGui::EndTable();
}

void DisplayListPanel::drawState(const usize row) {
    const n64::RdpState* state = inspection->findDrawState(row);
    if (state == nullptr) {
        return;
    }
    ImGui::SeparatorText("What it drew with");
    for (const StateSection& section : describeRdpState(*state)) {
        if (section.lines.empty()) {
            continue;
        }
        const bool isOpen = ImGui::CollapsingHeader(
            section.title.c_str(),
            section.title == "Colour combiner" ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        drawWrappedTooltip(findGlossaryEntry(section.term), {});
        if (!isOpen ||
            !ImGui::BeginTable(section.title.c_str(), 2,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            continue;
        }
        for (const StateLine& line : section.lines) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(line.label.c_str());
            drawWrappedTooltip(line.description, {});
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", line.value.c_str());
        }
        ImGui::EndTable();
    }
    drawTilePreviews(row, *state);
}

void DisplayListPanel::drawTilePreviews(const usize row, const n64::RdpState& state) {
    if (previewRow != row || previewGeneration != shownFrame->generation) {
        previewRow = row;
        previewGeneration = shownFrame->generation;
        previews.clear();
        const std::vector<usize> draws = inspection->listDraws(row);
        const auto* triangle =
            draws.empty() ? nullptr
                          : std::get_if<n64::Triangle>(
                                &shownFrame->frame.rdpResult.draws[draws.front()].primitive);
        if (triangle != nullptr && triangle->textured &&
            state.textureMemory < shownFrame->frame.rdpResult.textureMemories.size()) {
            const bool isTwo = state.otherMode.getCycleType() == n64::CycleType::Two;
            for (usize tile = triangle->tile; tile <= triangle->tile + (isTwo ? 1U : 0U); ++tile) {
                const std::optional<TilePreview> preview = makeTilePreview(
                    state, shownFrame->frame.rdpResult.textureMemories[state.textureMemory],
                    tile & 7U);
                if (!preview) {
                    continue;
                }
                std::expected<gfx::ImageTexture, base::Diagnostic> uploaded =
                    gfx::ImageTexture::create(device, preview->width, preview->height,
                                              preview->pixels);
                if (uploaded) {
                    previews.push_back(
                        UploadedTile{.tile = tile & 7U, .texture = std::move(*uploaded)});
                }
            }
        }
    }
    if (previews.empty()) {
        return;
    }
    ImGui::SeparatorText("Textures it read");
    drawWrappedTooltip(findGlossaryEntry("texture memory"), {});
    for (const UploadedTile& uploaded : previews) {
        const f32 scale =
            std::max(1.0F, PreviewSize / static_cast<f32>(std::max(uploaded.texture.getWidth(),
                                                                   uploaded.texture.getHeight())));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto textureId = static_cast<ImTextureID>(
            reinterpret_cast<std::uintptr_t>(uploaded.texture.getTexture()));
        ImGui::BeginGroup();
        ImGui::Image(textureId, ImVec2(static_cast<f32>(uploaded.texture.getWidth()) * scale,
                                       static_cast<f32>(uploaded.texture.getHeight()) * scale));
        ImGui::TextColored(core::palette::TextMuted, "tile %zu, %u×%u", uploaded.tile,
                           uploaded.texture.getWidth(), uploaded.texture.getHeight());
        ImGui::EndGroup();
        ImGui::SameLine();
    }
    ImGui::NewLine();
}

} // namespace toki::displayList

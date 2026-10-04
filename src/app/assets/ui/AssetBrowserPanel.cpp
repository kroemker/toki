#include "AssetBrowserPanel.h"

#include "app/assets/Types.h"
#include "app/assets/domain/AssetReader.h"
#include "app/assets/domain/AssetReport.h"
#include "app/assets/domain/AssetRows.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <SDL3/SDL_misc.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::assets {

namespace {

constexpr float DetailsLines = 8.5F;
constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                       ImGuiTableFlags_BordersInnerV;

struct SortOrder {
    AssetColumn column = AssetColumn::Name;
    bool ascending = true;
};

SortOrder findSortOrder() {
    const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
    if (specs == nullptr || specs->SpecsCount == 0) {
        return {};
    }
    const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
    return {.column = static_cast<AssetColumn>(spec.ColumnUserID),
            .ascending = spec.SortDirection != ImGuiSortDirection_Descending};
}

void setupColumn(const char* label, const AssetColumn column, const ImGuiTableColumnFlags flags) {
    ImGui::TableSetupColumn(label, flags, 0.0F, static_cast<ImGuiID>(column));
}

template <typename Entry>
std::string formatId(const Entry* entry, const int digits) {
    return entry != nullptr ? std::format("0x{:0{}X}", entry->id, digits) : std::string("—");
}

void drawSource(const hackeroot::AssetDirectory& asset) {
    if (asset.isExtracted) {
        ImGui::TextColored(core::palette::TextMuted, "vanilla");
    } else {
        ImGui::TextColored(core::palette::Warning, "fast64");
    }
}

void drawLabel(const char* label) {
    ImGui::TableNextColumn();
    ImGui::TextColored(core::palette::TextMuted, "%s", label);
    ImGui::TableNextColumn();
}

std::string makeFileUrl(const std::filesystem::path& directory) {
    const std::string path = core::encodeUtf8(directory);
    std::string url = path.starts_with("//") ? "file:" : "file:///";
    for (const char c : path) {
        if (c == ' ' || c == '#' || c == '%') {
            url += std::format("%{:02X}", static_cast<unsigned char>(c));
        } else {
            url += c;
        }
    }
    return url;
}

std::string findRelativePath(const hackeroot::Workspace& workspace,
                             const hackeroot::AssetDirectory& asset) {
    return core::encodeUtf8(asset.directory.lexically_relative(workspace.getRoot()));
}

} // namespace

AssetBrowserPanel::AssetBrowserPanel(const workspace::WorkspaceService& workspaceService,
                                     AssetReader& assetReader)
    : Panel("assets", "Assets", core::icons::FolderOpen, core::DockSlot::Left),
      workspaceService(workspaceService), assetReader(assetReader) {}

void AssetBrowserPanel::draw() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "%s",
                           workspaceService.isLoading()
                               ? "Reading the checkout…"
                               : "Open a HackerOoT checkout to browse its assets.");
        return;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter by name, folder or enum…", filter.data(),
                             filter.size());
    if (ImGui::BeginTabBar("##kinds")) {
        if (ImGui::BeginTabItem("Scenes")) {
            kind = Kind::Scenes;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Objects")) {
            kind = Kind::Objects;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * DetailsLines;
    const float tableHeight = std::max(ImGui::GetContentRegionAvail().y - detailsHeight,
                                       ImGui::GetTextLineHeightWithSpacing() * 4.0F);
    if (kind == Kind::Scenes) {
        drawScenes(*workspace, tableHeight);
        ImGui::Separator();
        drawSceneDetails(*workspace);
    } else {
        drawObjects(*workspace, tableHeight);
        ImGui::Separator();
        drawObjectDetails(*workspace);
    }
}

void AssetBrowserPanel::drawAssetRow(const hackeroot::AssetDirectory& asset, const char* name) {
    ImGui::PushID(core::encodeUtf8(asset.directory).c_str());
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanAllColumns;
    if (selected == asset.directory) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    ImGui::TreeNodeEx("##row", flags, "%s", name);
    if (ImGui::IsItemClicked()) {
        selected = asset.directory;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            openAsset(asset);
        }
    }
    if (ImGui::BeginPopupContextItem()) {
        selected = asset.directory;
        if (ImGui::MenuItem("Open")) {
            openAsset(asset);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void AssetBrowserPanel::drawScenes(const hackeroot::Workspace& workspace, const float height) {
    if (!ImGui::BeginTable("##scenes", 5, TableFlags, ImVec2(0.0F, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    setupColumn("Name", AssetColumn::Name,
                ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
    setupColumn("Folder", AssetColumn::Folder, ImGuiTableColumnFlags_WidthFixed);
    setupColumn("ID", AssetColumn::Id, ImGuiTableColumnFlags_WidthFixed);
    setupColumn("Rooms", AssetColumn::Rooms, ImGuiTableColumnFlags_WidthFixed);
    setupColumn("Source", AssetColumn::Source, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    const SortOrder order = findSortOrder();
    const std::vector<SceneRow> rows = sortSceneRows(
        makeSceneRows(workspace, std::string_view(filter.data())), order.column, order.ascending);
    if (rows.empty()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(core::palette::TextMuted, "No scenes match.");
    }
    usize start = 0;
    while (start < rows.size()) {
        usize end = start;
        while (end < rows.size() && rows[end].category == rows[start].category) {
            ++end;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string header = std::format("{} ({})", rows[start].category, end - start);
        const bool expanded =
            ImGui::TreeNodeEx(rows[start].category.c_str(),
                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns,
                              "%s", header.c_str());
        for (usize i = start; expanded && i < end; ++i) {
            const SceneRow& row = rows[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawAssetRow(*row.asset, row.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.asset->name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatId(row.entry, 2).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", row.asset->roomCount);
            ImGui::TableNextColumn();
            drawSource(*row.asset);
        }
        if (expanded) {
            ImGui::TreePop();
        }
        start = end;
    }
    ImGui::EndTable();
}

void AssetBrowserPanel::drawObjects(const hackeroot::Workspace& workspace, const float height) {
    if (!ImGui::BeginTable("##objects", 3, TableFlags, ImVec2(0.0F, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    setupColumn("Name", AssetColumn::Name,
                ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
    setupColumn("ID", AssetColumn::Id, ImGuiTableColumnFlags_WidthFixed);
    setupColumn("Source", AssetColumn::Source, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    const SortOrder order = findSortOrder();
    const std::vector<ObjectRow> rows = sortObjectRows(
        makeObjectRows(workspace, std::string_view(filter.data())), order.column, order.ascending);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const ObjectRow& row = rows[static_cast<usize>(index)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawAssetRow(*row.asset, row.asset->name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatId(row.entry, 4).c_str());
            ImGui::TableNextColumn();
            drawSource(*row.asset);
        }
    }
    ImGui::EndTable();
}

void AssetBrowserPanel::drawSceneDetails(const hackeroot::Workspace& workspace) {
    const std::vector<SceneRow> rows = makeSceneRows(workspace, {});
    const auto row = std::ranges::find_if(
        rows, [&](const SceneRow& candidate) { return candidate.asset->directory == selected; });
    if (row == rows.end()) {
        ImGui::TextColored(core::palette::TextMuted, "Select a scene to see its details.");
        return;
    }
    ImGui::TextUnformatted(row->name.c_str());
    if (row->entry != nullptr) {
        ImGui::SameLine();
        ImGui::TextColored(core::palette::TextMuted, "· %s", row->entry->name.c_str());
    }
    if (ImGui::BeginTable("##sceneDetails", 2, ImGuiTableFlags_SizingFixedFit)) {
        drawLabel("Enum");
        if (row->entry != nullptr) {
            ImGui::Text("%s (%s)", row->entry->enumName.c_str(), formatId(row->entry, 2).c_str());
        } else {
            ImGui::TextColored(core::palette::Warning, "not in the scene table");
        }
        drawLabel("Folder");
        ImGui::TextUnformatted(findRelativePath(workspace, *row->asset).c_str());
        if (row->entry != nullptr) {
            drawLabel("Title card");
            ImGui::Text("%s · draw config %s", row->entry->titleCard.c_str(),
                        row->entry->drawConfig.c_str());
        }
        drawLabel("Rooms");
        ImGui::Text("%zu · %zu entrances lead here", row->asset->roomCount, row->entrances);
        drawLabel("Last opened");
        drawReport(*row->asset);
        ImGui::EndTable();
    }
    drawActions(*row->asset);
}

void AssetBrowserPanel::drawObjectDetails(const hackeroot::Workspace& workspace) {
    const std::vector<ObjectRow> rows = makeObjectRows(workspace, {});
    const auto row = std::ranges::find_if(
        rows, [&](const ObjectRow& candidate) { return candidate.asset->directory == selected; });
    if (row == rows.end()) {
        ImGui::TextColored(core::palette::TextMuted, "Select an object to see its details.");
        return;
    }
    ImGui::TextUnformatted(row->asset->name.c_str());
    if (ImGui::BeginTable("##objectDetails", 2, ImGuiTableFlags_SizingFixedFit)) {
        drawLabel("Enum");
        if (row->entry != nullptr) {
            ImGui::Text("%s (%s)", row->entry->enumName.c_str(), formatId(row->entry, 4).c_str());
        } else {
            ImGui::TextColored(core::palette::Warning, "not in the object table");
        }
        drawLabel("Folder");
        ImGui::TextUnformatted(findRelativePath(workspace, *row->asset).c_str());
        drawLabel("Last opened");
        drawReport(*row->asset);
        ImGui::EndTable();
    }
    drawActions(*row->asset);
}

void AssetBrowserPanel::drawReport(const hackeroot::AssetDirectory& asset) {
    const std::optional<std::string> reading = assetReader.findReadingName();
    if (reading && *reading == asset.name) {
        ImGui::TextColored(core::palette::Accent, "reading…");
        return;
    }
    const std::optional<ReportCounts> counts = assetReader.findReportCounts(asset.directory);
    if (!counts) {
        ImGui::TextColored(core::palette::TextMuted, "not opened yet");
        return;
    }
    ImGui::Text("%zu files, %zu declarations, %zu images ·", counts->files, counts->declarations,
                counts->images);
    ImGui::SameLine();
    ImVec4 color = core::palette::Success;
    if (counts->errors > 0) {
        color = core::palette::Danger;
    } else if (counts->warnings > 0) {
        color = core::palette::Warning;
    }
    ImGui::TextColored(color, "%zu errors, %zu warnings", counts->errors, counts->warnings);
}

void AssetBrowserPanel::drawActions(const hackeroot::AssetDirectory& asset) {
    if (ImGui::Button("Open")) {
        openAsset(asset);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reveal in folder")) {
        SDL_OpenURL(makeFileUrl(asset.directory).c_str());
    }
}

void AssetBrowserPanel::openAsset(const hackeroot::AssetDirectory& asset) {
    assetReader.open(workspaceService.getSharedWorkspace(), asset);
}

} // namespace toki::assets

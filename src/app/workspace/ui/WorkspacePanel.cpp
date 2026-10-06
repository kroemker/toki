#include "WorkspacePanel.h"

#include "app/core/domain/TextSearch.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/workspace/Types.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::workspace {

namespace {

template <usize Columns>
using Row = std::array<std::string, Columns>;

template <usize Columns>
[[nodiscard]] bool matchesFilter(const Row<Columns>& row, std::string_view filter) {
    return std::ranges::any_of(row, [filter](const std::string& cell) {
        return core::containsIgnoringCase(cell, filter);
    });
}

template <usize Columns>
void drawTable(const char* id, const std::array<const char*, Columns>& headers,
               const std::vector<Row<Columns>>& rows, std::string_view filter) {
    std::vector<const Row<Columns>*> visible;
    for (const Row<Columns>& row : rows) {
        if (matchesFilter(row, filter)) {
            visible.push_back(&row);
        }
    }

    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable(id, static_cast<int>(Columns), Flags)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    for (const char* header : headers) {
        ImGui::TableSetupColumn(header);
    }
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            ImGui::TableNextRow();
            for (const std::string& cell : *visible[static_cast<usize>(index)]) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cell.c_str());
            }
        }
    }
    ImGui::EndTable();
}

[[nodiscard]] std::vector<Row<4>> makeSceneRows(std::span<const hackeroot::SceneEntry> scenes) {
    std::vector<Row<4>> rows;
    for (const hackeroot::SceneEntry& scene : scenes) {
        rows.push_back(
            {std::format("0x{:02X}", scene.id), scene.name, scene.enumName, scene.titleCard});
    }
    return rows;
}

[[nodiscard]] std::vector<Row<3>> makeActorRows(std::span<const hackeroot::ActorEntry> actors) {
    std::vector<Row<3>> rows;
    for (const hackeroot::ActorEntry& actor : actors) {
        rows.push_back({std::format("0x{:04X}", actor.id),
                        actor.isUnset ? std::string("(unset)") : actor.name, actor.enumName});
    }
    return rows;
}

[[nodiscard]] std::vector<Row<3>> makeObjectRows(std::span<const hackeroot::ObjectEntry> objects) {
    std::vector<Row<3>> rows;
    for (const hackeroot::ObjectEntry& object : objects) {
        rows.push_back({std::format("0x{:04X}", object.id),
                        object.isUnset ? std::string("(unset)") : object.name, object.enumName});
    }
    return rows;
}

[[nodiscard]] std::vector<Row<4>>
makeEntranceRows(std::span<const hackeroot::EntranceEntry> entrances) {
    std::vector<Row<4>> rows;
    for (const hackeroot::EntranceEntry& entrance : entrances) {
        rows.push_back({std::format("0x{:03X}", entrance.id), entrance.enumName,
                        entrance.sceneEnumName, entrance.spawn});
    }
    return rows;
}

[[nodiscard]] std::vector<Row<3>>
makeAssetRows(std::span<const hackeroot::AssetDirectory> directories) {
    std::vector<Row<3>> rows;
    for (const hackeroot::AssetDirectory& directory : directories) {
        rows.push_back({directory.name, directory.isExtracted ? "extracted" : "checkout",
                        core::encodeUtf8(directory.directory)});
    }
    return rows;
}

void drawTab(const char* label, usize count, auto drawContents) {
    const std::string title = std::format("{} ({})###{}", label, count, label);
    if (ImGui::BeginTabItem(title.c_str())) {
        drawContents();
        ImGui::EndTabItem();
    }
}

void drawSummary(const hackeroot::Workspace& workspace) {
    ImGui::TextUnformatted(core::encodeUtf8(workspace.getRoot()).c_str());
    ImGui::TextColored(core::palette::TextMuted, "VERSION %s",
                       workspace.getBuildDefines().version.c_str());
}

} // namespace

WorkspacePanel::WorkspacePanel(const WorkspaceService& workspaceService)
    : Panel("workspace", "Workspace", core::icons::Database, core::DockSlot::Left),
      workspaceService(workspaceService) {}

void WorkspacePanel::draw() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspaceService.isLoading()) {
        ImGui::TextColored(core::palette::TextMuted, "%s Reading the checkout…",
                           core::icons::ArrowsClockwise);
    }
    if (workspace == nullptr) {
        if (!workspaceService.isLoading()) {
            ImGui::TextColored(core::palette::TextMuted, "No checkout is open.");
        }
        return;
    }

    drawSummary(*workspace);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##filter", "Filter…", filter.data(), filter.size());
    drawTables(*workspace);
}

void WorkspacePanel::drawTables(const hackeroot::Workspace& workspace) const {
    const hackeroot::Tables& tables = workspace.getTables();
    const hackeroot::Assets& assets = workspace.getAssets();
    const std::string_view wanted(filter.data());

    if (!ImGui::BeginTabBar("##tables")) {
        return;
    }
    drawTab("Scenes", tables.scenes.size(), [&] {
        drawTable<4>("##scenes", {"ID", "Name", "Enum", "Title card"}, makeSceneRows(tables.scenes),
                     wanted);
    });
    drawTab("Actors", tables.actors.size(), [&] {
        drawTable<3>("##actors", {"ID", "Name", "Enum"}, makeActorRows(tables.actors), wanted);
    });
    drawTab("Objects", tables.objects.size(), [&] {
        drawTable<3>("##objects", {"ID", "Name", "Enum"}, makeObjectRows(tables.objects), wanted);
    });
    drawTab("Entrances", tables.entrances.size(), [&] {
        drawTable<4>("##entrances", {"ID", "Enum", "Scene", "Spawn"},
                     makeEntranceRows(tables.entrances), wanted);
    });
    drawTab("Scene folders", assets.scenes.size(), [&] {
        drawTable<3>("##sceneFolders", {"Name", "From", "Folder"}, makeAssetRows(assets.scenes),
                     wanted);
    });
    drawTab("Object folders", assets.objects.size(), [&] {
        drawTable<3>("##objectFolders", {"Name", "From", "Folder"}, makeAssetRows(assets.objects),
                     wanted);
    });
    ImGui::EndTabBar();
}

} // namespace toki::workspace

#include "ActorsPanel.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorRow.h"
#include "app/actor/domain/DatabaseEdits.h"
#include "app/actor/domain/DatabaseStore.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "base/Diagnostic.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <array>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr double SaveDelay = 0.6;
constexpr float ListWidth = 18.0F;
constexpr const char* DerivePrompt = "Derive this actor?";

constexpr std::array<std::pair<RowFilter, const char*>, 3> FilterNames{{
    {RowFilter::All, "All actors"},
    {RowFilter::WithEntry, "With an entry"},
    {RowFilter::Missing, "Without an entry"},
}};

[[nodiscard]] const char* findRowIcon(const ActorRow& row) {
    return row.hasEntry ? core::icons::CheckCircle : core::icons::Question;
}

[[nodiscard]] ImVec4 findRowColor(const ActorRow& row) {
    return row.hasEntry ? core::palette::Success : core::palette::TextDisabled;
}

[[nodiscard]] std::string formatEntry(const ActorEntry& entry) {
    ActorDatabase database;
    database.entries.emplace(entry.actor, entry);
    return formatActorDatabase(database);
}

} // namespace

ActorsPanel::ActorsPanel(const workspace::WorkspaceService& workspaceService,
                         DatabaseStore& databaseStore, SymbolCatalog& symbolCatalog)
    : Panel("actors", "Actors", core::icons::Users, core::DockSlot::Right),
      workspaceService(workspaceService), databaseStore(databaseStore),
      symbolCatalog(symbolCatalog) {}

void ActorsPanel::setDatabase(DatabaseSnapshot read) {
    snapshot = std::move(read);
    areRowsStale = true;
    if (selected && !isDirty) {
        select(*selected);
    }
}

void ActorsPanel::openActor(const std::string& actor) {
    setOpen(true);
    ImGui::SetWindowFocus(getWindowTitle().c_str());
    if (isDirty) {
        saveDraft();
    }
    select(actor);
}

void ActorsPanel::select(const std::string& actor) {
    selected = actor;
    draft.reset();
    if (!snapshot) {
        return;
    }
    if (const ActorEntry* entry = findEntry(snapshot->database, actor)) {
        draft = *entry;
    }
}

void ActorsPanel::saveDraft() {
    isDirty = false;
    if (!draft) {
        return;
    }
    databaseStore.edit(
        [entry = *draft](ActorDatabase& database) { return setEntry(database, entry); });
}

void ActorsPanel::deriveSelected() {
    isDirty = false;
    databaseStore.edit(
        [workspace = workspaceService.getSharedWorkspace(), builtIn = snapshot->builtIn,
         actor = *selected](ActorDatabase& database) -> std::expected<void, std::string> {
            if (workspace == nullptr) {
                return std::unexpected(std::string("no checkout is open to derive the actor from"));
            }
            return deriveEntries(database, *workspace, builtIn, {actor});
        });
}

void ActorsPanel::updateLists() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace != listedWorkspace) {
        listedWorkspace = workspace;
        areRowsStale = true;
        objects.clear();
        actors.clear();
        if (workspace != nullptr) {
            for (const hackeroot::ObjectEntry& object : workspace->getTables().objects) {
                if (!object.isUnset && !object.name.empty()) {
                    objects.push_back(object.name);
                }
            }
            for (const hackeroot::ActorEntry& actor : workspace->getTables().actors) {
                if (!actor.isUnset) {
                    actors.push_back(actor.enumName);
                }
            }
        }
    }
    if (areRowsStale && workspace != nullptr && snapshot) {
        rows = listActorRows(workspace->getTables(), snapshot->database);
        areRowsStale = false;
    }
}

void ActorsPanel::draw() {
    updateLists();
    if (isDirty && ImGui::GetTime() - changedAt > SaveDelay && !ImGui::IsAnyItemActive()) {
        saveDraft();
    }
    if (listedWorkspace == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Open a checkout to edit its actors.");
        return;
    }
    if (const std::optional<SeedingProgress> seeding = databaseStore.findSeeding()) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Making the checkout's actor database: deriving %zu of %zu actors…",
                           seeding->done, seeding->total);
        return;
    }
    if (!snapshot) {
        ImGui::TextColored(core::palette::TextMuted, "Reading the actor database…");
        return;
    }
    drawList();
    ImGui::SameLine();
    drawEditor();
}

void ActorsPanel::drawList() {
    if (!ImGui::BeginChild("##list", ImVec2(ImGui::GetFontSize() * ListWidth, 0.0F),
                           ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter by name or enum…", &filterText);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const auto current =
        std::ranges::find(FilterNames, filter, &std::pair<RowFilter, const char*>::first);
    if (ImGui::BeginCombo("##entries", current->second)) {
        for (const auto& [value, name] : FilterNames) {
            if (ImGui::Selectable(name, value == filter)) {
                filter = value;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginChild("##rows")) {
        for (const ActorRow& row : rows) {
            if (!isRowShown(row, filter, filterText)) {
                continue;
            }
            ImGui::TextColored(findRowColor(row), "%s", findRowIcon(row));
            ImGui::SameLine();
            const std::string label = std::format("{}##{}", row.name, row.enumName);
            if (ImGui::Selectable(label.c_str(), selected == row.enumName)) {
                if (isDirty) {
                    saveDraft();
                }
                select(row.enumName);
            }
            ImGui::SetItemTooltip("%s  ·  ID 0x%04X", row.enumName.c_str(),
                                  static_cast<unsigned>(row.id));
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

void ActorsPanel::drawEditorHeader() {
    ImGui::TextUnformatted(selected->c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Derive")) {
        if (draft) {
            ImGui::OpenPopup(DerivePrompt);
        } else {
            deriveSelected();
        }
    }
    ImGui::SetItemTooltip("Fill the entry from what Toki knows of the actor, or from its C");
    if (draft) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove the entry")) {
            isDirty = false;
            databaseStore.edit([actor = *selected](ActorDatabase& database) {
                return removeEntry(database, actor);
            });
        }
    } else {
        ImGui::TextColored(core::palette::TextMuted,
                           "It has no entry, so it is drawn as a marker.");
    }
    if (ImGui::BeginPopupModal(DerivePrompt, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Deriving replaces this entry with what Toki knows of the actor, or what its C says.");
        if (ImGui::Button("Derive")) {
            deriveSelected();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep the entry")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ActorsPanel::drawEditor() {
    if (!ImGui::BeginChild("##editor")) {
        ImGui::EndChild();
        return;
    }
    if (!selected) {
        ImGui::TextColored(core::palette::TextMuted, "Pick an actor to edit its entry.");
        ImGui::EndChild();
        return;
    }
    drawEditorHeader();
    if (draft) {
        for (const base::Diagnostic& problem : checkActorEntry(*draft)) {
            ImGui::TextColored(core::palette::Warning, "%s %s", core::icons::Warning,
                               problem.message.c_str());
        }
        ImGui::Separator();
        // Temporary: the entry is shown as its text until the editor is rebuilt
        // for format 2 (steps 4 to 6 of docs/actor-database.md).
        std::string text = formatEntry(*draft);
        ImGui::InputTextMultiline("##entry", &text, ImVec2(-FLT_MIN, -FLT_MIN),
                                  ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::EndChild();
}

} // namespace toki::actor

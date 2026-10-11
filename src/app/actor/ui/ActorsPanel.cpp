#include "ActorsPanel.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorRow.h"
#include "app/actor/domain/CollectionCatalog.h"
#include "app/actor/domain/DatabaseEdits.h"
#include "app/actor/domain/DatabaseStore.h"
#include "app/actor/domain/ProblemText.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/EntryEditor.h"
#include "app/actor/ui/VariantsEditor.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "base/Diagnostic.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <expected>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr double SaveDelay = 0.6;
constexpr float ListWidth = 18.0F;
constexpr const char* DerivePrompt = "Derive this actor?";
constexpr const char* ResetPrompt = "Reset this actor?";
constexpr const char* DeriveAllPrompt = "Derive every actor?";
constexpr const char* ResetAllPrompt = "Reset every actor?";
constexpr usize ProblemLines = 6;
constexpr float PreviewLines = 20.0F;

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

[[nodiscard]] bool drawConfirmation(const char* id, const std::function<std::string()>& describe,
                                    const char* confirm, const char* keep) {
    bool isConfirmed = false;
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(describe().c_str());
        if (ImGui::Button(confirm)) {
            isConfirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(keep)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return isConfirmed;
}

} // namespace

ActorsPanel::ActorsPanel(const workspace::WorkspaceService& workspaceService,
                         DatabaseStore& databaseStore, SymbolCatalog& symbolCatalog,
                         CollectionCatalog& collectionCatalog, const core::Fonts& fonts)
    : Panel("actors", "Actor Database", core::icons::Database, core::DockSlot::Right),
      workspaceService(workspaceService), databaseStore(databaseStore),
      symbolCatalog(symbolCatalog), collectionCatalog(collectionCatalog), fonts(fonts) {}

void ActorsPanel::setDatabase(DatabaseSnapshot read) {
    if (snapshot && snapshot->files != read.files) {
        if (isDirty) {
            saveDraft();
        }
        selected.reset();
        draft.reset();
    }
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
    if (!draft || !snapshot) {
        return;
    }
    databaseStore.editFile(snapshot->files.checkout, [entry = *draft](ActorDatabase& database) {
        return setEntry(database, entry);
    });
}

void ActorsPanel::deriveActors(std::vector<std::string> actorNames) {
    isDirty = false;
    databaseStore.editFile(
        snapshot->files.checkout,
        [workspace = workspaceService.getSharedWorkspace(), actorNames = std::move(actorNames)](
            ActorDatabase& database) -> std::expected<void, std::string> {
            if (workspace == nullptr) {
                return std::unexpected(std::string("no checkout is open to derive actors from"));
            }
            return deriveEntries(database, *workspace, actorNames);
        });
}

void ActorsPanel::resetActors(std::vector<std::string> actorNames) {
    isDirty = false;
    databaseStore.editFile(
        snapshot->files.checkout,
        [builtIn = snapshot->builtIn, actorNames = std::move(actorNames)](ActorDatabase& database) {
            return resetEntries(database, builtIn, actorNames);
        });
}

std::vector<std::string> ActorsPanel::listEntryActors(const bool isShippedOnly) const {
    std::vector<std::string> names;
    for (const ActorRow& row : rows) {
        if (row.hasEntry && (!isShippedOnly || findEntry(snapshot->builtIn, row.enumName))) {
            names.push_back(row.enumName);
        }
    }
    return names;
}

bool ActorsPanel::isShipped(const std::string& actor) const {
    return findEntry(snapshot->builtIn, actor) != nullptr;
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
    drawPanelHeader();
    drawList();
    ImGui::SameLine();
    drawEditor();
}

void ActorsPanel::drawPanelHeader() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(core::palette::TextMuted,
                       "How Toki shows the checkout's actors. Changes here never touch its code.");
    if (const std::optional<DatabaseFiles>& files = databaseStore.getFiles()) {
        ImGui::SetItemTooltip("%s", files->checkout.string().c_str());
    }
    const bool isBusy = databaseStore.isBusy();
    if (isBusy) {
        ImGui::SameLine();
        ImGui::TextColored(core::palette::TextMuted, "Saving…");
    }
    const float buttons = ImGui::CalcTextSize("Derive all").x + ImGui::CalcTextSize("Reset all").x +
                          (ImGui::GetStyle().FramePadding.x * 4.0F) +
                          ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - buttons, 0.0F));
    ImGui::BeginDisabled(isBusy);
    if (ImGui::Button("Derive all")) {
        ImGui::OpenPopup(DeriveAllPrompt);
    }
    ImGui::SetItemTooltip("Read every entry again from its actor's C");
    ImGui::SameLine();
    if (ImGui::Button("Reset all")) {
        ImGui::OpenPopup(ResetAllPrompt);
    }
    ImGui::SetItemTooltip("Put back Toki's own entry for every actor it has one for");
    ImGui::EndDisabled();
    if (drawConfirmation(
            DeriveAllPrompt,
            [this] {
                return std::format("Deriving replaces all {} entries with what each actor's C "
                                   "says. Your changes to them are lost.",
                                   listEntryActors(false).size());
            },
            "Derive all", "Keep the entries")) {
        deriveActors(listEntryActors(false));
    }
    if (drawConfirmation(
            ResetAllPrompt,
            [this] {
                return std::format("Resetting puts back Toki's own entry for {} actors. Your "
                                   "changes to them are lost.",
                                   listEntryActors(true).size());
            },
            "Reset all", "Keep the entries")) {
        resetActors(listEntryActors(true));
    }
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
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(selected->c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Derive")) {
        if (draft) {
            ImGui::OpenPopup(DerivePrompt);
        } else {
            deriveActors({*selected});
        }
    }
    ImGui::SetItemTooltip("Fill the entry from what the actor's C says");
    if (isShipped(*selected)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            if (draft) {
                ImGui::OpenPopup(ResetPrompt);
            } else {
                resetActors({*selected});
            }
        }
        ImGui::SetItemTooltip("Put back Toki's own entry for the actor");
    }
    if (draft) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove the entry")) {
            isDirty = false;
            databaseStore.editFile(snapshot->files.checkout,
                                   [actor = *selected](ActorDatabase& database) {
                                       return removeEntry(database, actor);
                                   });
        }
    } else {
        ImGui::SameLine();
        ImGui::TextColored(core::palette::TextMuted,
                           "It has no entry, so it is drawn as a marker.");
    }
    if (drawConfirmation(
            DerivePrompt,
            [] {
                return std::string("Deriving replaces this entry with what the actor's C says.");
            },
            "Derive", "Keep the entry")) {
        deriveActors({*selected});
    }
    if (drawConfirmation(
            ResetPrompt,
            [] { return std::string("Resetting replaces this entry with Toki's own."); }, "Reset",
            "Keep the entry")) {
        resetActors({*selected});
    }
}

void ActorsPanel::drawProblems() {
    std::vector<base::Diagnostic> problems = checkActorEntry(*draft);
    if (collectionCatalog.hasRead(draft->actor)) {
        std::ranges::move(checkCollections(*draft,
                                           [this](const std::string_view name) {
                                               return collectionCatalog.findCollection(draft->actor,
                                                                                       name);
                                           }),
                          std::back_inserter(problems));
    }
    if (problems.empty()) {
        return;
    }
    const float lines = static_cast<float>(std::min(problems.size(), ProblemLines));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0F, 0.0F), ImVec2(FLT_MAX, lines * ImGui::GetTextLineHeightWithSpacing() +
                                                ImGui::GetStyle().WindowPadding.y * 2.0F));
    if (ImGui::BeginChild("##problems", ImVec2(0.0F, 0.0F), ImGuiChildFlags_AutoResizeY)) {
        for (const base::Diagnostic& problem : problems) {
            const ProblemText text = makeProblemText(draft->actor, problem.message);
            ImGui::TextColored(core::palette::Warning, "%s", core::icons::Warning);
            ImGui::SameLine();
            if (!text.place.empty()) {
                ImGui::TextColored(core::palette::TextMuted, "%s", text.place.c_str());
                ImGui::SameLine();
            }
            ImGui::TextWrapped("%s", text.message.c_str());
        }
    }
    ImGui::EndChild();
}

void ActorsPanel::drawEntry() {
    const EditContext context{.symbolCatalog = symbolCatalog,
                              .objects = objects,
                              .actors = actors,
                              .findCollection = [this](const std::string_view name) {
                                  return collectionCatalog.findCollection(draft->actor, name);
                              }};
    bool isChanged = drawGeneralEditor(*draft, context);
    if (ImGui::CollapsingHeader("Params", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("params");
        const std::vector<std::string> collections =
            collectionCatalog.listCollectionNames(draft->actor);
        isChanged = paramsEditor.draw(*draft, collections) || isChanged;
        ImGui::PopID();
    }
    ImGui::SeparatorText("Variants, tried in order");
    ImGui::PushID("variants");
    isChanged = drawVariantsEditor(*draft, context) || isChanged;
    ImGui::PopID();
    if (isChanged) {
        isDirty = true;
        changedAt = ImGui::GetTime();
    }
    if (ImGui::CollapsingHeader("As written in actors.toml")) {
        std::string text = formatEntry(*draft);
        ImGui::PushFont(fonts.getMono(), core::Fonts::MonoSize);
        ImGui::InputTextMultiline("##entry", &text,
                                  ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * PreviewLines),
                                  ImGuiInputTextFlags_ReadOnly);
        ImGui::PopFont();
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
        drawProblems();
        ImGui::Separator();
        if (ImGui::BeginChild("##entry")) {
            drawEntry();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

} // namespace toki::actor

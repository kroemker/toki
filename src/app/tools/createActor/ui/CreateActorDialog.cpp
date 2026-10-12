#include "CreateActorDialog.h"

#include "app/actor/domain/CollectionCatalog.h"
#include "app/actor/domain/ReadableName.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/core/domain/TextSearch.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/tools/createActor/domain/ActorTemplate.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <expected>
#include <format>
#include <future>
#include <optional>
#include <string>
#include <string_view>

namespace toki::createActor {

namespace {

constexpr float ObjectListHeight = 12.0F;
constexpr float PreviewLines = 18.0F;
constexpr float TooltipWidth = 28.0F;
constexpr float LabelWidth = 12.0F;

[[nodiscard]] std::string makeLabel(const std::string_view name, const std::string_view prefix) {
    std::string lowered(name.substr(std::min(prefix.size(), name.size())));
    std::ranges::transform(lowered, lowered.begin(), [](const char letter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    });
    return actor::makeReadableName(lowered);
}

void drawComment(const std::string& comment) {
    if (comment.empty() || !ImGui::BeginItemTooltip()) {
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * TooltipWidth);
    ImGui::TextUnformatted(comment.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

} // namespace

CreateActorDialog::CreateActorDialog(const workspace::WorkspaceService& workspaceService,
                                     actor::CollectionCatalog& collectionCatalog,
                                     const core::Fonts& fonts)
    : Dialog("createActor", "Create Actor", core::icons::FilePlus, core::DialogMode::Modal, 52.0F,
             38.0F),
      workspaceService(workspaceService), collectionCatalog(collectionCatalog), fonts(fonts) {}

void CreateActorDialog::updateChoices() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace != listedWorkspace && !reading.valid()) {
        listedWorkspace = workspace;
        if (workspace != nullptr) {
            reading = std::async(std::launch::async,
                                 [root = workspace->getRoot()] { return readActorChoices(root); });
        }
    }
    if (reading.valid() && reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        choices = reading.get();
    }
}

void CreateActorDialog::draw() {
    updateChoices();
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Open a checkout to add an actor to it.");
        return;
    }
    if (!choices) {
        ImGui::TextColored(core::palette::TextMuted, "Reading the actor headers…");
        return;
    }
    if (!*choices) {
        ImGui::TextColored(core::palette::Danger, "%s %s", core::icons::Warning,
                           choices->error().c_str());
        return;
    }
    ImGui::BeginDisabled(editFlow.isReviewing() || editFlow.isBusy());
    drawForm(*workspace);
    ImGui::EndDisabled();
    const std::optional<std::string> problem = findDraftProblem(draft, workspace->getTables());
    if (problem) {
        ImGui::TextColored(core::palette::Warning, "%s %s", core::icons::Warning, problem->c_str());
    }
    ImGui::BeginDisabled(problem.has_value() || editFlow.isBusy() || editFlow.isReviewing());
    if (ImGui::Button("Review changes")) {
        editFlow.requestPlan(workspace->getRoot(), [root = workspace->getRoot(), wanted = draft,
                                                    tables = workspace->getTables()] {
            return makeActorPlan(root, wanted, tables);
        });
    }
    ImGui::EndDisabled();
    if (editFlow.draw(workspace->getRoot())) {
        draft = ActorDraft{};
    }
    drawPreview(*workspace);
}

void CreateActorDialog::drawForm(const hackeroot::Workspace& workspace) {
    if (core::beginProperties("##names", LabelWidth)) {
        core::drawPropertyLabel("Name", "The actor table's name for it. Its files, struct and enum "
                                        "are named after it.");
        ImGui::InputTextWithHint("##name", "En_Foo", &draft.name);
        core::drawPropertyLabel("Description",
                                "The line that describes the actor at the top of its files.");
        ImGui::InputText("##description", &draft.description);
        core::endProperties();
    }
    ImGui::SeparatorText("Overlay");
    drawOverlay();
    ImGui::SeparatorText("Actor");
    drawActor(workspace);
}

void CreateActorDialog::drawOverlay() {
    if (!core::beginProperties("##overlay", LabelWidth)) {
        return;
    }
    core::drawPropertyLabel("Allocation type",
                            "How the overlay is kept in memory, the actor table's allocType.");
    if (ImGui::BeginCombo("##allocation", makeLabel(draft.allocation, "ACTOROVL_ALLOC_").c_str())) {
        for (const hackeroot::DocumentedDefine& allocation : (*choices)->allocations) {
            if (ImGui::Selectable(makeLabel(allocation.name, "ACTOROVL_ALLOC_").c_str(),
                                  allocation.name == draft.allocation)) {
                draft.allocation = allocation.name;
            }
            drawComment(allocation.comment);
        }
        ImGui::EndCombo();
    }
    core::drawPropertyLabel("Reuse unused slot",
                            "Puts the actor, and a new object if there is one, into an unused "
                            "slot of the actor and object tables instead of appending to the end.");
    ImGui::Checkbox("##reuseSlot", &draft.takesUnsetSlot);
    core::endProperties();
}

void CreateActorDialog::drawActor(const hackeroot::Workspace& workspace) {
    if (!core::beginProperties("##actor", LabelWidth)) {
        return;
    }
    core::drawPropertyLabel("Category", "The ACTORCAT_ list the actor is updated and drawn in.");
    drawCategory();
    core::drawPropertyLabel("Flags");
    drawFlags();
    drawObject(workspace);
    core::drawPropertyLabel("Add Draw function",
                            "Writes a Draw function and puts it in the profile.");
    ImGui::Checkbox("##draws", &draft.draws);
    core::drawPropertyLabel("Add common includes",
                            "Includes the headers most actors use, not only play_state.h.");
    ImGui::Checkbox("##includesCommon", &draft.includesCommon);
    core::drawPropertyLabel("Enable DynaPoly",
                            "Makes it a DynaPolyActor, with collision of its own.");
    ImGui::Checkbox("##isDynaPoly", &draft.isDynaPoly);
    if (draft.isDynaPoly) {
        core::drawPropertyLabel("DynaPoly settings");
        ImGui::Checkbox("Move actors", &draft.movesCollision);
        ImGui::SetItemTooltip("DYNA_TRANSFORM_POS: actors standing on it move with it.");
        ImGui::SameLine();
        ImGui::Checkbox("Turn actors", &draft.turnsCollision);
        ImGui::SetItemTooltip("DYNA_TRANSFORM_ROT_Y: actors standing on it turn with it.");
    }
    core::endProperties();
}

void CreateActorDialog::drawCategory() {
    if (!ImGui::BeginCombo("##category", makeLabel(draft.category, "ACTORCAT_").c_str())) {
        return;
    }
    if (const actor::ValueCollection* categories =
            collectionCatalog.findSharedCollection("ActorCategory")) {
        for (const actor::ChoiceValue& category : categories->values) {
            if (ImGui::Selectable(makeLabel(category.label, "ACTORCAT_").c_str(),
                                  category.label == draft.category)) {
                draft.category = category.label;
            }
        }
    }
    ImGui::EndCombo();
}

void CreateActorDialog::drawFlags() {
    std::string preview;
    for (const std::string& flag : draft.flags) {
        preview += std::format("{}{}", preview.empty() ? "" : ", ", makeLabel(flag, "ACTOR_FLAG_"));
    }
    if (!ImGui::BeginCombo("##flags", preview.empty() ? "None" : preview.c_str(),
                           ImGuiComboFlags_HeightLargest)) {
        return;
    }
    for (const hackeroot::DocumentedDefine& flag : (*choices)->flags) {
        bool isOn = std::ranges::contains(draft.flags, flag.name);
        if (ImGui::Checkbox(makeLabel(flag.name, "ACTOR_FLAG_").c_str(), &isOn)) {
            if (isOn) {
                draft.flags.push_back(flag.name);
            } else {
                std::erase(draft.flags, flag.name);
            }
        }
        drawComment(flag.comment);
    }
    ImGui::EndCombo();
}

void CreateActorDialog::drawObject(const hackeroot::Workspace& workspace) {
    core::drawPropertyLabel("Create new object",
                            "Adds a new object to the object table for this actor.");
    bool isNew = draft.newObject.has_value();
    if (ImGui::Checkbox("##createObject", &isNew)) {
        draft.newObject = isNew ? std::optional<std::string>("object_") : std::nullopt;
    }
    core::drawPropertyLabel("Object", draft.newObject ? "The name of the new object."
                                                      : "The object loaded for the actor.");
    if (draft.newObject) {
        ImGui::InputText("##object", &*draft.newObject);
        return;
    }
    if (!ImGui::BeginCombo("##object", draft.object.c_str(), ImGuiComboFlags_HeightLargest)) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        objectFilter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter", &objectFilter);
    if (ImGui::BeginChild("##objects",
                          ImVec2(0.0F, ImGui::GetFrameHeightWithSpacing() * ObjectListHeight))) {
        for (const hackeroot::ObjectEntry& object : workspace.getTables().objects) {
            if (object.isUnset || object.name.empty() ||
                !core::containsIgnoringCase(object.name, objectFilter)) {
                continue;
            }
            if (ImGui::Selectable(object.name.c_str(), object.name == draft.object)) {
                draft.object = object.name;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndCombo();
}

void CreateActorDialog::drawPreview(const hackeroot::Workspace& workspace) {
    if (!ImGui::CollapsingHeader("Generated code") || draft.name.empty()) {
        return;
    }
    if (!ImGui::BeginTabBar("##files")) {
        return;
    }
    const auto drawFile = [this](const char* title, std::string text) {
        if (!ImGui::BeginTabItem(title)) {
            return;
        }
        ImGui::PushFont(fonts.getMono(), core::Fonts::MonoSize);
        ImGui::InputTextMultiline("##text", &text,
                                  ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * PreviewLines),
                                  ImGuiInputTextFlags_ReadOnly);
        ImGui::PopFont();
        ImGui::EndTabItem();
    };
    drawFile(".c", makeActorSource(draft, workspace.getTables()));
    drawFile(".h", makeActorHeader(draft));
    ImGui::EndTabBar();
}

} // namespace toki::createActor

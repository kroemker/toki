#include "CreateActorPanel.h"

#include "app/actor/domain/CollectionCatalog.h"
#include "app/actor/domain/ReadableName.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/core/domain/TextSearch.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
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
constexpr float FlagListHeight = 12.0F;
constexpr float PreviewLines = 18.0F;
constexpr float TooltipWidth = 28.0F;

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

CreateActorPanel::CreateActorPanel(const workspace::WorkspaceService& workspaceService,
                                   actor::CollectionCatalog& collectionCatalog,
                                   const core::Fonts& fonts)
    : Panel("createActor", "Create Actor", core::icons::FilePlus, core::DockSlot::Right),
      workspaceService(workspaceService), collectionCatalog(collectionCatalog), fonts(fonts) {}

void CreateActorPanel::updateChoices() {
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

void CreateActorPanel::draw() {
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
    if (ImGui::Button("Review")) {
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

void CreateActorPanel::drawForm(const hackeroot::Workspace& workspace) {
    ImGui::InputTextWithHint("Name", "En_Foo", &draft.name);
    ImGui::InputText("Description", &draft.description);
    ImGui::SeparatorText("Overlay");
    if (ImGui::BeginCombo("Loaded", makeLabel(draft.allocation, "ACTOROVL_ALLOC_").c_str())) {
        for (const hackeroot::DocumentedDefine& allocation : (*choices)->allocations) {
            if (ImGui::Selectable(makeLabel(allocation.name, "ACTOROVL_ALLOC_").c_str(),
                                  allocation.name == draft.allocation)) {
                draft.allocation = allocation.name;
            }
            drawComment(allocation.comment);
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Take an unused slot of the actor table", &draft.takesUnsetSlot);
    ImGui::SetItemTooltip("Keeps every actor ID after it; otherwise it goes at the end");

    ImGui::SeparatorText("Actor");
    if (ImGui::BeginCombo("Category", makeLabel(draft.category, "ACTORCAT_").c_str())) {
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
    drawFlags();
    drawObject(workspace);
    ImGui::Checkbox("Draws", &draft.draws);
    ImGui::SameLine();
    ImGui::Checkbox("Common includes", &draft.includesCommon);
    ImGui::SetItemTooltip("The headers most actors include, rather than only play_state.h");
    ImGui::Checkbox("Has collision of its own (DynaPolyActor)", &draft.isDynaPoly);
    if (draft.isDynaPoly) {
        ImGui::Indent();
        ImGui::Checkbox("Moves with it", &draft.movesCollision);
        ImGui::SameLine();
        ImGui::Checkbox("Turns with it", &draft.turnsCollision);
        ImGui::Unindent();
    }
}

void CreateActorPanel::drawFlags() {
    const std::string title = std::format("Flags ({})###flags", draft.flags.size());
    if (!ImGui::TreeNode(title.c_str())) {
        return;
    }
    if (ImGui::BeginChild("##flags",
                          ImVec2(0.0F, ImGui::GetFrameHeightWithSpacing() * FlagListHeight),
                          ImGuiChildFlags_Borders)) {
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
    }
    ImGui::EndChild();
    ImGui::TreePop();
}

void CreateActorPanel::drawObject(const hackeroot::Workspace& workspace) {
    bool isNew = draft.newObject.has_value();
    if (ImGui::Checkbox("A new object", &isNew)) {
        draft.newObject = isNew ? std::optional<std::string>("object_") : std::nullopt;
    }
    if (draft.newObject) {
        ImGui::InputText("Object", &*draft.newObject);
        return;
    }
    if (!ImGui::BeginCombo("Object", draft.object.c_str(), ImGuiComboFlags_HeightLargest)) {
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

void CreateActorPanel::drawPreview(const hackeroot::Workspace& workspace) {
    if (!ImGui::CollapsingHeader("The files it gets") || draft.name.empty()) {
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

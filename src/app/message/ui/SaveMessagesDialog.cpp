#include "SaveMessagesDialog.h"

#include "app/core/domain/Log.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageSave.h"
#include "hackeroot/EditPlan.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace toki::message {

namespace {

constexpr float Width = 44.0F;
constexpr float Height = 26.0F;

} // namespace

SaveMessagesDialog::SaveMessagesDialog(const workspace::WorkspaceService& workspaceService,
                                       MessageFocus& focus, std::function<void()> stopReviewing)
    : core::Dialog("saveMessages", "Save Messages", core::icons::FloppyDisk,
                   core::DialogMode::Modal, Width, Height),
      workspaceService(workspaceService), focus(focus), stopReviewing(std::move(stopReviewing)) {}

void SaveMessagesDialog::open(std::vector<usize> saving, const bool isReviewed,
                              core::AssetTab::Saved whenSaved) {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr || isSaving()) {
        if (whenSaved) {
            whenSaved(false);
        }
        return;
    }
    messages = std::move(saving);
    written = focus.draft.makeSavedTable(messages);
    saved = std::move(whenSaved);
    outcome.reset();
    isDoNotShowAgain = false;
    isShownOnce = isReviewed;
    if (isReviewed) {
        show();
    }
    editFlow.requestPlan(
        workspace->getRoot(),
        [shared = workspaceService.getSharedWorkspace(),
         table = *written]() -> std::expected<hackeroot::EditPlan, std::string> {
            if (shared == nullptr) {
                return std::unexpected(std::string("no checkout is open"));
            }
            return planMessageSave(*shared, table);
        },
        isReviewed, true);
}

void SaveMessagesDialog::finish(const bool isWritten) {
    if (isWritten && written) {
        focus.draft.markSaved(*written, messages);
        core::logOk("saved {}: {}",
                    messages.size() == 1 ? std::string("the message")
                                         : std::format("{} messages", messages.size()),
                    editFlow.getStatus());
    }
    written.reset();
    outcome.reset();
    const core::AssetTab::Saved taken = std::move(saved);
    saved = nullptr;
    if (taken) {
        taken(isWritten);
    }
}

void SaveMessagesDialog::update() {
    if (!isSaving()) {
        return;
    }
    if (outcome) {
        finish(*outcome);
        return;
    }
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr) {
        finish(false);
        return;
    }
    if (isShown()) {
        return;
    }
    if (isShownOnce) {
        finish(false);
        return;
    }
    if (editFlow.update(workspace->getRoot()) || editFlow.isUpToDate()) {
        finish(true);
    } else if (editFlow.hasFailed() || editFlow.isReviewing()) {
        isShownOnce = true;
        show();
    }
}

void SaveMessagesDialog::draw() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "No checkout is open.");
        return;
    }
    ImGui::TextWrapped("%s",
                       "Toki writes assets/text/message_data.h whole, every message in it, and "
                       "owns it from then on: the vanilla messages it included are written into "
                       "it, so that an edit of one replaces it, since the game finds only the "
                       "first message of an ID. Only the messages saved are written as edited; "
                       "every other one is written as it was read.");
    ImGui::Spacing();
    if (editFlow.isReviewing()) {
        if (ImGui::Checkbox("Do Not Show Again", &isDoNotShowAgain) && isDoNotShowAgain &&
            stopReviewing) {
            stopReviewing();
        }
        ImGui::SetItemTooltip("Saves are written at once from now on, unless they warn, and "
                              "what they warn of goes to the log. Settings > Messages > Review "
                              "Saves shows them again.");
    }
    const bool isWritten = editFlow.draw(workspace->getRoot());
    if (isSaving() && !outcome && (isWritten || editFlow.isUpToDate())) {
        outcome = true;
    }
}

} // namespace toki::message

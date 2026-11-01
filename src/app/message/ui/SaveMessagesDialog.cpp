#include "SaveMessagesDialog.h"

#include "app/core/domain/Log.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageSave.h"
#include "hackeroot/EditPlan.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <expected>
#include <string>

namespace toki::message {

namespace {

constexpr float Width = 44.0F;
constexpr float Height = 26.0F;

} // namespace

SaveMessagesDialog::SaveMessagesDialog(const workspace::WorkspaceService& workspaceService,
                                       MessageFocus& focus)
    : core::Dialog("saveMessages", "Save Messages", core::icons::FloppyDisk,
                   core::DialogMode::Modal, Width, Height),
      workspaceService(workspaceService), focus(focus) {}

void SaveMessagesDialog::open() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr) {
        return;
    }
    plannedRevision = focus.draft.getRevision();
    show();
    editFlow.requestPlan(
        workspace->getRoot(),
        [shared = workspaceService.getSharedWorkspace(),
         table = focus.draft.getTable()]() -> std::expected<hackeroot::EditPlan, std::string> {
            if (shared == nullptr) {
                return std::unexpected(std::string("no checkout is open"));
            }
            return planMessageSave(*shared, table);
        });
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
                       "first message of an ID.");
    ImGui::Spacing();
    if (editFlow.draw(workspace->getRoot())) {
        if (focus.draft.getRevision() == plannedRevision) {
            focus.draft.markSaved();
        }
        core::logOk("saved the messages: {}", editFlow.getStatus());
    }
}

} // namespace toki::message

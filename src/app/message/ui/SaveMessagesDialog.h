#pragma once

#include "app/core/ui/Dialog.h"
#include "app/message/ui/MessageFocus.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"

namespace toki::message {

/// Saves the messages as edited over `assets/text/message_data.h`, which Toki
/// owns from then on: the plan is made as soon as the dialog opens, and
/// written once it has been reviewed.
class SaveMessagesDialog : public core::Dialog {
public:
    SaveMessagesDialog(const workspace::WorkspaceService& workspaceService, MessageFocus& focus);

    /// Plans saving the draft as it is now, and shows the plan.
    void open();

    void draw() override;

private:
    const workspace::WorkspaceService& workspaceService;
    MessageFocus& focus;
    u64 plannedRevision = 0;
    tools::EditFlow editFlow;
};

} // namespace toki::message

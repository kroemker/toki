#pragma once

#include "app/core/ui/AssetTab.h"
#include "app/core/ui/Dialog.h"
#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/ui/MessageFocus.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <functional>
#include <optional>
#include <vector>

namespace toki::message {

/// Saves the edits of some messages over `assets/text/message_data.h`, which
/// Toki owns from then on, every other message written as it was read. A save
/// is shown for review unless the person asked not to be, and one that warns
/// is shown either way, as the first is.
class SaveMessagesDialog : public core::Dialog {
public:
    /// `stopReviewing` is called when the person asks not to be shown a save
    /// again.
    SaveMessagesDialog(const workspace::WorkspaceService& workspaceService, MessageFocus& focus,
                       std::function<void()> stopReviewing);

    /// Plans saving the edits of `messages` as they are now. `saved` is
    /// called once the save is written, fails, or is cancelled.
    void open(std::vector<usize> messages, bool isReviewed, core::AssetTab::Saved saved);

    /// Takes how a save goes, and says how it went; called every frame.
    void update();

    /// Whether a save is being planned, reviewed or written.
    [[nodiscard]] bool isSaving() const {
        return saved != nullptr;
    }

    void draw() override;

private:
    void finish(bool isWritten);

    const workspace::WorkspaceService& workspaceService;
    MessageFocus& focus;
    std::function<void()> stopReviewing;
    std::vector<usize> messages;
    std::optional<MessageTable> written;
    core::AssetTab::Saved saved;
    std::optional<bool> outcome;
    bool isDoNotShowAgain = false;
    /// Whether the save was shown, so that the dialog closed means the save
    /// was given up.
    bool isShownOnce = false;
    tools::EditFlow editFlow;
};

} // namespace toki::message

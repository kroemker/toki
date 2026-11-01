#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageDraft.h"
#include "app/message/domain/MessageLoader.h"

#include <optional>

namespace toki::message {

/// What the Assets panel's Messages tab and the Message panel share: the
/// catalog last read, the messages as edited, the language they are shown in,
/// and the message shown, by its index in the draft's table.
struct MessageFocus {
    SharedCatalog catalog;
    MessageDraft draft;
    Language language = Language::English;
    std::optional<usize> shown;

    /// The message shown, if it is still in the draft.
    [[nodiscard]] const Message* findShown() const {
        const MessageTable& table = draft.getTable();
        if (!catalog || !shown || *shown >= table.messages.size()) {
            return nullptr;
        }
        return &table.messages[*shown];
    }
};

} // namespace toki::message

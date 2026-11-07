#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageDraft.h"
#include "app/message/domain/MessageLoader.h"

#include <optional>

namespace toki::message {

/// What the Assets panel's Messages tab and the Message panel's tabs share:
/// the catalog last read, the messages as edited, the language they are shown
/// in, the message shown, by its index in the draft's table, and how large
/// the boxes are drawn.
struct MessageFocus {
    SharedCatalog catalog;
    MessageDraft draft;
    Language language = Language::English;
    std::optional<usize> shown;
    /// Window pixels a game pixel; none to draw the boxes as large as the
    /// panel holds.
    f32 zoom = 0.0F;

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

#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageLoader.h"

#include <optional>

namespace toki::message {

/// What the Assets panel's Messages tab and the Message panel share: the
/// catalog last read, the language messages are shown in, and the message
/// shown, by its index in the catalog's table.
struct MessageFocus {
    SharedCatalog catalog;
    Language language = Language::English;
    std::optional<usize> shown;

    /// The message shown, if it is still in the catalog.
    [[nodiscard]] const Message* findShown() const {
        if (!catalog || !shown || *shown >= catalog->table.messages.size()) {
            return nullptr;
        }
        return &catalog->table.messages[*shown];
    }
};

} // namespace toki::message

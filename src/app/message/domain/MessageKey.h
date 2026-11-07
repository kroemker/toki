#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"

#include <optional>
#include <string>

namespace toki::message {

/// A message as it is found again in a table read anew: by its ID, its
/// macro, and how many rows of both stand before it. An index would name
/// another message once a row is added or taken out before it.
struct MessageKey {
    u16 id = 0;
    MessageMacro macro = MessageMacro::Message;
    usize occurrence = 0;

    friend bool operator==(const MessageKey&, const MessageKey&) = default;
};

[[nodiscard]] MessageKey makeMessageKey(const MessageTable& table, usize index);

[[nodiscard]] std::optional<usize> findMessage(const MessageTable& table, const MessageKey& key);

/// The key as text, for what names messages by strings.
[[nodiscard]] std::string formatMessageKey(const MessageKey& key);

} // namespace toki::message

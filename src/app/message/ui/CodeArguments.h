#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"

#include <string>

namespace toki::message {

struct MessageCatalog;
struct MessageTable;

/// A form of the arguments of `code`, one row each: a number as a field, a
/// message ID as a field with the start of that message's text beside it, and
/// an item, a high score or a sound as a dropdown of what the checkout
/// declares, filtered by `filter`. True when an argument changed.
[[nodiscard]] bool drawCodeArguments(CodePiece& code, const MessageCatalog& catalog,
                                     const MessageTable& table, Language language,
                                     std::string& filter);

} // namespace toki::message

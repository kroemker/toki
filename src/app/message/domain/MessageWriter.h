#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"

#include <functional>
#include <optional>
#include <string>

namespace toki::message {

/// One language's text as `MSG(...)`, the way the extracted files write it: a
/// line of the C for each line of the box, strings and codes in between, a
/// missing text as `MSG(/* MISSING */)` with the word it had, or `MSG()`, and
/// an empty one as the empty string.
[[nodiscard]] std::string writeMessageText(const MessageText& text);

/// One row, `DEFINE_MESSAGE(0x0001, TEXTBOX_TYPE_BLUE, TEXTBOX_POS_BOTTOM,`
/// and its four texts, in the macro it was read from.
[[nodiscard]] std::string writeMessageRow(const Message& message);

/// The feature switch a row stands under, if any.
using FindSwitch = std::function<std::optional<std::string>(const Message&)>;

/// `assets/text/message_data.h` as Toki owns it: marked as Toki's, every row
/// of `table` in its order, those under a feature switch in an `#if` of it,
/// and nothing included from `extracted/`.
[[nodiscard]] std::string writeMessageFile(const MessageTable& table, const FindSwitch& findSwitch);

} // namespace toki::message

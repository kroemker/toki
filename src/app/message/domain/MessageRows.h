#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::message {

enum class MessageColumn : u8 {
    Id,
    Text,
    Type,
    Position,
    Boxes,
    Source,
};

/// One message as the Messages tab lists it, in one language.
struct MessageRow {
    /// The message's index in its table.
    usize index = 0;
    u16 id = 0;
    /// Its text in a line: the strings, the player's name as `Link`, every
    /// line break and box break a space, control codes left out.
    std::string text;
    std::string boxType;
    std::string boxPosition;
    usize boxCount = 0;
    bool isVanilla = false;
};

/// Which messages the Messages tab lists. `text` matches the ID as written
/// (`0x0001`, `1`) or any part of the text, ignoring case.
struct MessageFilter {
    std::string_view text;
    bool showsVanilla = true;
    bool showsOwn = true;
    /// Only messages with this box type (`TEXTBOX_TYPE_BLUE`), if set.
    std::optional<std::string> boxType;
};

/// The message's text in `language` in a line, as `MessageRow::text` is.
[[nodiscard]] std::string makeLineText(const MessageText& text);

/// How many boxes the text shows: one, and one more for each `BOX_BREAK`.
[[nodiscard]] usize countBoxes(const MessageText& text);

/// The rows of every message of `table` the build puts into `language`'s
/// table that `filter` lets through, in table order.
[[nodiscard]] std::vector<MessageRow> makeMessageRows(const MessageTable& table, Language language,
                                                      bool isFontMessageEnglish,
                                                      const MessageFilter& filter);

void sortMessageRows(std::vector<MessageRow>& rows, MessageColumn column, bool ascending);

/// Every box type the table's messages use, as written, sorted.
[[nodiscard]] std::vector<std::string> listBoxTypes(const MessageTable& table);

} // namespace toki::message

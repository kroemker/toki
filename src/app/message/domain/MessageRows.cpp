#include "MessageRows.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

constexpr std::string_view BoxBreakCodes[] = {"BOX_BREAK", "BOX_BREAK_DELAYED"};

[[nodiscard]] bool isBoxBreak(const CodePiece& code) {
    return std::ranges::contains(BoxBreakCodes, std::string_view(code.name));
}

[[nodiscard]] std::string makeLower(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return lower;
}

[[nodiscard]] bool matchesId(const u16 id, const std::string_view text) {
    const std::string lower = makeLower(text);
    return lower == std::format("0x{:04x}", id) || lower == std::format("{:04x}", id) ||
           lower == std::format("{:x}", id) || lower == std::format("0x{:x}", id);
}

void appendSpace(std::string& line) {
    if (!line.empty() && line.back() != ' ') {
        line += ' ';
    }
}

} // namespace

std::string makeLineText(const MessageText& text) {
    std::string line;
    for (const Piece& piece : text.pieces) {
        if (const auto* run = std::get_if<TextPiece>(&piece)) {
            line += run->text;
        } else if (std::holds_alternative<NewlinePiece>(piece)) {
            appendSpace(line);
        } else {
            const CodePiece& code = std::get<CodePiece>(piece);
            if (code.name == "NAME") {
                line += "Link";
            } else if (isBoxBreak(code)) {
                appendSpace(line);
            }
        }
    }
    while (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
    return line;
}

usize countBoxes(const MessageText& text) {
    return 1 + static_cast<usize>(std::ranges::count_if(text.pieces, [](const Piece& piece) {
               const auto* code = std::get_if<CodePiece>(&piece);
               return code != nullptr && isBoxBreak(*code);
           }));
}

std::vector<MessageRow> makeMessageRows(const MessageTable& table, const Language language,
                                        const bool isFontMessageEnglish,
                                        const MessageFilter& filter) {
    const std::string needle = makeLower(filter.text);
    std::vector<MessageRow> rows;
    for (usize index = 0; index < table.messages.size(); ++index) {
        const Message& message = table.messages[index];
        if (!isInLanguage(message, language, isFontMessageEnglish) ||
            (message.isVanilla ? !filter.showsVanilla : !filter.showsOwn) ||
            (filter.boxType && message.boxType != *filter.boxType)) {
            continue;
        }
        const MessageText& text = message.texts[static_cast<usize>(language)];
        MessageRow row{.index = index,
                       .id = message.id,
                       .text = makeLineText(text),
                       .boxType = message.boxType,
                       .boxPosition = message.boxPosition,
                       .boxCount = countBoxes(text),
                       .isVanilla = message.isVanilla};
        if (!needle.empty() && !matchesId(message.id, needle) &&
            !makeLower(row.text).contains(needle)) {
            continue;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void sortMessageRows(std::vector<MessageRow>& rows, const MessageColumn column,
                     const bool ascending) {
    const auto makeKey = [column](const MessageRow& row) {
        switch (column) {
        case MessageColumn::Text:
            return std::tuple(makeLower(row.text), usize{0}, row.index);
        case MessageColumn::Type:
            return std::tuple(row.boxType, usize{0}, row.index);
        case MessageColumn::Position:
            return std::tuple(row.boxPosition, usize{0}, row.index);
        case MessageColumn::Boxes:
            return std::tuple(std::string(), row.boxCount, row.index);
        case MessageColumn::Source:
            return std::tuple(std::string(row.isVanilla ? "vanilla" : "own"), usize{0}, row.index);
        case MessageColumn::Id:
            break;
        }
        return std::tuple(std::string(), usize{row.id}, row.index);
    };
    std::ranges::stable_sort(rows, [&](const MessageRow& left, const MessageRow& right) {
        return ascending ? makeKey(left) < makeKey(right) : makeKey(right) < makeKey(left);
    });
}

namespace {

[[nodiscard]] std::vector<std::string> listUsed(const MessageTable& table,
                                                std::string Message::*member) {
    std::vector<std::string> used;
    for (const Message& message : table.messages) {
        if (!std::ranges::contains(used, message.*member)) {
            used.push_back(message.*member);
        }
    }
    std::ranges::sort(used);
    return used;
}

} // namespace

std::vector<std::string> listBoxTypes(const MessageTable& table) {
    return listUsed(table, &Message::boxType);
}

std::vector<std::string> listBoxPositions(const MessageTable& table) {
    return listUsed(table, &Message::boxPosition);
}

} // namespace toki::message

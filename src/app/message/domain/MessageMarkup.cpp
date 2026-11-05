#include "MessageMarkup.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

struct CodeName {
    std::string_view macro;
    std::string_view words;
    /// The prefix the code's one argument drops when it is shown in words.
    std::string_view argumentPrefix;
    bool hasArgumentInWords = false;
};

// The codes of `include/message_data_fmt.h` that take a place in the text,
// in words. `END` is never written; `\n` is a new line.
constexpr CodeName CodeNames[] = {
    {.macro = "BOX_BREAK", .words = "Box Break"},
    {.macro = "COLOR", .words = "Color"},
    {.macro = "SHIFT", .words = "Shift"},
    {.macro = "TEXTID", .words = "Text ID"},
    {.macro = "QUICKTEXT_ENABLE", .words = "Quick Text On"},
    {.macro = "QUICKTEXT_DISABLE", .words = "Quick Text Off"},
    {.macro = "PERSISTENT", .words = "Persistent"},
    {.macro = "EVENT", .words = "Event"},
    {.macro = "BOX_BREAK_DELAYED", .words = "Box Break Delayed"},
    {.macro = "AWAIT_BUTTON_PRESS", .words = "Await Button Press"},
    {.macro = "FADE", .words = "Fade"},
    {.macro = "NAME", .words = "Name"},
    {.macro = "OCARINA", .words = "Ocarina"},
    {.macro = "FADE2", .words = "Fade2"},
    {.macro = "SFX", .words = "Sfx"},
    {.macro = "ITEM_ICON", .words = "Icon", .argumentPrefix = "ITEM_", .hasArgumentInWords = true},
    {.macro = "TEXT_SPEED", .words = "Text Speed"},
    {.macro = "BACKGROUND", .words = "Background"},
    {.macro = "MARATHON_TIME", .words = "Marathon Time"},
    {.macro = "RACE_TIME", .words = "Race Time"},
    {.macro = "POINTS", .words = "Points"},
    {.macro = "TOKENS", .words = "Tokens"},
    {.macro = "UNSKIPPABLE", .words = "Unskippable"},
    {.macro = "TWO_CHOICE", .words = "Two Choice"},
    {.macro = "THREE_CHOICE", .words = "Three Choice"},
    {.macro = "FISH_INFO", .words = "Fish Info"},
    {.macro = "HIGHSCORE",
     .words = "Highscore",
     .argumentPrefix = "HS_",
     .hasArgumentInWords = true},
    {.macro = "TIME", .words = "Time"},
};

[[nodiscard]] bool isBoxBreak(const CodePiece& code) {
    return code.name == "BOX_BREAK" || code.name == "BOX_BREAK_DELAYED";
}

[[nodiscard]] bool isConstant(const std::string_view text) {
    return !text.empty() && std::isupper(static_cast<unsigned char>(text.front())) != 0 &&
           std::ranges::all_of(text, [](const char character) {
               return std::isupper(static_cast<unsigned char>(character)) != 0 ||
                      std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '_';
           });
}

[[nodiscard]] bool isInWords(const std::string_view text) {
    return std::ranges::any_of(text, [](const char character) {
        return std::islower(static_cast<unsigned char>(character)) != 0 || character == ' ';
    });
}

[[nodiscard]] std::string makeWords(const std::string_view constant) {
    std::string words;
    bool isWordStart = true;
    for (const char character : constant) {
        if (character == '_') {
            words += ' ';
            isWordStart = true;
            continue;
        }
        words += isWordStart
                     ? character
                     : static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        isWordStart = false;
    }
    return words;
}

[[nodiscard]] std::string makeConstant(const std::string_view words) {
    std::string constant;
    for (const char character : words) {
        constant += character == ' '
                        ? '_'
                        : static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return constant;
}

[[nodiscard]] const CodeName* findCodeByMacro(const std::string_view macro) {
    const auto* found = std::ranges::find(CodeNames, macro, &CodeName::macro);
    return found != std::ranges::end(CodeNames) ? found : nullptr;
}

[[nodiscard]] std::string formatCode(const CodePiece& code) {
    if (code.name == "COLOR" && code.arguments.size() == 1 && isConstant(code.arguments[0])) {
        return std::format("{{{}}}", makeWords(code.arguments[0]));
    }
    const CodeName* name = findCodeByMacro(code.name);
    std::string tag = name != nullptr ? std::string(name->words) : code.name;
    for (usize index = 0; index < code.arguments.size(); ++index) {
        std::string_view argument = code.arguments[index];
        std::string shown(argument);
        if (name != nullptr && name->hasArgumentInWords && code.arguments.size() == 1 &&
            argument.starts_with(name->argumentPrefix) && isConstant(argument)) {
            argument.remove_prefix(name->argumentPrefix.size());
            shown = makeWords(argument);
        }
        tag += (index == 0 ? " " : ", ") + shown;
    }
    return std::format("{{{}}}", tag);
}

void appendText(std::string& markup, const std::string_view text) {
    for (const char character : text) {
        markup += character;
        if (character == '{' || character == '}') {
            markup += character;
        }
    }
}

[[nodiscard]] std::vector<std::string> splitArguments(const std::string_view text) {
    std::vector<std::string> arguments;
    usize start = 0;
    while (start <= text.size()) {
        const usize comma = text.find(',', start);
        std::string_view argument =
            text.substr(start, comma == std::string_view::npos ? text.npos : comma - start);
        while (!argument.empty() && argument.front() == ' ') {
            argument.remove_prefix(1);
        }
        while (!argument.empty() && argument.back() == ' ') {
            argument.remove_suffix(1);
        }
        arguments.emplace_back(argument);
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return arguments;
}

[[nodiscard]] CodePiece parseTag(const std::string_view tag) {
    const CodeName* best = nullptr;
    for (const CodeName& name : CodeNames) {
        const bool matches =
            tag == name.words || (tag.starts_with(name.words) && tag.size() > name.words.size() &&
                                  tag[name.words.size()] == ' ');
        if (matches && (best == nullptr || name.words.size() > best->words.size())) {
            best = &name;
        }
    }
    if (best == nullptr) {
        const usize space = tag.find(' ');
        const std::string_view head = tag.substr(0, space);
        if (isConstant(head) && space != std::string_view::npos) {
            return CodePiece{.name = std::string(head),
                             .arguments = splitArguments(tag.substr(space + 1))};
        }
        if (isConstant(tag) && tag.find(' ') == std::string_view::npos && tag.contains('_')) {
            return CodePiece{.name = std::string(tag)};
        }
        return CodePiece{.name = "COLOR", .arguments = {makeConstant(tag)}};
    }
    CodePiece code{.name = std::string(best->macro)};
    if (tag.size() == best->words.size()) {
        return code;
    }
    code.arguments = splitArguments(tag.substr(best->words.size() + 1));
    if (best->hasArgumentInWords && code.arguments.size() == 1 && isInWords(code.arguments[0])) {
        code.arguments[0] = std::string(best->argumentPrefix) + makeConstant(code.arguments[0]);
    }
    return code;
}

void appendPiece(std::vector<Piece>& pieces, const std::string_view text) {
    if (text.empty()) {
        return;
    }
    if (!pieces.empty() && std::holds_alternative<TextPiece>(pieces.back())) {
        std::get<TextPiece>(pieces.back()).text += text;
        return;
    }
    pieces.emplace_back(TextPiece{.text = std::string(text)});
}

} // namespace

std::vector<BoxText> splitBoxes(const MessageText& text) {
    std::vector<BoxText> boxes(1);
    for (const Piece& piece : text.pieces) {
        const auto* code = std::get_if<CodePiece>(&piece);
        if (code != nullptr && isBoxBreak(*code)) {
            boxes.back().boxBreak = *code;
            boxes.emplace_back();
            continue;
        }
        boxes.back().pieces.push_back(piece);
    }
    return boxes;
}

std::vector<TextPosition> localizeSources(const MessageText& text,
                                          const std::span<const TextPosition> sources) {
    std::vector<usize> local;
    local.reserve(text.pieces.size() + 1);
    usize inBox = 0;
    for (const Piece& piece : text.pieces) {
        const auto* code = std::get_if<CodePiece>(&piece);
        if (code != nullptr && isBoxBreak(*code)) {
            local.push_back(inBox);
            inBox = 0;
            continue;
        }
        local.push_back(inBox++);
    }
    local.push_back(inBox);
    std::vector<TextPosition> localized;
    localized.reserve(sources.size());
    for (const TextPosition& source : sources) {
        const usize piece = std::min(source.piece, local.size() - 1);
        localized.push_back(TextPosition{.piece = local[piece], .offset = source.offset});
    }
    return localized;
}

std::vector<Piece> joinBoxes(const std::span<const BoxText> boxes) {
    std::vector<Piece> pieces;
    for (const BoxText& box : boxes) {
        pieces.insert(pieces.end(), box.pieces.begin(), box.pieces.end());
        if (box.boxBreak) {
            pieces.emplace_back(*box.boxBreak);
        }
    }
    return pieces;
}

std::string formatMarkup(const std::span<const Piece> pieces) {
    std::string markup;
    for (const Piece& piece : pieces) {
        if (const auto* text = std::get_if<TextPiece>(&piece)) {
            appendText(markup, text->text);
        } else if (std::holds_alternative<NewlinePiece>(piece)) {
            markup += '\n';
        } else {
            markup += formatCode(std::get<CodePiece>(piece));
        }
    }
    return markup;
}

std::expected<std::vector<Piece>, MarkupError> parseMarkup(const std::string_view markup) {
    std::vector<Piece> pieces;
    std::string text;
    usize index = 0;
    while (index < markup.size()) {
        const char character = markup[index];
        if ((character == '{' || character == '}') && index + 1 < markup.size() &&
            markup[index + 1] == character) {
            text += character;
            index += 2;
            continue;
        }
        if (character == '}') {
            return std::unexpected(MarkupError{
                .offset = index, .message = "a '}' closes no code; write '}}' for one"});
        }
        if (character == '\n') {
            appendPiece(pieces, std::exchange(text, {}));
            pieces.emplace_back(NewlinePiece{});
            ++index;
            continue;
        }
        if (character != '{') {
            text += character;
            ++index;
            continue;
        }
        const usize close = markup.find('}', index);
        const usize newline = markup.find('\n', index);
        if (close == std::string_view::npos || newline < close) {
            return std::unexpected(MarkupError{
                .offset = index, .message = "a '{' is never closed; write '{{' for one"});
        }
        const std::string_view tag = markup.substr(index + 1, close - index - 1);
        if (tag.empty() || tag.front() == ' ' || tag.back() == ' ') {
            return std::unexpected(
                MarkupError{.offset = index, .message = "a code names nothing between its braces"});
        }
        appendPiece(pieces, std::exchange(text, {}));
        pieces.emplace_back(parseTag(tag));
        index = close + 1;
    }
    appendPiece(pieces, text);
    return pieces;
}

} // namespace toki::message

#include "MessageReader.h"

#include "base/Diagnostic.h"
#include "cparser/Initializer.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/StringLiteral.h"
#include "cparser/TableReader.h"
#include "cparser/Token.h"
#include "hackeroot/BuildDefines.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"

#include <array>
#include <charconv>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace toki::message {

namespace {

constexpr std::array EntryMacros = {
    std::string_view("DEFINE_MESSAGE"), std::string_view("DEFINE_MESSAGE_JPN"),
    std::string_view("DEFINE_MESSAGE_NES"), std::string_view("DEFINE_MESSAGE_FFFC")};

[[nodiscard]] MessageMacro findMacro(const std::string_view spelling) {
    if (spelling == "DEFINE_MESSAGE_JPN") {
        return MessageMacro::JapaneseOnly;
    }
    if (spelling == "DEFINE_MESSAGE_NES") {
        return MessageMacro::NesOnly;
    }
    if (spelling == "DEFINE_MESSAGE_FFFC") {
        return MessageMacro::FontMessage;
    }
    return MessageMacro::Message;
}

constexpr usize FirstTextArgument = 3;
constexpr u16 FontMessageId = 0xFFFC;
constexpr u16 LastMessageId = 0xFFFD;

[[nodiscard]] std::string makeIdText(const u16 id) {
    return std::format("0x{:04X}", id);
}

[[nodiscard]] std::optional<u16> parseId(const std::span<const cparser::Token> argument) {
    if (argument.size() != 1 || argument.front().kind != cparser::TokenKind::Integer) {
        return std::nullopt;
    }
    std::string_view text = argument.front().text;
    while (!text.empty() &&
           (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) {
        text.remove_suffix(1);
    }
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc() || end != text.data() + text.size() || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<u16>(value);
}

[[nodiscard]] std::string joinTokens(const std::span<const cparser::Token> tokens) {
    std::string joined;
    for (const cparser::Token& token : tokens) {
        if (!joined.empty() && token.spaceBefore) {
            joined += ' ';
        }
        joined += token.text;
    }
    return joined;
}

[[nodiscard]] std::string findPlaceholder(const std::string_view spelling) {
    const usize open = spelling.find("/*");
    const usize close = spelling.find("*/", open == std::string_view::npos ? 0 : open);
    if (open == std::string_view::npos || close == std::string_view::npos) {
        return {};
    }
    std::string_view word = spelling.substr(open + 2, close - open - 2);
    while (!word.empty() && word.front() == ' ') {
        word.remove_prefix(1);
    }
    while (!word.empty() && word.back() == ' ') {
        word.remove_suffix(1);
    }
    return std::string(word);
}

[[nodiscard]] usize findClose(const std::span<const cparser::Token> tokens, const usize open) {
    int nesting = 0;
    for (usize cursor = open; cursor < tokens.size(); ++cursor) {
        if (cparser::isPunctuator(tokens[cursor], "(")) {
            ++nesting;
        } else if (cparser::isPunctuator(tokens[cursor], ")") && --nesting == 0) {
            return cursor;
        }
    }
    return tokens.size();
}

void appendText(MessageText& text, const std::string_view decoded) {
    usize start = 0;
    while (start <= decoded.size()) {
        const usize newline = decoded.find('\n', start);
        const std::string_view run = decoded.substr(
            start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        if (!run.empty()) {
            if (!text.pieces.empty() && std::holds_alternative<TextPiece>(text.pieces.back())) {
                std::get<TextPiece>(text.pieces.back()).text += run;
            } else {
                text.pieces.emplace_back(TextPiece{.text = std::string(run)});
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        text.pieces.emplace_back(NewlinePiece{});
        start = newline + 1;
    }
}

[[nodiscard]] std::expected<MessageText, base::Diagnostic>
readText(const std::span<const cparser::Token> argument, const std::string_view spelling) {
    if (argument.size() < 3 || !cparser::isIdentifier(argument.front(), "MSG") ||
        !cparser::isPunctuator(argument[1], "(") || !cparser::isPunctuator(argument.back(), ")")) {
        return std::unexpected(base::makeWarning(
            argument.empty() ? base::SourceLocation{} : cparser::locate(argument.front()),
            std::format("a language's text is `{}`, not `MSG(...)`", spelling)));
    }
    const std::span<const cparser::Token> body = argument.subspan(2, argument.size() - 3);
    MessageText text;
    if (body.empty()) {
        text.placeholder = findPlaceholder(spelling);
        return text;
    }
    text.isPresent = true;
    for (usize cursor = 0; cursor < body.size(); ++cursor) {
        const cparser::Token& token = body[cursor];
        if (token.kind == cparser::TokenKind::String) {
            appendText(text, cparser::decodeEscapes(token.text.substr(1, token.text.size() - 2)));
            continue;
        }
        if (token.kind != cparser::TokenKind::Identifier) {
            return std::unexpected(base::makeWarning(
                cparser::locate(token),
                std::format("`{}` stands in a message's text, where only strings and control "
                            "codes may",
                            token.text)));
        }
        CodePiece code{.name = std::string(token.text), .location = cparser::locate(token)};
        if (cursor + 1 < body.size() && cparser::isPunctuator(body[cursor + 1], "(")) {
            const usize close = findClose(body, cursor + 1);
            if (close == body.size()) {
                return std::unexpected(base::makeWarning(
                    code.location, std::format("`{}(` is never closed", code.name)));
            }
            for (const std::span<const cparser::Token> element :
                 cparser::splitElements(body.subspan(cursor + 2, close - cursor - 2))) {
                code.arguments.push_back(joinTokens(element));
            }
            cursor = close;
        }
        text.pieces.emplace_back(std::move(code));
    }
    return text;
}

enum class LanguageGroup : u8 {
    Japanese,
    Nes,
};

[[nodiscard]] bool isInGroup(const MessageMacro macro, const LanguageGroup group) {
    switch (macro) {
    case MessageMacro::JapaneseOnly:
        return group == LanguageGroup::Japanese;
    case MessageMacro::NesOnly:
        return group == LanguageGroup::Nes;
    case MessageMacro::Message:
    case MessageMacro::FontMessage:
        return true;
    }
    return true;
}

class RowReader {
public:
    RowReader(const MessageFiles& files, MessageTable& table) : files(files), table(table) {}

    void read(const cparser::TableRow& row);

    void checkOrder();

private:
    [[nodiscard]] bool isVanilla(const base::SourceLocation& location);

    const MessageFiles& files;
    MessageTable& table;
    std::map<std::pair<LanguageGroup, u16>, base::SourceLocation> firstDefinitions;
    std::unordered_map<std::string, bool> vanillaFiles;
    std::optional<std::filesystem::path> vanillaDirectory;
};

bool RowReader::isVanilla(const base::SourceLocation& location) {
    const std::string key = location.file.string();
    if (const auto found = vanillaFiles.find(key); found != vanillaFiles.end()) {
        return found->second;
    }
    if (!vanillaDirectory) {
        std::error_code error;
        vanillaDirectory = std::filesystem::weakly_canonical(files.vanillaDirectory, error);
    }
    std::error_code error;
    const std::filesystem::path file = std::filesystem::weakly_canonical(location.file, error);
    const std::filesystem::path relative = file.lexically_relative(*vanillaDirectory);
    const bool inside = !relative.empty() && *relative.begin() != "..";
    vanillaFiles.emplace(key, inside);
    return inside;
}

void RowReader::read(const cparser::TableRow& row) {
    if (row.argumentTokens.size() != FirstTextArgument + LanguageCount) {
        table.diagnostics.push_back(base::makeWarning(
            row.location,
            std::format("{} has {} arguments where {} were expected; it is left out", row.macro,
                        row.argumentTokens.size(), FirstTextArgument + LanguageCount)));
        return;
    }
    const std::optional<u16> id = parseId(row.argumentTokens[0]);
    if (!id) {
        table.diagnostics.push_back(base::makeWarning(
            row.location,
            std::format("the message ID `{}` is not a number from 0 to 0xFFFF; the message is "
                        "left out",
                        row.arguments[0])));
        return;
    }
    Message message{.id = *id,
                    .boxType = std::string(row.arguments[1]),
                    .boxPosition = std::string(row.arguments[2]),
                    .macro = findMacro(row.macro),
                    .isVanilla = isVanilla(row.location),
                    .location = row.location};
    for (usize language = 0; language < LanguageCount; ++language) {
        std::expected<MessageText, base::Diagnostic> text =
            readText(row.argumentTokens[FirstTextArgument + language],
                     row.arguments[FirstTextArgument + language]);
        if (!text) {
            text.error().message = std::format("message {}: {}; the message is left out",
                                               makeIdText(*id), text.error().message);
            table.diagnostics.push_back(std::move(text.error()));
            return;
        }
        message.texts[language] = std::move(*text);
    }
    for (const LanguageGroup group : {LanguageGroup::Japanese, LanguageGroup::Nes}) {
        if (!isInGroup(message.macro, group)) {
            continue;
        }
        const auto [first, isNew] = firstDefinitions.emplace(std::pair{group, *id}, row.location);
        if (!isNew) {
            table.diagnostics.push_back(base::makeWarning(
                row.location,
                std::format("message {} is defined again; the game only ever finds the first, "
                            "at {}:{}",
                            makeIdText(*id), first->second.file.filename().string(),
                            first->second.line)));
            break;
        }
    }
    table.messages.push_back(std::move(message));
}

void RowReader::checkOrder() {
    const std::vector<Message>& messages = table.messages;
    for (usize index = 0; index < messages.size(); ++index) {
        const Message& message = messages[index];
        if (message.id == LastMessageId && index + 1 != messages.size()) {
            table.diagnostics.push_back(base::makeWarning(
                message.location,
                std::format("message {} is not last, which breaks the message debugger",
                            makeIdText(LastMessageId))));
        }
        if (message.id == FontMessageId &&
            (index + 1 == messages.size() || messages[index + 1].id != LastMessageId)) {
            table.diagnostics.push_back(base::makeWarning(
                message.location,
                std::format("message {} does not stand right before {}, which breaks "
                            "Font_LoadOrderedFont",
                            makeIdText(FontMessageId), makeIdText(LastMessageId))));
        }
    }
}

} // namespace

MessageFiles findMessageFiles(const std::filesystem::path& repository,
                              const std::string_view version) {
    const std::filesystem::path extracted = repository / "extracted" / version;
    return MessageFiles{.table = repository / "assets/text/message_data.h",
                        .vanillaDirectory = extracted / "text",
                        .searchPaths = {repository, extracted, repository / "include"}};
}

MessageTable readMessageTable(const cparser::MacroTable& macros, const MessageFiles& files) {
    MessageTable table;
    const cparser::PreprocessOptions options{
        .searchPaths = files.searchPaths,
        .allowedRoots = {files.table.parent_path(), files.vanillaDirectory},
        .dialect = &z64::getDecompDialect()};
    std::expected<cparser::TableResult, base::Diagnostic> rows =
        cparser::readTable(macros, files.table, EntryMacros, options);
    if (!rows) {
        table.diagnostics.push_back(std::move(rows.error()));
        return table;
    }
    table.diagnostics = std::move(rows->diagnostics);
    RowReader reader(files, table);
    for (const cparser::TableRow& row : rows->rows) {
        reader.read(row);
    }
    reader.checkOrder();
    return table;
}

MessageTable readMessages(const hackeroot::Workspace& workspace) {
    return readMessageTable(
        workspace.getMacros(),
        findMessageFiles(workspace.getRoot(), workspace.getBuildDefines().version));
}

} // namespace toki::message

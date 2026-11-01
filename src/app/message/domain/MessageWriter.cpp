#include "MessageWriter.h"

#include "hackeroot/CheckoutEdits.h"

#include <cctype>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

constexpr u16 FontMessageId = 0xFFFC;
constexpr std::string_view KeptLastNote =
    "/*\n"
    " * The following two messages should be kept last and in this order.\n"
    " * Message 0xFFFD must be last to not break the message debugger (see "
    "R_MESSAGE_DEBUGGER_TEXTID).\n"
    " * Message 0xFFFC must be immediately before message 0xFFFD to not break "
    "Font_LoadOrderedFont.\n"
    " */\n";

struct Token {
    std::string text;
    bool isString = false;
    bool endsWithHexEscape = false;
};

class LineWriter {
public:
    void addText(const std::string_view text) {
        for (const char character : text) {
            const auto byte = static_cast<unsigned char>(character);
            const bool isHexDigit = std::isxdigit(byte) != 0;
            Token& token = openString(isHexDigit);
            if (character == '"' || character == '\\') {
                token.text += '\\';
                token.text += character;
                token.endsWithHexEscape = false;
            } else if (byte < 0x20 || byte == 0x7F) {
                token.text += std::format("\\x{:02X}", byte);
                token.endsWithHexEscape = true;
            } else {
                token.text += character;
                token.endsWithHexEscape = false;
            }
        }
    }

    void addNewline() {
        Token& token = openString(false);
        token.text += "\\n";
        token.endsWithHexEscape = false;
        endLine();
    }

    void addCode(const CodePiece& code) {
        std::string text = code.name;
        if (!code.arguments.empty()) {
            text += '(';
            for (usize index = 0; index < code.arguments.size(); ++index) {
                text += (index == 0 ? "" : ", ") + code.arguments[index];
            }
            text += ')';
        }
        tokens.push_back(Token{.text = std::move(text)});
    }

    [[nodiscard]] std::string finish() {
        endLine();
        return std::move(written);
    }

private:
    Token& openString(const bool startsWithHexDigit) {
        if (tokens.empty() || !tokens.back().isString ||
            (startsWithHexDigit && tokens.back().endsWithHexEscape)) {
            tokens.push_back(Token{.isString = true});
        }
        return tokens.back();
    }

    void endLine() {
        if (tokens.empty()) {
            return;
        }
        for (usize index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            written += index == 0 ? "" : " ";
            written += token.isString ? std::format("\"{}\"", token.text) : token.text;
        }
        written += '\n';
        tokens.clear();
    }

    std::vector<Token> tokens;
    std::string written;
};

[[nodiscard]] std::string_view getMacroName(const MessageMacro macro) {
    switch (macro) {
    case MessageMacro::Message:
        return "DEFINE_MESSAGE";
    case MessageMacro::JapaneseOnly:
        return "DEFINE_MESSAGE_JPN";
    case MessageMacro::NesOnly:
        return "DEFINE_MESSAGE_NES";
    case MessageMacro::FontMessage:
        return "DEFINE_MESSAGE_FFFC";
    }
    return "DEFINE_MESSAGE";
}

} // namespace

std::string writeMessageText(const MessageText& text) {
    if (!text.isPresent) {
        return text.placeholder.empty() ? std::string("MSG()")
                                        : std::format("MSG(/* {} */)", text.placeholder);
    }
    if (text.pieces.empty()) {
        return "MSG(\n\"\"\n)";
    }
    LineWriter writer;
    for (const Piece& piece : text.pieces) {
        if (const auto* run = std::get_if<TextPiece>(&piece)) {
            writer.addText(run->text);
        } else if (std::holds_alternative<NewlinePiece>(piece)) {
            writer.addNewline();
        } else {
            writer.addCode(std::get<CodePiece>(piece));
        }
    }
    return std::format("MSG(\n{})", writer.finish());
}

std::string writeMessageRow(const Message& message) {
    std::string row = std::format("{}(0x{:04X}, {}, {},\n", getMacroName(message.macro), message.id,
                                  message.boxType, message.boxPosition);
    for (usize language = 0; language < LanguageCount; ++language) {
        row += language == 0 ? "" : "\n,\n";
        row += writeMessageText(message.texts[language]);
    }
    row += "\n)\n";
    return row;
}

std::string writeMessageFile(const MessageTable& table, const FindSwitch& findSwitch,
                             const std::span<const KeptText> kept) {
    std::string file =
        std::format("{}\n#include \"config.h\"\n", hackeroot::makeTokiMarker("messages"));
    std::optional<std::string> open;
    const auto writeKept = [&](const usize index) {
        for (const KeptText& text : kept) {
            if (text.beforeIndex != index &&
                !(index == table.messages.size() && text.beforeIndex > index)) {
                continue;
            }
            if (open) {
                file += "\n#endif\n";
                open.reset();
            }
            file += '\n';
            file += text.text;
        }
    };
    for (usize index = 0; index < table.messages.size(); ++index) {
        writeKept(index);
        const Message& message = table.messages[index];
        const std::optional<std::string> under = findSwitch ? findSwitch(message) : std::nullopt;
        if (under != open) {
            if (open) {
                file += "\n#endif\n";
            }
            if (under) {
                file += std::format("\n#if {}\n", *under);
            }
            open = under;
        }
        file += '\n';
        if (message.id == FontMessageId) {
            file += KeptLastNote;
        }
        file += writeMessageRow(message);
    }
    if (open) {
        file += "\n#endif\n";
        open.reset();
    }
    writeKept(table.messages.size());
    return file;
}

} // namespace toki::message

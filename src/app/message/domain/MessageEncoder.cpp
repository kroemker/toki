#include "MessageEncoder.h"

#include "base/Diagnostic.h"
#include "cparser/Expand.h"
#include "cparser/Expression.h"
#include "cparser/Initializer.h"
#include "cparser/MacroTable.h"
#include "cparser/Scanner.h"
#include "cparser/SourceFile.h"
#include "cparser/Spellings.h"
#include "cparser/Token.h"
#include "cparser/TypeTable.h"
#include "cparser/Value.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"

#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

constexpr usize DataAlignment = 4;

[[nodiscard]] std::string makeCall(const CodePiece& code) {
    std::string call = code.name;
    if (code.arguments.empty()) {
        return call;
    }
    call += '(';
    for (usize index = 0; index < code.arguments.size(); ++index) {
        if (index != 0) {
            call += ", ";
        }
        call += code.arguments[index];
    }
    call += ')';
    return call;
}

void appendCode(std::vector<u8>& bytes, const u16 code) {
    if (code > 0xFF) {
        bytes.push_back(static_cast<u8>(code >> 8));
    }
    bytes.push_back(static_cast<u8>(code));
}

[[nodiscard]] std::expected<std::string, std::string> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(std::format("{} could not be opened", path.string()));
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

EncodedText MessageEncoder::encode(const MessageText& text) const {
    EncodedText encoded;
    for (const Piece& piece : text.pieces) {
        if (const auto* run = std::get_if<TextPiece>(&piece)) {
            encodeText(run->text, encoded);
        } else if (std::holds_alternative<NewlinePiece>(piece)) {
            encodeText("\n", encoded);
        } else {
            encodeCode(std::get<CodePiece>(piece), encoded);
        }
    }
    encodeCode(CodePiece{.name = "END"}, encoded);
    return encoded;
}

void MessageEncoder::encodeText(const std::string_view text, EncodedText& encoded) const {
    usize offset = 0;
    while (offset < text.size()) {
        if (const CharmapEntry* entry = charmap.findMatch(text, offset)) {
            appendCode(encoded.bytes, entry->code);
            offset += entry->text.size();
        } else {
            encoded.bytes.push_back(static_cast<u8>(text[offset]));
            ++offset;
        }
    }
}

void MessageEncoder::encodeCode(const CodePiece& code, EncodedText& encoded) const {
    const cparser::Macro* macro = macros.find(code.name);
    if (macro == nullptr) {
        encoded.diagnostics.push_back(base::makeWarning(
            code.location,
            std::format("`{}` is no control code message_data_fmt.h defines", code.name)));
        return;
    }
    if (macro->functionLike != !code.arguments.empty() ||
        (!macro->variadic && macro->parameters.size() != code.arguments.size())) {
        encoded.diagnostics.push_back(base::makeWarning(
            code.location, std::format("`{}` takes {} arguments, not {}", code.name,
                                       macro->parameters.size(), code.arguments.size())));
        return;
    }

    const cparser::SourceFile call{.path = "<control code>", .contents = makeCall(code)};
    std::vector<cparser::Token> tokens = cparser::scan(call).tokens;
    std::erase_if(tokens, [](const cparser::Token& token) {
        return token.kind == cparser::TokenKind::EndOfFile;
    });
    cparser::Spellings spellings;
    std::vector<base::Diagnostic> diagnostics;
    const std::vector<cparser::Token> expanded = cparser::expand(
        tokens, cparser::ExpandOptions{.macros = &macros, .spellings = &spellings}, diagnostics);

    const cparser::EvalContext context{
        .macros = &macros,
        .dialect = &z64::getDecompDialect(),
        .resolve = [this](const std::string_view name) -> std::optional<cparser::Value> {
            if (const std::optional<s64> value = types.findEnumerator(name)) {
                return cparser::makeInteger(*value);
            }
            return std::nullopt;
        },
        .unknownIsZero = false};
    for (const std::span<const cparser::Token> element : cparser::splitElements(expanded)) {
        const std::expected<cparser::Value, base::Diagnostic> value =
            cparser::evaluate(element, context, diagnostics);
        if (!value) {
            diagnostics.push_back(value.error());
            break;
        }
        encoded.bytes.push_back(static_cast<u8>(cparser::getInteger(*value)));
    }
    for (base::Diagnostic& diagnostic : diagnostics) {
        diagnostic.location = code.location;
        diagnostic.message = std::format("{}: {}", makeCall(code), diagnostic.message);
        encoded.diagnostics.push_back(std::move(diagnostic));
    }
}

std::expected<LayoutRequest, std::string>
MessageFormat::makeLayoutRequest(const Message& message) const {
    const std::optional<s64> boxType = prelude->types.findEnumerator(message.boxType);
    if (!boxType) {
        return std::unexpected(std::format("`{}` is no box type", message.boxType));
    }
    const std::optional<s64> boxPosition = prelude->types.findEnumerator(message.boxPosition);
    if (!boxPosition) {
        return std::unexpected(std::format("`{}` is no box position", message.boxPosition));
    }
    const std::optional<s64> firstQuestItem =
        prelude->types.findEnumerator("ITEM_MEDALLION_FOREST");
    if (!firstQuestItem) {
        return std::unexpected("include/item.h defines no ITEM_MEDALLION_FOREST");
    }
    return LayoutRequest{.boxType = static_cast<u8>(*boxType),
                         .boxPosition = static_cast<u8>(*boxPosition),
                         .firstQuestItem = *firstQuestItem};
}

MessageEncoder MessageFormat::makeEncoder() const {
    return MessageEncoder(prelude->macros, prelude->types, charmap);
}

std::expected<MessageFormat, base::Diagnostic>
readMessageFormat(const hackeroot::Workspace& workspace) {
    const std::filesystem::path charmapPath = workspace.getRoot() / "assets/text/charmap.nes.txt";
    const std::expected<std::string, std::string> contents = readFile(charmapPath);
    if (!contents) {
        return std::unexpected(base::makeError({.file = charmapPath}, contents.error()));
    }
    std::expected<Charmap, std::string> charmap = parseCharmap(*contents);
    if (!charmap) {
        return std::unexpected(base::makeError({.file = charmapPath}, charmap.error()));
    }
    const std::array defines = {std::string("MESSAGE_DATA_STATIC")};
    const std::array includes = {std::string("message_data_static.h")};
    return MessageFormat{.prelude = std::make_unique<hackeroot::Prelude>(
                             hackeroot::readPreludeWithDefines(workspace, defines, includes)),
                         .charmap = std::move(*charmap)};
}

bool isConditionTrue(const cparser::MacroTable& macros, const std::string_view condition) {
    const cparser::SourceFile file{.path = "<condition>", .contents = std::string(condition)};
    std::vector<cparser::Token> tokens = cparser::scan(file).tokens;
    std::erase_if(tokens, [](const cparser::Token& token) {
        return token.kind == cparser::TokenKind::EndOfFile;
    });
    std::vector<base::Diagnostic> diagnostics;
    const std::expected<cparser::Value, base::Diagnostic> value = cparser::evaluate(
        tokens, cparser::EvalContext{.macros = &macros, .dialect = &z64::getDecompDialect()},
        diagnostics);
    return value && cparser::getInteger(*value) != 0;
}

bool isFontMessageEnglish(const cparser::MacroTable& macros) {
    return !isConditionTrue(macros, "OOT_NTSC");
}

EncodedData encodeMessageData(const MessageTable& table, const MessageEncoder& encoder,
                              const Language language, const bool includesFontMessage) {
    EncodedData data;
    for (const Message& message : table.messages) {
        if (!isInLanguage(message, language, includesFontMessage)) {
            data.offsets.emplace_back();
            continue;
        }
        while (data.bytes.size() % DataAlignment != 0) {
            data.bytes.push_back(0);
        }
        data.offsets.emplace_back(data.bytes.size());
        EncodedText text = encoder.encode(message.texts[static_cast<usize>(language)]);
        data.bytes.insert(data.bytes.end(), text.bytes.begin(), text.bytes.end());
        for (base::Diagnostic& diagnostic : text.diagnostics) {
            data.diagnostics.push_back(std::move(diagnostic));
        }
    }
    return data;
}

} // namespace toki::message

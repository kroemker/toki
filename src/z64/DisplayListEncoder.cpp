#include "DisplayListEncoder.h"

#include "base/Diagnostic.h"
#include "cparser/Dialect.h"
#include "cparser/Expand.h"
#include "cparser/Expression.h"
#include "cparser/Initializer.h"
#include "cparser/Spellings.h"
#include "cparser/Token.h"
#include "cparser/Value.h"
#include "z64/DecompDialect.h"
#include "z64/DisplayList.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr usize WordsPerCommand = 2;
constexpr usize BytesPerCommand = WordsPerCommand * 4;

void appendWord(std::vector<u8>& bytes, const u32 word) {
    bytes.push_back(static_cast<u8>(word >> 24));
    bytes.push_back(static_cast<u8>(word >> 16));
    bytes.push_back(static_cast<u8>(word >> 8));
    bytes.push_back(static_cast<u8>(word));
}

std::string describeElement(const std::span<const cparser::Token> element) {
    const cparser::Token& first = element.front();
    if (first.kind == cparser::TokenKind::Identifier) {
        return std::string(first.text);
    }
    return std::format("the element at '{}'", first.text);
}

std::string makeSpelling(const std::span<const cparser::Token> element) {
    std::string spelling;
    for (const cparser::Token& token : element) {
        spelling += token.text;
        spelling += ' ';
    }
    return spelling;
}

bool isBuiltin(const std::string_view name) {
    return cparser::findBuiltin(getDecompDialect(), name) != nullptr;
}

bool isCall(const std::span<const cparser::Token> element) {
    return element.size() >= 2 && element[0].kind == cparser::TokenKind::Identifier &&
           cparser::isPunctuator(element[1], "(") && !isBuiltin(element[0].text);
}

} // namespace

DisplayListEncoder::DisplayListEncoder(DisplayListContext context)
    : context(std::move(context)), evalContext{.dialect = &getDecompDialect(),
                                               .findSymbol = this->context.findSymbol,
                                               .findArrayLength = this->context.findArrayLength,
                                               .unknownIsZero = false} {}

DisplayList DisplayListEncoder::encode(const std::span<const cparser::Token> initializer) {
    DisplayList list;
    for (const std::span<const cparser::Token> element : cparser::splitElements(initializer)) {
        encodeElement(element, list);
    }
    return list;
}

void DisplayListEncoder::encodeElement(const std::span<const cparser::Token> element,
                                       DisplayList& list) {
    const base::SourceLocation location = cparser::locate(element.front());
    const std::string name = describeElement(element);
    std::string spelling = makeSpelling(element);

    std::optional<std::vector<Word>> words;
    if (const auto found = remembered.find(spelling); found != remembered.end()) {
        words = found->second;
    } else {
        cparser::Spellings spellings;
        const usize reported = list.diagnostics.size();
        const std::vector<cparser::Token> expanded = cparser::expand(
            element,
            cparser::ExpandOptions{
                .macros = context.macros, .keep = &isBuiltin, .spellings = &spellings},
            list.diagnostics);
        words = evaluateWords(expanded, name, location, list);
        if (words && list.diagnostics.size() == reported) {
            remembered.emplace(std::move(spelling), *words);
        }
    }
    if (!words) {
        return;
    }
    const usize first = list.bytes.size() / BytesPerCommand;
    for (const Word& word : *words) {
        if (!word.symbol.empty()) {
            list.relocations.push_back(
                Relocation{.offset = list.bytes.size(), .symbol = word.symbol});
        }
        appendWord(list.bytes, word.value);
    }
    const bool isNamed = element.front().kind == cparser::TokenKind::Identifier;
    list.commands.push_back(GbiCommand{.macro = isNamed ? name : std::string(),
                                       .firstCommand = first,
                                       .commandCount = words->size() / WordsPerCommand,
                                       .location = location});
}

std::optional<DisplayListEncoder::Word>
DisplayListEncoder::evaluateWord(const std::span<const cparser::Token> word,
                                 DisplayList& list) const {
    const std::expected<cparser::Value, base::Diagnostic> value =
        cparser::evaluate(word, evalContext, list.diagnostics);
    if (!value) {
        list.diagnostics.push_back(value.error());
        return std::nullopt;
    }
    return Word{.value = static_cast<u32>(cparser::getInteger(*value)),
                .symbol = std::string(value->symbol)};
}

std::optional<std::vector<DisplayListEncoder::Word>>
DisplayListEncoder::evaluateWords(const std::span<const cparser::Token> expanded,
                                  const std::string& name, const base::SourceLocation& location,
                                  DisplayList& list) const {
    const auto fail = [&](std::string message) -> std::optional<std::vector<Word>> {
        list.diagnostics.push_back(base::makeError(location, std::move(message)));
        return std::nullopt;
    };
    std::vector<Word> words;
    for (const std::span<const cparser::Token> element : cparser::splitElements(expanded)) {
        if (isCall(element)) {
            return fail(
                std::format("'{}' is not a gbi macro this checkout defines", element.front().text));
        }
        const std::optional<std::span<const cparser::Token>> body = cparser::getBraceBody(element);
        if (!body) {
            std::optional<Word> word = evaluateWord(element, list);
            if (!word) {
                return std::nullopt;
            }
            words.push_back(std::move(*word));
            continue;
        }
        const std::vector<std::span<const cparser::Token>> halves = cparser::splitElements(*body);
        if (halves.size() != WordsPerCommand || words.size() % WordsPerCommand != 0) {
            return fail(std::format("{} does not come out as pairs of words", name));
        }
        for (const std::span<const cparser::Token> half : halves) {
            std::optional<Word> word = evaluateWord(half, list);
            if (!word) {
                return std::nullopt;
            }
            words.push_back(std::move(*word));
        }
    }
    if (words.empty() || words.size() % WordsPerCommand != 0) {
        return fail(std::format("{} does not come out as pairs of words", name));
    }
    return words;
}

} // namespace z64

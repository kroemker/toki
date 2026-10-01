#include "TableReader.h"

#include "z64/Diagnostic.h"
#include "z64/Initializer.h"
#include "z64/Preprocessor.h"
#include "z64/Token.h"
#include "z64/Types.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

[[nodiscard]] std::string_view spellingOf(std::span<const Token> argument) {
    if (argument.empty()) {
        return {};
    }
    const Token& first = argument.front();
    const Token& last = argument.back();
    if (first.file != last.file) {
        return first.text;
    }
    const auto length = static_cast<usize>(last.text.data() + last.text.size() - first.text.data());
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) - the size is right there.
    return {first.text.data(), length};
}

[[nodiscard]] usize findClose(std::span<const Token> tokens, usize open) {
    int nesting = 0;
    for (usize cursor = open; cursor < tokens.size(); ++cursor) {
        if (isPunctuator(tokens[cursor], "(")) {
            ++nesting;
        } else if (isPunctuator(tokens[cursor], ")")) {
            --nesting;
            if (nesting == 0) {
                return cursor;
            }
        }
    }
    return tokens.size();
}

} // namespace

std::expected<TableResult, Diagnostic> readTable(const MacroTable& base,
                                                 const std::filesystem::path& file,
                                                 std::span<const std::string_view> entryMacros,
                                                 const PreprocessOptions& options) {
    PreprocessOptions strict = options;
    strict.strictConditionals = true;

    std::expected<TranslationUnit, Diagnostic> unit = preprocess(base, file, strict);
    if (!unit) {
        return std::unexpected(unit.error());
    }

    TableResult result;
    result.diagnostics = std::move(unit->diagnostics);

    const std::span<const Token> tokens(unit->tokens);
    for (usize cursor = 0; cursor + 1 < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind != TokenKind::Identifier ||
            std::ranges::find(entryMacros, token.text) == entryMacros.end() ||
            !isPunctuator(tokens[cursor + 1], "(")) {
            continue;
        }

        const usize close = findClose(tokens, cursor + 1);
        if (close == tokens.size()) {
            result.diagnostics.push_back(
                warning(locate(token), "the entry's argument list is never closed"));
            break;
        }

        TableRow row;
        row.ordinal = result.rows.size();
        row.macro = token.text;
        row.location = locate(token);
        for (const std::span<const Token> argument :
             splitElements(tokens.subspan(cursor + 2, close - cursor - 2))) {
            row.arguments.push_back(spellingOf(argument));
        }
        result.rows.push_back(std::move(row));
        cursor = close;
    }
    result.sources = std::move(unit->sources);
    return result;
}

} // namespace z64

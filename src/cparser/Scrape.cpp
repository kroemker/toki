#include "Scrape.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Scanner.h"
#include "cparser/SourceFile.h"
#include "cparser/SourceSet.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cparser {

namespace {

template <typename Visit>
void visitDirectives(std::span<const Token> tokens, Visit visit) {
    for (usize cursor = 0; cursor + 1 < tokens.size(); ++cursor) {
        if (!tokens[cursor].startsLine || !isPunctuator(tokens[cursor], "#")) {
            continue;
        }

        usize end = cursor + 1;
        while (end < tokens.size() && !tokens[end].startsLine &&
               tokens[end].kind != TokenKind::EndOfFile) {
            ++end;
        }
        if (end > cursor + 1 && tokens[cursor + 1].kind == TokenKind::Identifier) {
            visit(tokens[cursor + 1].text, tokens.subspan(cursor + 2, end - cursor - 2));
        }
        cursor = end - 1;
    }
}

void scrapeFile(const std::filesystem::path& path, ScrapeResult& result) {
    const std::expected<const SourceFile*, base::Diagnostic> file = result.sources.open(path);
    if (!file) {
        result.diagnostics.push_back(file.error());
        return;
    }
    ++result.headers;

    const ScanResult scanned = scan(**file);
    visitDirectives(scanned.tokens,
                    [&result](std::string_view directive, std::span<const Token> rest) {
                        if (directive != "define") {
                            return;
                        }
                        if (std::optional<Macro> macro = parseDefinition(rest)) {
                            result.macros.defineIfAbsent(std::move(*macro));
                            ++result.definitions;
                        }
                    });
}

} // namespace

std::vector<std::string_view> findMentionedNames(const SourceFile& file) {
    std::vector<std::string_view> names;
    const ScanResult scanned = scan(file);
    visitDirectives(scanned.tokens,
                    [&names](std::string_view directive, std::span<const Token> rest) {
                        if ((directive == "define" || directive == "undef") && !rest.empty() &&
                            rest.front().kind == TokenKind::Identifier) {
                            names.push_back(rest.front().text);
                        }
                    });
    return names;
}

ScrapeResult scrapeDefinitions(const std::filesystem::path& directory) {
    ScrapeResult result;

    std::error_code failure;
    std::vector<std::filesystem::path> headers;
    for (std::filesystem::recursive_directory_iterator entry(directory, failure);
         !failure && entry != std::filesystem::recursive_directory_iterator();
         entry.increment(failure)) {
        std::error_code typeFailure;
        if (entry->is_regular_file(typeFailure) && entry->path().extension() == ".h") {
            headers.push_back(entry->path());
        }
    }
    if (failure) {
        result.diagnostics.push_back(
            base::makeError(base::SourceLocation{.file = directory},
                            std::format("cannot list the header tree: {}", failure.message())));
        return result;
    }

    // Sorted so a name defined in two headers resolves the same way on every
    // machine, rather than following directory order.
    std::ranges::sort(headers);
    for (const std::filesystem::path& header : headers) {
        scrapeFile(header, result);
    }
    return result;
}

} // namespace cparser

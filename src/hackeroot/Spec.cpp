#include "Spec.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/StringLiteral.h"
#include "cparser/Token.h"
#include "z64/DecompDialect.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::string_view BuildDirectory = "$(BUILD_DIR)/";

[[nodiscard]] std::optional<std::string> readString(const cparser::Token& token) {
    if (token.kind != cparser::TokenKind::String || token.text.size() < 2) {
        return std::nullopt;
    }
    std::string text = cparser::decodeEscapes(token.text.substr(1, token.text.size() - 2));
    if (text.starts_with(BuildDirectory)) {
        text.erase(0, BuildDirectory.size());
    }
    return text;
}

} // namespace

const SpecSegment* Spec::findSegment(const std::string_view name) const {
    const auto found = std::ranges::find(segments, name, &SpecSegment::name);
    return found == segments.end() ? nullptr : &*found;
}

Spec readSpec(const cparser::MacroTable& macros, const std::filesystem::path& repository) {
    Spec spec;
    const std::filesystem::path file = repository / "spec" / "spec";
    std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        cparser::preprocess(macros, file,
                            cparser::PreprocessOptions{.searchPaths = {repository},
                                                       .dialect = &z64::getDecompDialect()});
    if (!unit) {
        spec.diagnostics.push_back(std::move(unit.error()));
        return spec;
    }
    std::optional<SpecSegment> segment;
    const std::vector<cparser::Token>& tokens = unit->tokens;
    for (usize at = 0; at < tokens.size(); ++at) {
        const cparser::Token& token = tokens[at];
        const std::optional<std::string> argument =
            at + 1 < tokens.size() ? readString(tokens[at + 1]) : std::nullopt;
        if (cparser::isIdentifier(token, "beginseg")) {
            segment.emplace();
        } else if (cparser::isIdentifier(token, "endseg") && segment) {
            spec.segments.push_back(std::move(*segment));
            segment.reset();
        } else if (segment && argument && cparser::isIdentifier(token, "name")) {
            segment->name = *argument;
        } else if (segment && argument && cparser::isIdentifier(token, "include")) {
            segment->includes.push_back(*argument);
        }
    }
    return spec;
}

} // namespace hackeroot

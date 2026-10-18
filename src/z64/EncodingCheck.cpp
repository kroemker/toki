#include "EncodingCheck.h"

#include "cparser/Scanner.h"
#include "cparser/SourceFile.h"
#include "cparser/Token.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace z64 {

namespace {

[[nodiscard]] bool isSameAddress(const Relocation& left, const Relocation& right) {
    return left.offset == right.offset && left.symbol == right.symbol;
}

} // namespace

ElementCheck makeEncodingCheck(DataContext context) {
    return [context = std::move(context)](const ElementData& element, const std::string_view text) {
        const cparser::SourceFile file{.path = "element.c", .contents = std::string(text)};
        const cparser::ScanResult scanned = cparser::scan(file);
        if (!scanned.diagnostics.empty()) {
            return false;
        }
        std::span<const cparser::Token> tokens = scanned.tokens;
        while (!tokens.empty() && tokens.back().kind == cparser::TokenKind::EndOfFile) {
            tokens = tokens.first(tokens.size() - 1);
        }
        DataEncoder encoder(context);
        const EncodedData data = encoder.encode(*element.type, tokens, false);
        return data.diagnostics.empty() && std::ranges::equal(data.bytes, element.bytes) &&
               std::ranges::equal(data.relocations, element.relocations, isSameAddress);
    };
}

} // namespace z64

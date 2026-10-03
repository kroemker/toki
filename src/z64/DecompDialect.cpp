#include "DecompDialect.h"

#include "base/Diagnostic.h"
#include "cparser/Dialect.h"
#include "cparser/ScalarType.h"
#include "cparser/Token.h"
#include "cparser/Value.h"
#include "z64/Types.h"

#include <array>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string_view>

namespace z64 {

namespace {

using cparser::ScalarType;

constexpr std::array Types = {
    ScalarType{.name = "char", .size = 1, .isSigned = false},
    ScalarType{.name = "short", .size = 2},
    ScalarType{.name = "int", .size = 4},
    ScalarType{.name = "long", .size = 4},
    ScalarType{.name = "signed", .size = 4},
    ScalarType{.name = "unsigned", .size = 4, .isSigned = false},
    ScalarType{.name = "float", .size = 4, .isReal = true},
    ScalarType{.name = "double", .size = 8, .isReal = true},
    ScalarType{.name = "void", .size = 0},
    ScalarType{.name = "u8", .size = 1, .isSigned = false},
    ScalarType{.name = "u16", .size = 2, .isSigned = false},
    ScalarType{.name = "u32", .size = 4, .isSigned = false},
    ScalarType{.name = "u64", .size = 8, .isSigned = false},
    ScalarType{.name = "s8", .size = 1},
    ScalarType{.name = "s16", .size = 2},
    ScalarType{.name = "s32", .size = 4},
    ScalarType{.name = "s64", .size = 8},
    ScalarType{.name = "f32", .size = 4, .isReal = true},
    ScalarType{.name = "f64", .size = 8, .isReal = true},
    ScalarType{.name = "size_t", .size = 4, .isSigned = false},
    ScalarType{.name = "uintptr_t", .size = 4, .isSigned = false},
    ScalarType{.name = "intptr_t", .size = 4},
};

[[nodiscard]] std::expected<cparser::Value, base::Diagnostic>
arrayCount(const cparser::BuiltinCall& call) {
    const std::span<const std::span<const cparser::Token>> arguments = call.getArguments();
    if (arguments.size() != 1 || arguments.front().size() != 1 ||
        arguments.front().front().kind != cparser::TokenKind::Identifier) {
        return std::unexpected(
            base::makeError(call.getLocation(), "ARRAY_COUNT expects an array name"));
    }

    const std::string_view name = arguments.front().front().text;
    if (const std::optional<usize> length = call.findArrayLength(name)) {
        return cparser::makeInteger(static_cast<s64>(*length));
    }
    call.report(base::makeWarning(
        call.getLocation(), std::format("ARRAY_COUNT({}) has no declaration to count", name)));
    return cparser::makeInteger(0);
}

[[nodiscard]] std::expected<cparser::Value, base::Diagnostic>
textureLength(const cparser::BuiltinCall& call) {
    const std::span<const std::span<const cparser::Token>> arguments = call.getArguments();
    if (arguments.size() != 4) {
        return std::unexpected(base::makeError(
            call.getLocation(), "TEX_LEN expects a type, a width, a height and a bit depth"));
    }
    const ScalarType* element =
        arguments.front().size() == 1 ? call.findType(arguments.front().front().text) : nullptr;
    if (element == nullptr || element->size == 0) {
        return std::unexpected(
            base::makeError(call.getLocation(), "TEX_LEN expects an element type first"));
    }

    s64 bits = 1;
    for (const std::span<const cparser::Token> argument : arguments.subspan(1)) {
        const std::expected<cparser::Value, base::Diagnostic> value = call.evaluate(argument);
        if (!value) {
            return std::unexpected(value.error());
        }
        bits *= cparser::getInteger(*value);
    }
    return cparser::makeInteger(bits / 8 / element->size);
}

constexpr std::array Builtins = {
    cparser::Builtin{.name = "ARRAY_COUNT", .evaluate = &arrayCount},
    cparser::Builtin{.name = "ARRAY_COUNTU", .evaluate = &arrayCount},
    cparser::Builtin{.name = "ARRAY_COUNT_2D", .evaluate = &arrayCount},
    cparser::Builtin{.name = "TEX_LEN", .evaluate = &textureLength},
};

constexpr cparser::Dialect Decomp{.types = Types, .builtins = Builtins};

} // namespace

const cparser::Dialect& getDecompDialect() {
    return Decomp;
}

} // namespace z64

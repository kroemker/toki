#pragma once

#include "z64/Types.h"

#include <optional>
#include <string_view>

namespace z64 {

struct Token;

/// A constant expression's result. Decomp data is overwhelmingly integer, but
/// fast64 writes rotations as `DEG_TO_BINANG(0.000)` and vertex scales as
/// floats, so both have to survive evaluation.
struct Value {
    bool real = false;
    s64 integer = 0;
    f64 number = 0.0;
};

[[nodiscard]] Value integerValue(s64 value);

[[nodiscard]] Value realValue(f64 value);

[[nodiscard]] s64 asInteger(const Value& value);

[[nodiscard]] f64 asReal(const Value& value);

/// The value of an integer, floating point or character literal as the decomp
/// spells it, including the `0x`, `0`, `u`, `L` and `f` forms.
[[nodiscard]] std::optional<Value> literalValue(const Token& token);

/// One binary C operator. Anything the parser did not recognise as an operator
/// never reaches here, so an unknown spelling is treated as `!=`.
[[nodiscard]] Value applyOperator(std::string_view op, const Value& left, const Value& right);

[[nodiscard]] bool isCastType(std::string_view name);

[[nodiscard]] s64 sizeOfType(std::string_view name);

/// Applies a cast the way the decomp's type would: `(s16)` wraps, `(f32)`
/// converts, and a pointer cast leaves the bits alone.
[[nodiscard]] Value castTo(std::string_view type, bool pointer, const Value& operand);

} // namespace z64

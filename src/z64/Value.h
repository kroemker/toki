#pragma once

#include "z64/Types.h"

#include <optional>
#include <string_view>

namespace z64 {

struct Token;

enum class ValueType : u8 {
    Integer,
    Float,
    Double,
};

/// A constant expression's result. Decomp data is overwhelmingly integer, but
/// fast64 writes rotations as `DEG_TO_BINANG(0.000)` and vertex scales as
/// floats, so both have to survive evaluation, and `float` has to stay
/// `float`: `0x8000 / 180.0f` is computed in single precision, and computing
/// it in double moves some angles by one binary angle unit.
///
/// A `Float` is held in `number` already rounded to single precision.
struct Value {
    ValueType type = ValueType::Integer;
    s64 integer = 0;
    f64 number = 0.0;
};

[[nodiscard]] Value integerValue(s64 value);

/// Rounds to single precision, as a `float` would hold it.
[[nodiscard]] Value floatValue(f64 value);

[[nodiscard]] Value doubleValue(f64 value);

[[nodiscard]] bool isReal(const Value& value);

/// Whether the value counts as true in a condition: nonzero, for a real too.
[[nodiscard]] bool isTrue(const Value& value);

/// Truncates toward zero, the way C converts a real to an integer. C leaves a
/// real outside the integer's range undefined; this saturates, and reads NaN
/// as zero.
[[nodiscard]] s64 asInteger(const Value& value);

[[nodiscard]] f64 asReal(const Value& value);

/// The value of an integer, floating point or character literal as the decomp
/// spells it, including the `0x`, `0`, `u`, `L` and `f` forms.
[[nodiscard]] std::optional<Value> literalValue(const Token& token);

/// One binary C operator. Anything the parser did not recognise as an operator
/// never reaches here, so an unknown spelling is treated as `!=`. A bitwise
/// operator or shift on a real, which C rejects, truncates it to an integer.
/// Integer division by zero is 0 here; the evaluator is what reports it.
[[nodiscard]] Value applyOperator(std::string_view op, const Value& left, const Value& right);

[[nodiscard]] bool isCastType(std::string_view name);

[[nodiscard]] s64 sizeOfType(std::string_view name);

/// Applies a cast the way the decomp's type would: `(s16)` wraps, `(f32)`
/// rounds to single precision, and a pointer cast leaves the bits alone.
/// `char` is unsigned and `long` is 32 bits, as the N64 build compiles them.
[[nodiscard]] Value castTo(std::string_view type, bool pointer, const Value& operand);

} // namespace z64

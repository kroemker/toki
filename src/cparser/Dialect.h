#pragma once

#include "base/Diagnostic.h"
#include "cparser/ScalarType.h"
#include "cparser/Token.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace cparser {

/// What a builtin sees of its own call: the argument tokens as written, split
/// at top-level commas and already macro-expanded, and the evaluator's means to
/// read them.
class BuiltinCall {
public:
    BuiltinCall() = default;
    BuiltinCall(const BuiltinCall&) = delete;
    BuiltinCall(BuiltinCall&&) = delete;
    BuiltinCall& operator=(const BuiltinCall&) = delete;
    BuiltinCall& operator=(BuiltinCall&&) = delete;
    virtual ~BuiltinCall() = default;

    [[nodiscard]] virtual std::span<const std::span<const Token>> getArguments() const = 0;

    [[nodiscard]] virtual const base::SourceLocation& getLocation() const = 0;

    [[nodiscard]] virtual std::expected<Value, base::Diagnostic>
    evaluate(std::span<const Token> tokens) const = 0;

    [[nodiscard]] virtual std::optional<usize> findArrayLength(std::string_view name) const = 0;

    [[nodiscard]] virtual const ScalarType* findType(std::string_view name) const = 0;

    virtual void report(base::Diagnostic diagnostic) const = 0;
};

using BuiltinFunction = std::expected<Value, base::Diagnostic> (*)(const BuiltinCall& call);

/// A name the evaluator answers itself instead of expanding, for macros whose
/// real definitions need something it cannot do, such as `sizeof`.
struct Builtin {
    std::string_view name;
    BuiltinFunction evaluate = nullptr;
};

/// The C a codebase is written in, as far as constant expressions care: the
/// type names a cast may use, with the widths of its target, and the builtins
/// its macros need. The caller supplies it, so nothing about any one target or
/// codebase is written into the evaluator. Both spans must outlive every use.
struct Dialect {
    std::span<const ScalarType> types;
    std::span<const Builtin> builtins;
};

/// Plain C on a 32-bit target: `int` and `long` 32 bits, `char` signed, and no
/// builtins.
[[nodiscard]] const Dialect& getStandardC();

[[nodiscard]] const ScalarType* findType(const Dialect& dialect, std::string_view name);

[[nodiscard]] const Builtin* findBuiltin(const Dialect& dialect, std::string_view name);

} // namespace cparser

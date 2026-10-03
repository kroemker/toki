#include "Dialect.h"

#include "cparser/ScalarType.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace cparser {

namespace {

constexpr std::array StandardTypes = {
    ScalarType{.name = "char", .size = 1},
    ScalarType{.name = "short", .size = 2},
    ScalarType{.name = "int", .size = 4},
    ScalarType{.name = "long", .size = 4},
    ScalarType{.name = "signed", .size = 4},
    ScalarType{.name = "unsigned", .size = 4, .isSigned = false},
    ScalarType{.name = "float", .size = 4, .isReal = true},
    ScalarType{.name = "double", .size = 8, .isReal = true},
    ScalarType{.name = "void", .size = 0},
};

constexpr Dialect Standard{.types = StandardTypes, .builtins = {}};

} // namespace

const Dialect& getStandardC() {
    return Standard;
}

const ScalarType* findType(const Dialect& dialect, std::string_view name) {
    const auto found = std::ranges::find(dialect.types, name, &ScalarType::name);
    return found == dialect.types.end() ? nullptr : &*found;
}

const Builtin* findBuiltin(const Dialect& dialect, std::string_view name) {
    const auto found = std::ranges::find(dialect.builtins, name, &Builtin::name);
    return found == dialect.builtins.end() ? nullptr : &*found;
}

} // namespace cparser

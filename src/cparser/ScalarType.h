#pragma once

#include "cparser/Types.h"

#include <string_view>

namespace cparser {

/// A type name a cast may name, and what converting to it does: an integer
/// type wraps to its size and signedness, a real one rounds to its precision.
/// A size of 0 converts nothing, as `void` and anything wider than 64 bits.
struct ScalarType {
    std::string_view name;
    u8 size = 4;
    bool isSigned = true;
    bool isReal = false;
};

} // namespace cparser

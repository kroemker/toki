#pragma once

#include "z64/Types.h"

#include <string>

namespace z64 {

/// A 32-bit big-endian word that holds an address once data is laid out. The
/// word already holds the constant added to the address, the way a MIPS object
/// file keeps the addend in place for `R_MIPS_32`, so patching it adds the
/// symbol's address to what is there.
struct Relocation {
    usize offset = 0;
    std::string symbol;
};

} // namespace z64

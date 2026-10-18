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
    /// Once laid out, the address patched in, which is zero for a symbol that
    /// resolved to nothing: the word less this is the constant added to it.
    u32 address = 0;

    [[nodiscard]] bool operator==(const Relocation&) const = default;
};

} // namespace z64

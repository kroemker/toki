#pragma once

#include "z64/Types.h"

#include <span>
#include <string_view>

namespace z64 {

/// Data in the game's code that assets point at. Toki64 reads no code, so the
/// little of it that assets need is written down here, contents included:
/// `gIdentityMtx`, which display lists load with `gsSPMatrix`.
struct CodeSymbol {
    std::string_view name;
    usize elementSize = 1;
    std::span<const u8> bytes;
};

/// Every code symbol assets are known to use. A memory image places them in
/// segment 0, which the game reads physical addresses through.
[[nodiscard]] std::span<const CodeSymbol> getCodeSymbols();

} // namespace z64

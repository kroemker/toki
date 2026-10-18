#pragma once

#include "z64/MemoryImage.h"
#include "z64/Types.h"

#include <string>
#include <vector>

namespace z64 {

/// How one symbol of an image differs from the same symbol of another.
struct ImageDifference {
    std::string symbol;
    std::string message;
};

/// How the symbols `actual` laid out in `segment` differ from those `expected`
/// did, symbol by symbol: a symbol one has and the other lacks, another type, or
/// other bytes before patching, or relocations at other offsets or to other
/// symbols, the constant added to each being in the bytes. Where a symbol sits
/// is not compared, so two images of the same symbols laid out in another order
/// or split into other files do not differ. Empty when nothing does.
[[nodiscard]] std::vector<ImageDifference> compareSymbols(const MemoryImage& expected,
                                                          const MemoryImage& actual, u8 segment);

} // namespace z64

#pragma once

#include "cparser/Dialect.h"

namespace z64 {

/// The C the decomp is written in, for constant expressions. Plain C's types
/// at the widths the N64 build compiles them with (`char` unsigned, as
/// `-funsigned-char` makes it, and `long` 32 bits), the decomp's own width
/// typedefs (`u8` through `f64`), and two builtins whose real definitions are
/// written with `sizeof`: `ARRAY_COUNT`, answered from declared array lengths,
/// and `TEX_LEN`, the element count of a texture of the given size and depth.
/// The gbi's aggregates (`Gfx`, `Vtx`, `Mtx`, `Vp`, `Light`, `uSprite`) have
/// their N64 sizes, since the gbi's macros take `sizeof` of them.
[[nodiscard]] const cparser::Dialect& getDecompDialect();

} // namespace z64

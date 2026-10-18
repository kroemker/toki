#pragma once

#include "z64/DeclarationWriter.h"

namespace z64 {

class GbiNames;

/// Writes a `Vtx` as fast64 does: `{{ {x, y, z}, flag, {s, t}, {r, g, b, a} }}`,
/// its colour or normal in hex.
[[nodiscard]] ElementWriter makeVertexWriter();

/// Writes a `Gfx` as the one `gs*` macro of its command, its values named as
/// `names` names them and its address as the symbol its relocation names.
/// A command that is part of a longer one, such as the halves of a texture
/// rectangle, reads back as no macro alone and so is written as its words.
/// `names` must outlive the writer.
[[nodiscard]] ElementWriter makeDisplayListWriter(const GbiNames& names);

} // namespace z64

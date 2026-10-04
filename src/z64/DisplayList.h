#pragma once

#include "base/Diagnostic.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <string>
#include <vector>

namespace z64 {

/// One element of a `Gfx` initializer, such as `gsSPVertex(...)`, and the
/// 64-bit commands it became. `gsDPLoadTextureBlock` alone is seven.
struct GbiCommand {
    /// The macro the element names, or nothing for an element written as words.
    std::string macro;
    usize firstCommand = 0;
    usize commandCount = 0;
    base::SourceLocation location;
};

/// A display list as the N64 reads it: big-endian words, eight bytes per
/// command, which element of the source each command came from, and which
/// words hold an address.
struct DisplayList {
    std::vector<u8> bytes;
    std::vector<GbiCommand> commands;
    std::vector<Relocation> relocations;
    std::vector<base::Diagnostic> diagnostics;
};

} // namespace z64

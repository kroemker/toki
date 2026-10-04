#pragma once

#include "base/Diagnostic.h"
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
/// command, and which element of the source each command came from.
struct DisplayList {
    std::vector<u8> bytes;
    std::vector<GbiCommand> commands;
    std::vector<base::Diagnostic> diagnostics;
};

} // namespace z64

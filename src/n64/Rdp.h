#pragma once

#include "base/Diagnostic.h"
#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/Memory.h"
#include "n64/OtherMode.h"
#include "n64/RspResult.h"
#include "n64/TextureMemory.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <array>
#include <variant>
#include <vector>

namespace n64 {

/// `G_SETSCISSOR`, in 10.2 fixed point.
struct Scissor {
    u16 left = 0;
    u16 top = 0;
    u16 right = 0;
    u16 bottom = 0;

    friend bool operator==(const Scissor&, const Scissor&) = default;
};

/// Everything the RDP holds that decides how a primitive is drawn, as it
/// stood when the primitive arrived.
struct RdpState {
    OtherMode otherMode;
    CombineMode combineMode;
    Color primitive{};
    Color environment{};
    Color blend{};
    Color fog{};
    u32 fill = 0;
    u8 primitiveLodFraction = 0;
    u8 primitiveMinLevel = 0;
    u16 primitiveDepth = 0;
    u16 primitiveDeltaDepth = 0;
    std::array<u8, 3> keyCenter{};
    std::array<u8, 3> keyScale{};
    std::array<u16, 3> keyWidth{};
    /// The six 9-bit `G_SETCONVERT` constants.
    std::array<u16, 6> convert{};
    Scissor scissor;
    /// Physical addresses of `G_SETCIMG` and `G_SETZIMG`. A fill rectangle drawn
    /// while both are the same address clears the depth buffer.
    u32 colorImage = 0;
    u32 depthImage = 0;
    /// The colour image's pixel size. Fill colours and copy-mode alpha depend on
    /// whether pixels are 16 or 32 bits.
    TexelSize colorImageSize = TexelSize::Bits16;
    std::array<TileDescriptor, TileCount> tiles{};
    /// Index into `RdpResult::textureMemories`.
    usize textureMemory = 0;

    friend bool operator==(const RdpState&, const RdpState&) = default;
};

/// `G_FILLRECT`, in 10.2 fixed point.
struct FillRectangle {
    u16 left = 0;
    u16 top = 0;
    u16 right = 0;
    u16 bottom = 0;
    u32 address = 0;
};

using RdpPrimitive = std::variant<Triangle, TextureRectangle, FillRectangle>;

struct Draw {
    RdpPrimitive primitive;
    /// Index into `RdpResult::states`.
    usize state = 0;
};

using TextureMemoryImage = std::array<u8, TextureMemorySize>;

/// What a task draws: each primitive with the state it is drawn under.
/// Consecutive primitives drawn under the same state share it, and texture
/// memory is copied only when a load has changed it, so both lists stay as
/// short as the task's state changes.
struct RdpResult {
    std::vector<RdpState> states;
    std::vector<TextureMemoryImage> textureMemories;
    std::vector<Draw> draws;
    std::vector<base::Diagnostic> diagnostics;
};

/// Feeds what the RSP produced through the RDP's state in order, reading
/// texture loads from `memory`.
[[nodiscard]] RdpResult runRdp(const RspResult& rspResult, const Memory& memory);

} // namespace n64

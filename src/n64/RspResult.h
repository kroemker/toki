#pragma once

#include "base/Diagnostic.h"
#include "n64/Types.h"
#include "n64/Viewport.h"

#include <array>
#include <variant>
#include <vector>

namespace n64 {

/// A vertex as the RSP hands it to the RDP: transformed, lit, fogged and with
/// texture coordinates scaled.
struct ShadedVertex {
    /// Clip space, in the N64's convention: visible where `-w <= x, y, z <= w`.
    std::array<f32, 4> position{};
    /// In texels: `G_TEXTURE`'s scale is already applied.
    std::array<f32, 2> texture{};
    std::array<u8, 4> color{};
};

/// One triangle that survived culling. Which coefficients the RDP gets is
/// decided here, by the geometry mode and `G_TEXTURE` at the time it was drawn.
struct Triangle {
    std::array<ShadedVertex, 3> vertices;
    /// Index into `RspResult::viewports`.
    usize viewport = 0;
    bool shade = false;
    bool textured = false;
    bool depth = false;
    u8 tile = 0;
    u8 levels = 1;
    /// Physical address of the command that drew it.
    u32 address = 0;
};

/// `G_TEXRECT` or `G_TEXRECTFLIP`, as the four words the RDP receives.
struct TextureRectangle {
    std::array<u32, 4> words{};
    u32 address = 0;
};

/// Any other command the RDP receives, passed on as it is. Image addresses in
/// `G_SETTIMG`, `G_SETZIMG` and `G_SETCIMG` are already resolved to physical
/// ones, and `G_SETOTHERMODE_H`/`_L` arrive as a whole `G_RDPSETOTHERMODE`.
struct RdpCommand {
    u32 word0 = 0;
    u32 word1 = 0;
    u32 address = 0;
};

using RspOutput = std::variant<Triangle, TextureRectangle, RdpCommand>;

/// One command the RSP ran, in the order it ran them: its physical address and
/// words, how many display list calls deep it ran, what it sent the RDP
/// (`RspResult::outputs` from `firstOutput` to before `endOutput`), and how many
/// of its triangles were culled rather than sent.
struct TraceEntry {
    u32 address = 0;
    u32 word0 = 0;
    u32 word1 = 0;
    usize depth = 0;
    usize firstOutput = 0;
    usize endOutput = 0;
    usize culled = 0;
};

struct RspResult {
    std::vector<Viewport> viewports;
    std::vector<RspOutput> outputs;
    /// Every command run, when the task asked for it.
    std::vector<TraceEntry> trace;
    std::vector<base::Diagnostic> diagnostics;
};

} // namespace n64

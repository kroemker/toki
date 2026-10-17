#pragma once

#include "app/viewport/domain/RenderMode.h"
#include "n64/Rdp.h"
#include "n64/Rsp.h"

#include <glm/ext/matrix_float4x4.hpp>

namespace toki::viewport {

/// What `mode` asks of the RSP: lights that shade white, and no culling.
[[nodiscard]] n64::RspOverrides makeRspOverrides(const RenderMode& mode);

/// `rdpResult` drawn as `mode` asks. Fog, translucency, alpha cutout and the
/// filter change the other mode of every state. A shading other than N64
/// draws every triangle through a combiner that selects one input: its
/// shade, its first texture (shade where it has none), or a shade written
/// for it: the normal of its face in world space, which `viewProjection`
/// takes to the clip space the triangles are in, or its depth across the
/// frame, near white and far black. Alpha is combined as it was, so cutouts
/// keep their shape. Rectangles keep their state.
[[nodiscard]] n64::RdpResult applyRenderMode(n64::RdpResult rdpResult, const RenderMode& mode,
                                             const glm::mat4& viewProjection);

} // namespace toki::viewport

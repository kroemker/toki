#pragma once

#include "app/object/Types.h"
#include "app/object/domain/Object.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "z64/MemoryImage.h"

#include <glm/ext/vector_float3.hpp>

#include <optional>
#include <vector>

namespace toki::object {

/// What of an object is shown: one display list on its own, or a skeleton,
/// at rest or at a frame of an animation.
struct ObjectSelection {
    std::optional<u32> displayList;
    std::optional<u32> skeleton;
    std::optional<u32> animation;
    usize frame = 0;
};

/// A box around what is drawn, in model space, for framing the camera.
struct Bounds {
    glm::vec3 lowest{0.0F};
    glm::vec3 highest{0.0F};
};

struct ObjectContent {
    viewport::ViewportContent content;
    std::optional<Bounds> bounds;
    /// The frames the selected animation has, one when nothing animates.
    usize frameCount = 1;
    std::vector<base::Diagnostic> diagnostics;
};

/// The segment a pose's matrices are written to, where a flexible skeleton's
/// display lists find them, as `SkelAnime_DrawFlexOpa` points it.
inline constexpr u8 LimbMatrixSegment = 0x0D;

/// What the viewport draws of `selection`, as an actor draws its object: the
/// default draw config and `sSetupDL[SETUPDL_25]`, as `Gfx_SetupDL_25Opa` sets
/// them up, then for a skeleton every limb's display list after its matrix,
/// all matrices also laid out in segment 13, and for a display list just the
/// list. Lit by a light from above front, without fog. `knownBounds`, when
/// given, is taken as the bounds instead of walking every display list drawn
/// again, as when only the frame changes.
[[nodiscard]] ObjectContent makeObjectContent(const Object& object,
                                              const ObjectSelection& selection,
                                              const std::optional<Bounds>& knownBounds);

/// The first skeleton whose limbs an animation moves, joint for joint.
[[nodiscard]] std::optional<u32> findFittingSkeleton(const Object& object, u32 animation);

/// What an object first shows: its first animation on the first skeleton it
/// fits, or failing that its first skeleton at rest, or its first display
/// list.
[[nodiscard]] ObjectSelection makeFirstSelection(const Object& object);

/// The box around every vertex the display list at `address` loads, following
/// the lists it calls, before any matrix moves them. Nothing when it loads
/// none it can read.
[[nodiscard]] std::optional<Bounds> findDisplayListBounds(const z64::MemoryImage& image,
                                                          u32 address);

} // namespace toki::object

#pragma once

#include "app/viewport/Types.h"
#include "base/Diagnostic.h"
#include "n64/FixedMatrix.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace toki::viewport {

class OrbitCamera;

/// A directional light: where it shines from, as a unit vector scaled to
/// 127, and its colour.
struct ViewportLight {
    std::array<s8, 3> direction{};
    std::array<u8, 3> color{};
};

/// The light and fog a scene is drawn under, as the game sets them up each
/// frame from its current light settings: the ambient colour and two
/// directional lights that `Lights_BindAll` binds, the fog `Play_SetFog` sets,
/// and the far plane of the view.
struct ViewportLighting {
    std::array<u8, 3> ambient{};
    std::array<ViewportLight, 2> lights{};
    std::array<u8, 3> fogColor{};
    s16 fogNear = 1000;
    f32 zFar = 12800.0F;
};

/// A segment pointed at an address in one of the content's images, as an
/// actor points segment 8 at its eye texture.
struct SegmentPointer {
    u8 segment = 0;
    usize image = 0;
    u32 address = 0;
};

/// One display list to run, from one of the content's images.
struct ViewportDraw {
    usize image = 0;
    u32 displayList = 0;
    /// Further images whose segments are pointed at before the draw's own, as
    /// an actor's object is in segment 6 while the actor is drawn.
    std::vector<usize> alsoImages;
    /// Segments pointed at addresses after the images' segments are.
    std::vector<SegmentPointer> pointedSegments;
    /// The modelview the draw is drawn with, loaded before it and the identity
    /// again after; nothing for a draw whose own list loads its matrices.
    std::optional<glm::mat4> modelView;
    /// Whether `modelView` faces the camera, as `Matrix_ReplaceRotation` turns
    /// a billboard: its rotation is the camera's undone, and its translation
    /// and scale are kept. The camera is the frame's, so it is done as the
    /// frame is run.
    bool isBillboard = false;
};

/// What a viewport draws: memory images, such as a scene and each of its
/// rooms, and the display lists in them to run, in order. Every image is
/// mapped into memory at once, and each segment starts the frame pointed at
/// the first image that has it. Before a draw, the segments of its image are
/// pointed at it with `gSPSegment`, as the game points segment 3 at each room
/// in turn.
struct ViewportContent {
    /// Shared with whatever the content was made from, a scene that makes its
    /// content again whenever how it is shown changes; an image never changes.
    std::vector<std::shared_ptr<const z64::MemoryImage>> images;
    std::vector<ViewportDraw> draws;
    std::optional<ViewportLighting> lighting;
};

/// The size of the screen a frame is drawn for, in pixels.
struct FrameSize {
    u32 width = 320;
    u32 height = 240;
};

/// What the RSP and RDP made of one frame, ready for the GPU.
struct Frame {
    n64::RspResult rspResult;
    n64::RdpResult rdpResult;
    std::vector<base::Diagnostic> diagnostics;
};

/// The segment the frame's own display list and matrices are written to,
/// starting with the camera's billboard matrix at its address 0, where
/// `Play_Draw` points segment 1 at `billboardMtx` for display lists that turn
/// to face the camera, as `gItemDropDL` does. Content that uses it cannot be
/// drawn.
inline constexpr u8 FrameSegment = 0x01;

/// `model` turned to face a camera whose view is `view`, keeping its
/// translation and the scale of each of its axes.
[[nodiscard]] glm::mat4 makeBillboardMatrix(const glm::mat4& model, const glm::mat4& view);

/// A matrix as glm holds it, for column vectors, as the RSP's `Mtx` holds it,
/// for row vectors, converted the way `guMtxF2L` converts.
[[nodiscard]] n64::FixedMatrix makeFixedMatrix(const glm::mat4& matrix);

/// Runs one frame the way the game sets one up: a display list of its own
/// sets the viewport to `size`, loads the camera's view and projection as the
/// projection matrix and the identity as the modelview, as the game's `View`
/// does, then calls each of the content's display lists in turn.
[[nodiscard]] Frame runFrame(const ViewportContent& content, const OrbitCamera& camera,
                             FrameSize size);

} // namespace toki::viewport

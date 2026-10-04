#pragma once

#include "app/viewport/Types.h"
#include "base/Diagnostic.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <vector>

namespace toki::viewport {

class OrbitCamera;

/// What a viewport draws: a memory image and the display lists in it to run,
/// in order, as segmented addresses.
struct ViewportImage {
    z64::MemoryImage image;
    std::vector<u32> displayLists;
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

/// The segment the frame's own display list and matrices are written to. An
/// image that uses it cannot be drawn.
inline constexpr u8 FrameSegment = 0x01;

/// Runs one frame the way the game sets one up: a display list of its own
/// sets the viewport to `size` and loads the camera's projection and view
/// matrices, then calls each of the image's display lists in turn.
[[nodiscard]] Frame runFrame(const ViewportImage& viewportImage, const OrbitCamera& camera,
                             FrameSize size);

} // namespace toki::viewport

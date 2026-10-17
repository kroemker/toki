#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/RspResult.h"

#include <string>
#include <string_view>
#include <vector>

namespace toki::viewport {

/// How many bytes of memory one of the content's images takes, and whether it
/// fit, or was left out of the frame.
struct ImageUse {
    std::string name;
    ImageKind kind = ImageKind::Generated;
    usize size = 0;
    bool isMapped = true;
};

/// What one frame cost and held, for showing under the viewport.
struct FrameStatistics {
    n64::RspStatistics rsp;
    usize textureLoads = 0;
    /// The bytes below the images, where the game code's data is mapped.
    usize codeRegion = 0;
    usize frameListSize = 0;
    /// The bytes the frame filled, from address 0 to the end of its display
    /// list; nothing when the frame was not run.
    usize used = 0;
    std::vector<ImageUse> images;
    FrameTimings timings;
};

/// What `frame`, run from `content`, cost and held.
[[nodiscard]] FrameStatistics makeFrameStatistics(const Frame& frame,
                                                  const ViewportContent& content);

/// What images of `kind` are called, as a group.
[[nodiscard]] std::string_view getImageKindName(ImageKind kind);

} // namespace toki::viewport

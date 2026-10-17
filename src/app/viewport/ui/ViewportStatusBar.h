#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/domain/FrameStatistics.h"

#include <optional>
#include <vector>

namespace toki::viewport {

/// What one rendered frame cost and held: what running it reported, how many
/// draw calls the GPU was given, and how long building and submitting them
/// took on the CPU, in milliseconds.
struct FrameReport {
    FrameStatistics statistics;
    usize drawCalls = 0;
    f64 render = 0.0;
};

/// The line under the viewport's image: the geometry the last frame drew, the
/// memory its images filled, and what it cost, each opening its breakdown
/// above it.
class ViewportStatusBar {
public:
    void addFrame(FrameReport report);

    void draw() const;

    /// How tall the line is, to leave room for it under the image.
    [[nodiscard]] static float getHeight();

private:
    std::optional<FrameReport> lastFrame;
    /// The CPU time of the frames rendered last, oldest first.
    std::vector<float> history;
};

} // namespace toki::viewport

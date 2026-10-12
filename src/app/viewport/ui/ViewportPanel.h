#pragma once

#include "app/core/domain/Settings.h"
#include "app/core/ui/Panel.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "gfx/Device.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"

#include <glm/vec2.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::viewport {

/// A frame the viewport drew with its commands recorded, the content it drew,
/// which the frame's addresses point into, and the size it drew it at. The
/// generation tells one frame from the next.
struct TracedFrame {
    Frame frame;
    ViewportContent content;
    FrameSize size;
    u64 generation = 0;
};

class ViewportPanel : public core::Panel {
public:
    explicit ViewportPanel(const gfx::Device& device);

    [[nodiscard]] ImGuiWindowFlags getWindowFlags() const override {
        return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }

    /// Replaces what is drawn. When `target` is given, the camera turns to
    /// look at it.
    void setContent(ViewportContent content, std::optional<glm::vec3> target = std::nullopt);

    /// Replaces what is drawn with the next frame of the same content, as an
    /// animation plays: what the frame before reported is not reported again.
    void setNextFrame(ViewportContent content) {
        viewportContent = std::move(content);
        stale = true;
    }

    [[nodiscard]] core::CameraPose getCameraPose() const;

    void setCameraPose(const core::CameraPose& pose);

    /// Turns the camera to look at `point`, coming close enough to make out
    /// what stands there.
    void focus(const glm::vec3& point) {
        constexpr f32 CloseUp = 400.0F;
        orbitCamera.setTarget(point);
        orbitCamera.setDistance(std::min(orbitCamera.getDistance(), CloseUp));
        stale = true;
    }

    /// Looks at a sphere around `center`, from far enough to see all of it,
    /// from where the camera turns now.
    void frameView(const glm::vec3& center, f32 radius) {
        constexpr f32 DistancePerRadius = 2.6F;
        orbitCamera.setTarget(center);
        orbitCamera.setDistance(std::max(radius, 1.0F) * DistancePerRadius);
        stale = true;
    }

    /// Replaces what is drawn over the image, on top of everything.
    void setOverlay(ViewportOverlay overlay) {
        viewportOverlay = std::move(overlay);
    }

    /// Triangles to pick out over the image, in pixels of a frame of `size`,
    /// as the display list viewer shows what a command drew.
    void setHighlight(std::vector<std::array<glm::vec2, 3>> triangles, const FrameSize size) {
        highlight = std::move(triangles);
        highlightSize = size;
    }

    /// Whether frames are drawn with their commands recorded, for looking
    /// into them. While they are, the last one is kept.
    void setTracing(const bool isOn) {
        if (isOn != isTracing) {
            isTracing = isOn;
            tracedFrame.reset();
            stale = true;
        }
    }

    /// The last frame drawn while tracing, if one was.
    [[nodiscard]] const TracedFrame* findTracedFrame() const {
        return tracedFrame ? &*tracedFrame : nullptr;
    }

    /// Where on the screen the viewport's image was last drawn, its top left.
    [[nodiscard]] ImVec2 getImageOrigin() const {
        return imageOrigin;
    }

    /// The scene being read for the panel, shown over what is drawn until it
    /// arrives.
    void setReadingName(std::optional<std::string> name) {
        readingName = std::move(name);
    }

    void draw() override;

private:
    [[nodiscard]] bool handleInput(ImVec2 size);
    void render(const gfx::RenderTarget& target);
    static void reportDiagnostics(const Frame& frame);
    void drawOverlay(ImDrawList& drawList, ImVec2 origin, ImVec2 size) const;
    void drawHighlight(ImDrawList& drawList, ImVec2 origin, ImVec2 size) const;

    const gfx::Device& device;
    std::optional<gfx::Renderer> renderer;
    std::optional<gfx::RenderTarget> renderTarget;
    std::string failure;
    std::optional<std::string> readingName;
    OrbitCamera orbitCamera;
    ViewportContent viewportContent;
    ViewportOverlay viewportOverlay;
    std::vector<std::array<glm::vec2, 3>> highlight;
    FrameSize highlightSize;
    bool isTracing = false;
    std::optional<TracedFrame> tracedFrame;
    u64 frameGeneration = 0;
    ImVec2 imageOrigin{};
    bool stale = true;
    bool reported = false;
    usize diagnosticCount = 0;
};

} // namespace toki::viewport

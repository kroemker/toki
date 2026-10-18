#pragma once

#include "app/core/domain/Settings.h"
#include "app/core/ui/Panel.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportView.h"
#include "app/viewport/ui/ViewportStatusBar.h"
#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/Overlay.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/vec2.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <functional>
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

/// What the RSP and RDP made of a frame, after the render mode changed it,
/// and the generation it was traced as, zero when it was not.
struct DrawnFrame {
    n64::RspResult rspResult;
    n64::RdpResult rdpResult;
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

    /// Replaces what is drawn over the image, hidden by what stands in front
    /// of it. The frame is drawn again, not run again.
    void setOverlay(ViewportOverlay overlay) {
        viewportOverlay = std::move(overlay);
        isOverlayStale = true;
    }

    [[nodiscard]] const ViewportView& getView() const {
        return viewportView;
    }

    /// Draws the frame as `view` says: run again when its render mode
    /// changes what the RSP or RDP produce, drawn again otherwise.
    void setView(const ViewportView& view);

    /// Offers `choices` in the bar, for the content's owner to apply through
    /// `choose` whenever one is changed there.
    void setChoices(ViewportChoices choices, std::function<void(const ViewportChoices&)> choose) {
        viewportChoices = std::move(choices);
        chooseHandler = std::move(choose);
    }

    /// Takes back what `setChoices` offered, when the content's owner gives
    /// up the viewport.
    void clearChoices() {
        viewportChoices.reset();
        chooseHandler = nullptr;
    }

    /// Triangles to pick out over the image, in the clip space of the frame's
    /// viewport, as the display list viewer shows what a command drew in the
    /// traced frame of `generation`. They are drawn in the overlay pass, faintly
    /// where the frame hides them, and only over that frame: over any other,
    /// the camera or the content they were found in may have moved.
    void setHighlight(std::vector<ClipTriangle> triangles, const u64 generation) {
        highlight = std::move(triangles);
        highlightGeneration = generation;
        isOverlayStale = true;
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

    /// The points a click in the viewport picks, and what picking the one at
    /// an index does.
    void setPickables(std::vector<glm::vec3> points, std::function<void(usize)> pick) {
        pickables = std::move(points);
        pickHandler = std::move(pick);
    }

    /// A point a gizmo moves, and what is done with where it is moved to:
    /// `isDone` once it is let go. Nothing hides the gizmo.
    void setHandle(std::optional<glm::vec3> position,
                   std::function<void(const glm::vec3&, bool isDone)> move) {
        if (!isHandleHeld) {
            handle = position;
        }
        moveHandler = std::move(move);
    }

    /// The scene being read for the panel, shown over what is drawn until it
    /// arrives.
    void setReadingName(std::optional<std::string> name) {
        readingName = std::move(name);
    }

    void draw() override;

private:
    void drawBar();
    void drawViewChanges(ImDrawList& drawList, ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] bool handleInput(ImVec2 size);
    void drawHandle(ImDrawList& drawList, ImVec2 origin, ImVec2 size);
    void pick(ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] glm::mat4 makeProjection(ImVec2 size) const;
    void render(const gfx::RenderTarget& target);
    [[nodiscard]] std::vector<base::Diagnostic> drawFrame(const gfx::RenderTarget& target);
    [[nodiscard]] gfx::Overlay makeOverlay(const DrawnFrame& drawing, FrameSize size) const;
    [[nodiscard]] gfx::Overlay makeHighlight(f32 scale) const;
    static void reportDiagnostics(const Frame& frame);

    const gfx::Device& device;
    std::optional<gfx::Renderer> renderer;
    std::optional<gfx::RenderTarget> renderTarget;
    std::string failure;
    std::optional<std::string> readingName;
    OrbitCamera orbitCamera;
    ViewportContent viewportContent;
    ViewportOverlay viewportOverlay;
    ViewportView viewportView;
    std::optional<ViewportChoices> viewportChoices;
    std::function<void(const ViewportChoices&)> chooseHandler;
    ViewportStatusBar statusBar;
    std::vector<ClipTriangle> highlight;
    u64 highlightGeneration = 0;
    bool isTracing = false;
    std::optional<TracedFrame> tracedFrame;
    /// What the RSP and RDP made of the last frame run, as it is drawn, to
    /// draw again when only what is over it changes.
    std::optional<DrawnFrame> drawnFrame;
    u64 frameGeneration = 0;
    ImVec2 imageOrigin{};
    std::vector<glm::vec3> pickables;
    std::function<void(usize)> pickHandler;
    std::optional<glm::vec3> handle;
    std::function<void(const glm::vec3&, bool)> moveHandler;
    bool isHandleHeld = false;
    bool stale = true;
    bool isOverlayStale = false;
    bool reported = false;
    usize diagnosticCount = 0;
};

} // namespace toki::viewport

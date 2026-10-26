#pragma once

#include "app/core/domain/Settings.h"
#include "app/core/ui/Panel.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ScreenPick.h"
#include "app/viewport/domain/SelectionShape.h"
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "app/viewport/domain/ViewportTools.h"
#include "app/viewport/domain/ViewportView.h"
#include "app/viewport/ui/ViewportHandles.h"
#include "app/viewport/ui/ViewportStatusBar.h"
#include "app/viewport/ui/ViewportTransform.h"
#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/Overlay.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/vec2.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <span>
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
    /// What each of the content's draws made, and what owns it.
    std::vector<std::optional<OutputRange>> drawOutputs;
    std::vector<std::optional<usize>> drawOwners;
    /// `drawOwners`, without the owners of draws that are not boxed.
    std::vector<std::optional<usize>> boxedOwners;
    /// The draws whose triangles have their edges drawn.
    std::vector<usize> edgedDraws;
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
        if (overlay != viewportOverlay) {
            viewportOverlay = std::move(overlay);
            isOverlayStale = true;
        }
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

    /// What a click or a box in the viewport hit: the indices of the points
    /// and segments it picked, and the boxed points a box held; for a click
    /// that picked none, the owner of the draw that drew what is under it, if
    /// anything is and one owns it, and the ray from the camera through it.
    struct ViewportPick {
        std::span<const usize> points;
        std::span<const usize> segments;
        std::span<const usize> boxedPoints;
        std::optional<usize> owner;
        std::optional<PickRay> ray;
    };

    /// What a click or a box can pick: points, near which a click picks them;
    /// segments, near which a click picks them and which a box picks when it
    /// holds both their ends; and points only a box picks, as the middles of
    /// faces, which a click picks by its ray instead.
    struct Pickables {
        std::vector<glm::vec3> points;
        std::vector<std::array<glm::vec3, 2>> segments;
        std::vector<glm::vec3> boxedPoints;
    };

    /// The points a click or a box in the viewport picks, and what picking
    /// does, as the pick changes the selection.
    using PickHandler = std::function<void(const ViewportPick&, PickMode)>;

    void setPickables(Pickables picked, PickHandler pick) {
        pickables = std::move(picked);
        pickHandler = std::move(pick);
    }

    /// What clicks in the viewport select: nodes, or faces, edges or vertices.
    [[nodiscard]] SelectionMode getSelectionMode() const {
        return viewportTools.mode;
    }

    /// The owners of the draws selected, shown as the overlays say: what
    /// their draws drew tinted and outlined, and boxed.
    void setSelectedOwners(std::vector<usize> owners) {
        if (owners != selectedOwners) {
            selectedOwners = std::move(owners);
            isOverlayStale = true;
            selectionBoxes.reset();
        }
    }

    /// What is selected, ringed over the image, and what is done with it once
    /// a transform of it is confirmed.
    void setTransformTarget(std::optional<TransformTarget> target,
                            ViewportTransform::Handler handler,
                            ViewportTransform::PreviewHandler preview = nullptr) {
        viewportTransform.setTarget(std::move(target), std::move(handler), std::move(preview));
    }

    /// Handles of what is selected that the mouse drags in node mode, and
    /// what is done with a drag once it is let go.
    void setHandles(std::vector<ViewportHandle> handles, ViewportHandles::Handler handler,
                    ViewportHandles::PreviewHandler preview = nullptr) {
        viewportHandles.setHandles(std::move(handles), std::move(handler), std::move(preview));
    }

    /// The scene being read for the panel, shown over what is drawn until it
    /// arrives.
    void setReadingName(std::optional<std::string> name) {
        readingName = std::move(name);
    }

    void draw() override;

private:
    void drawBar();
    /// Whether the content offers faces, edges or vertices to pick.
    [[nodiscard]] bool canEdit() const;
    /// Takes `chosen` as the bar's choices and hands it to their owner.
    void choose(const ViewportChoices& chosen);
    void drawViewChanges(ImDrawList& drawList, ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] bool handleInput(ImVec2 size);
    [[nodiscard]] TransformView makeTransformView(ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] bool isTransforming() const;
    [[nodiscard]] std::vector<glm::vec3> listGroundTriangles() const;
    [[nodiscard]] gfx::Overlay makeSelectionHighlight(const DrawnFrame& drawing, f32 scale) const;
    void drawSelectionBoxes(ImDrawList& drawList, const TransformView& view);
    void pick(ImVec2 origin, ImVec2 size) const;
    void pickInBox(ImVec2 origin, ImVec2 size) const;
    [[nodiscard]] PickMode findPickMode() const;
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
    /// Where the last frame run mapped each of the content's images.
    std::vector<std::optional<z64::MappedImage>> mappedImages;
    u64 frameGeneration = 0;
    ImVec2 imageOrigin{};
    Pickables pickables;
    PickHandler pickHandler;
    /// Where a box being dragged to select began, on the screen.
    std::optional<ImVec2> boxStart;
    /// The view and projection the last frame was run with, to take what it
    /// drew back to the world.
    glm::mat4 frameViewProjection{1.0F};
    ViewportTransform viewportTransform;
    ViewportHandles viewportHandles;
    std::vector<usize> selectedOwners;
    /// The boxes around what is selected, found once per frame drawn.
    std::optional<std::vector<std::optional<WorldBox>>> selectionBoxes;
    /// Whether the press the mouse is in ended a transform, so its release
    /// picks nothing.
    bool isPressSwallowed = false;
    ViewportTools viewportTools;
    bool stale = true;
    bool isOverlayStale = false;
    bool reported = false;
    usize diagnosticCount = 0;
};

} // namespace toki::viewport

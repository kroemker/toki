#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/domain/TransformSession.h"
#include "app/viewport/domain/ViewportTools.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <imgui.h>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct ImDrawList;

namespace toki::viewport {

/// What is selected, as the viewport transforms it: its points, the one
/// picked last, the middle they turn about, and the axes it may turn about,
/// with why the others may not.
struct TransformTarget {
    std::vector<glm::vec3> points;
    std::optional<usize> activePoint;
    glm::vec3 pivot{0.0F};
    std::array<bool, 3> isTurnable{true, true, true};
    std::string turnNote;
    /// Why what is selected cannot be scaled; empty when it can.
    std::string scaleNote;
};

/// A transform once it is confirmed: what it came to, and where it put each
/// of the target's points, in their order.
struct TransformEdit {
    TransformResult result;
    std::vector<glm::vec3> points;

    friend bool operator==(const TransformEdit&, const TransformEdit&) = default;
};

/// The camera and the part of the screen a transform is drawn over.
struct TransformView {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::vec3 eye{0.0F};
    ImVec2 origin{};
    ImVec2 size{};
};

/// Moves, turns and scales what is selected in the viewport, by a gizmo or
/// with the mouse alone, as G, R and S begin, its axes held and snapped as the bar's
/// options say. Every way runs one `TransformSession`, so a drag, a typed
/// number, the grid and the ground mean the same in all of them.
class ViewportTransform {
public:
    using Handler = std::function<void(const TransformEdit&)>;
    /// The triangles drawn of the ground, three corners each, in the world.
    using FindGround = std::function<std::vector<glm::vec3>()>;

    /// Shows a transform as it runs, or, given nothing, that it ended,
    /// confirmed or not.
    using PreviewHandler = std::function<void(const std::optional<TransformEdit>&)>;

    void setTarget(std::optional<TransformTarget> target, Handler handler,
                   PreviewHandler preview = nullptr);

    /// Tells the preview handler what the transform running comes to, when
    /// that changed since it was last told, or that it ended.
    void updatePreview();

    /// Switches to `tool`, which lets go of the axes held for the one before.
    void setTool(EditTool tool);

    void setGroundSource(FindGround findGround) {
        findGroundTriangles = std::move(findGround);
    }

    /// Draws the options of `tool` in the viewport's bar: the axes it is held
    /// to and what it snaps to, lit while a held key turns one on. False when
    /// the tool has none.
    bool drawOptions(EditTool tool);

    /// Begins a transform with the mouse, or holds one to an axis, as the
    /// keys pressed this frame say.
    void applyHotkeys(EditTool tool, const TransformView& view);

    /// Runs the transform the mouse makes, once a key began it: follows the
    /// mouse, takes typed numbers, and is confirmed or cancelled. True while
    /// it runs, when the viewport's own clicks and keys are left alone.
    bool updateModal(const TransformView& view);

    /// Whether the mouse is over the image itself, not the bar or the tool
    /// strip over it: where a click confirms or cancels a transform.
    [[nodiscard]] static bool isOverImage(const TransformView& view);

    [[nodiscard]] bool isModal() const {
        return isModalRunning;
    }

    /// Whether the gizmo has the mouse, so the viewport's button gives way.
    [[nodiscard]] bool isGizmoActive(EditTool tool) const;

    void drawGizmo(ImDrawList& drawList, const TransformView& view, EditTool tool);

    /// Draws the axes the transform is held to as lines through the target's
    /// middle, as Blender does, and says what it does over the image.
    void drawOverlay(ImDrawList& drawList, const TransformView& view, EditTool tool) const;

private:
    [[nodiscard]] Snapping findSnapping() const;
    [[nodiscard]] const AxisConstraint& findConstraint() const;
    [[nodiscard]] bool isTurnable(const AxisConstraint& constraint) const;
    [[nodiscard]] bool isScalable() const;
    [[nodiscard]] std::vector<glm::vec3> makePoints() const;
    void begin(TransformKind kind);
    void confirm();
    void cancel();
    void applyConstraintKeys();
    void applyTypedKeys();
    void drawAxisOptions(TransformKind kind);
    void drawAxisLines(ImDrawList& drawList, const TransformView& view, EditTool tool) const;

    std::optional<TransformTarget> transformTarget;
    Handler transformHandler;
    PreviewHandler previewHandler;
    std::optional<TransformEdit> previewed;
    FindGround findGroundTriangles;
    AxisConstraint axisConstraint;
    /// The axis a gizmo's ring turns about, while one is dragged.
    std::optional<AxisConstraint> gizmoConstraint;
    Snapping snapping;
    std::optional<TransformSession> session;
    bool isModalRunning = false;
    EditTool shownTool = EditTool::Select;
    /// The axes the bar held before G or R began a transform of their own,
    /// given back once it ends.
    AxisConstraint toolConstraint;
    bool isGizmoHeld = false;
    glm::mat4 gizmoMatrix{1.0F};
    ImVec2 modalStart{};
    ImVec2 lastMouse{};
    f32 turnedDegrees = 0.0F;
    mutable std::optional<std::vector<glm::vec3>> groundTriangles;
};

} // namespace toki::viewport

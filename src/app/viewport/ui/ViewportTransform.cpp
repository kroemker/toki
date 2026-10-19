#include "ViewportTransform.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/viewport/domain/ScreenPick.h"

#include <ImGuizmo.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>

#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr std::array<const char*, 3> AxisLabels{"X", "Y", "Z"};
constexpr std::array<ImVec4, 3> AxisColors{ImVec4{0.93F, 0.33F, 0.36F, 0.9F},
                                           ImVec4{0.47F, 0.80F, 0.33F, 0.9F},
                                           ImVec4{0.33F, 0.55F, 0.95F, 0.9F}};
constexpr f32 AxisLineWidth = 1.5F;
constexpr f32 RingRadius = 12.0F;
constexpr f32 RingWidth = 2.0F;
constexpr f32 NoteMargin = 8.0F;

[[nodiscard]] core::Action findConstrainAction(const usize axis) {
    return static_cast<core::Action>(static_cast<usize>(core::Action::ConstrainX) + axis);
}

[[nodiscard]] core::Action findPlaneAction(const usize axis) {
    return static_cast<core::Action>(static_cast<usize>(core::Action::ConstrainPlaneX) + axis);
}

[[nodiscard]] bool isTransformTool(const EditTool tool) {
    return tool == EditTool::Move || tool == EditTool::Rotate;
}

[[nodiscard]] glm::vec2 makeLocal(const ImVec2 point, const TransformView& view) {
    return {point.x - view.origin.x, point.y - view.origin.y};
}

bool drawToggle(const char* label, const bool isLit) {
    if (isLit) {
        ImGui::PushStyleColor(ImGuiCol_Button, core::palette::AccentMuted);
    }
    const bool isClicked = ImGui::Button(label);
    if (isLit) {
        ImGui::PopStyleColor();
    }
    return isClicked;
}

/// How far the gizmo turned this frame about each axis, in degrees, from the
/// turn it reports.
[[nodiscard]] std::array<f32, 3> findTurns(const glm::mat4& delta) {
    return {glm::degrees(std::atan2(delta[1][2], delta[1][1])),
            glm::degrees(std::atan2(delta[2][0], delta[0][0])),
            glm::degrees(std::atan2(delta[0][1], delta[0][0]))};
}

} // namespace

void ViewportTransform::setTarget(std::optional<TransformTarget> target, Handler handler) {
    transformTarget = std::move(target);
    transformHandler = std::move(handler);
    if (!transformTarget || transformTarget->points.empty()) {
        cancel();
    }
}

Snapping ViewportTransform::findSnapping() const {
    Snapping found = snapping;
    if (session) {
        const ImGuiIO& io = ImGui::GetIO();
        found.isGridOn = found.isGridOn != io.KeyCtrl;
        found.isGroundOn = found.isGroundOn != io.KeyAlt;
    }
    return found;
}

const AxisConstraint& ViewportTransform::findConstraint() const {
    return gizmoConstraint ? *gizmoConstraint : axisConstraint;
}

std::vector<glm::vec3> ViewportTransform::makePoints() const {
    if (!transformTarget) {
        return {};
    }
    if (!session) {
        return transformTarget->points;
    }
    const Snapping found = findSnapping();
    const TransformResult result = session->makeResult(findConstraint(), found);
    if (found.isGroundOn && result.kind == TransformKind::Move && findGroundTriangles) {
        if (!groundTriangles) {
            groundTriangles = findGroundTriangles();
        }
        return makeTransformedPoints(transformTarget->points, transformTarget->pivot, result,
                                     *groundTriangles);
    }
    return makeTransformedPoints(transformTarget->points, transformTarget->pivot, result);
}

void ViewportTransform::begin(const TransformKind kind) {
    session.emplace(kind);
    groundTriangles.reset();
    turnedDegrees = 0.0F;
}

void ViewportTransform::confirm() {
    if (session && transformHandler) {
        const TransformEdit edit{.result = session->makeResult(findConstraint(), findSnapping()),
                                 .points = makePoints()};
        const Handler handler = transformHandler;
        session.reset();
        handler(edit);
    }
    cancel();
}

void ViewportTransform::cancel() {
    session.reset();
    isModalRunning = false;
    gizmoConstraint.reset();
}

bool ViewportTransform::isTurnable(const AxisConstraint& constraint) const {
    return transformTarget && transformTarget->isTurnable[constraint.findRotationAxis()];
}

void ViewportTransform::applyConstraintKeys() {
    const AxisConstraint before = axisConstraint;
    for (usize axis = 0; axis < AxisLabels.size(); ++axis) {
        if (core::isActionPressed(findConstrainAction(axis))) {
            axisConstraint.constrainTo(axis);
        }
        if (core::isActionPressed(findPlaneAction(axis))) {
            axisConstraint.constrainToPlane(axis);
        }
    }
    if (session && session->getKind() == TransformKind::Rotate && !isTurnable(axisConstraint)) {
        axisConstraint = before;
    }
}

void ViewportTransform::applyHotkeys(const EditTool tool, const TransformView& view) {
    if (!transformTarget || transformTarget->points.empty() || isModalRunning || isGizmoHeld) {
        return;
    }
    const auto startModal = [this, &view](const TransformKind kind) {
        begin(kind);
        isModalRunning = true;
        modalStart = ImVec2(ImGui::GetIO().MousePos.x - view.origin.x,
                            ImGui::GetIO().MousePos.y - view.origin.y);
        lastMouse = modalStart;
    };
    if (core::isActionPressed(core::Action::Grab)) {
        startModal(TransformKind::Move);
    } else if (core::isActionPressed(core::Action::Turn) && isTurnable(axisConstraint)) {
        startModal(TransformKind::Rotate);
    } else if (isTransformTool(tool)) {
        applyConstraintKeys();
    }
}

bool ViewportTransform::updateModal(const TransformView& view) {
    if (!isModalRunning || !session || !transformTarget) {
        isModalRunning = false;
        return false;
    }
    applyConstraintKeys();
    ImGuiIO& io = ImGui::GetIO();
    for (const ImWchar character : io.InputQueueCharacters) {
        if (character < 0x80) {
            static_cast<void>(session->type(static_cast<char>(character)));
        }
    }
    io.InputQueueCharacters.resize(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        session->erase();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        session->moveToNextField(axisConstraint);
    }
    const ImVec2 mouse(io.MousePos.x - view.origin.x, io.MousePos.y - view.origin.y);
    const glm::mat4 viewProjection = view.projection * view.view;
    const glm::vec2 size{view.size.x, view.size.y};
    const glm::vec3& pivot = transformTarget->pivot;
    if (session->getKind() == TransformKind::Move) {
        session->setDragged(findDragOffset(viewProjection, size, {modalStart.x, modalStart.y},
                                           {mouse.x, mouse.y}, pivot, view.eye, axisConstraint));
    } else if (const std::optional<glm::vec2> center =
                   findScreenPosition(viewProjection, size, pivot)) {
        glm::vec3 axis(0.0F);
        axis[static_cast<glm::length_t>(axisConstraint.findRotationAxis())] = 1.0F;
        turnedDegrees += findDragAngle(*center, {lastMouse.x, lastMouse.y}, {mouse.x, mouse.y},
                                       axis, view.eye, pivot);
        session->setDraggedAngle(turnedDegrees);
    }
    lastMouse = mouse;
    const bool isOnImage = isOverImage(view);
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
        (isOnImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
        confirm();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
               (isOnImage && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        cancel();
    }
    return true;
}

bool ViewportTransform::isOverImage(const TransformView& view) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return ImGui::IsWindowHovered() && mouse.x >= view.origin.x && mouse.y >= view.origin.y &&
           mouse.x < view.origin.x + view.size.x && mouse.y < view.origin.y + view.size.y;
}

bool ViewportTransform::isGizmoActive(const EditTool tool) const {
    return transformTarget && !transformTarget->points.empty() && !isModalRunning &&
           isTransformTool(tool) && (isGizmoHeld || ImGuizmo::IsOver());
}

void ViewportTransform::drawGizmo(ImDrawList& drawList, const TransformView& view,
                                  const EditTool tool) {
    if (!transformTarget || transformTarget->points.empty() || isModalRunning ||
        !isTransformTool(tool)) {
        isGizmoHeld = false;
        return;
    }
    const bool isMove = tool == EditTool::Move;
    unsigned operation = 0;
    for (usize axis = 0; axis < AxisLabels.size(); ++axis) {
        if (isMove && (axisConstraint.isFree() || axisConstraint.axes[axis])) {
            operation |= ImGuizmo::TRANSLATE_X << axis;
        }
        const bool isHeld = axisConstraint.isFree() || axisConstraint.findRotationAxis() == axis;
        if (!isMove && isHeld && transformTarget->isTurnable[axis]) {
            operation |= ImGuizmo::ROTATE_X << axis;
        }
    }
    if (operation == 0) {
        return;
    }
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(&drawList);
    ImGuizmo::SetRect(view.origin.x, view.origin.y, view.size.x, view.size.y);
    if (!isGizmoHeld) {
        gizmoMatrix = glm::translate(glm::mat4(1.0F), transformTarget->pivot);
    }
    glm::mat4 delta(1.0F);
    if (ImGuizmo::Manipulate(glm::value_ptr(view.view), glm::value_ptr(view.projection),
                             static_cast<ImGuizmo::OPERATION>(operation), ImGuizmo::WORLD,
                             glm::value_ptr(gizmoMatrix), glm::value_ptr(delta))) {
        if (isMove) {
            if (!session) {
                begin(TransformKind::Move);
            }
            session->setDragged(glm::vec3(gizmoMatrix[3]) - transformTarget->pivot);
        } else {
            const std::array<f32, 3> turns = findTurns(delta);
            if (!session) {
                usize turned = 0;
                for (usize axis = 1; axis < turns.size(); ++axis) {
                    if (std::abs(turns[axis]) > std::abs(turns[turned])) {
                        turned = axis;
                    }
                }
                AxisConstraint held;
                held.constrainTo(turned);
                gizmoConstraint = held;
                begin(TransformKind::Rotate);
            }
            turnedDegrees += turns[gizmoConstraint->findRotationAxis()];
            session->setDraggedAngle(turnedDegrees);
        }
    }
    if (ImGuizmo::IsUsing()) {
        isGizmoHeld = true;
    } else if (isGizmoHeld) {
        isGizmoHeld = false;
        confirm();
    }
}

void ViewportTransform::drawAxisLines(ImDrawList& drawList, const TransformView& view,
                                      const EditTool tool) const {
    const bool isRotating =
        session ? session->getKind() == TransformKind::Rotate : tool == EditTool::Rotate;
    const AxisConstraint& constraint = findConstraint();
    if ((!session && !isTransformTool(tool)) || (constraint.isFree() && !isRotating)) {
        return;
    }
    const glm::mat4 viewProjection = view.projection * view.view;
    for (usize axis = 0; axis < AxisLabels.size(); ++axis) {
        const bool isShown =
            isRotating ? constraint.findRotationAxis() == axis : constraint.axes[axis];
        if (!isShown) {
            continue;
        }
        glm::vec3 direction(0.0F);
        direction[static_cast<glm::length_t>(axis)] = 1.0F;
        if (const std::optional<std::array<glm::vec2, 2>> line = findScreenLine(
                viewProjection, {view.size.x, view.size.y}, transformTarget->pivot, direction)) {
            drawList.AddLine(ImVec2(view.origin.x + (*line)[0].x, view.origin.y + (*line)[0].y),
                             ImVec2(view.origin.x + (*line)[1].x, view.origin.y + (*line)[1].y),
                             ImGui::GetColorU32(AxisColors[axis]), AxisLineWidth);
        }
    }
}

void ViewportTransform::drawOverlay(ImDrawList& drawList, const TransformView& view,
                                    const EditTool tool) const {
    if (!transformTarget || transformTarget->points.empty()) {
        return;
    }
    drawAxisLines(drawList, view, tool);
    const glm::mat4 viewProjection = view.projection * view.view;
    const std::vector<glm::vec3> points = makePoints();
    for (usize i = 0; i < points.size(); ++i) {
        const std::optional<glm::vec2> position =
            findScreenPosition(viewProjection, {view.size.x, view.size.y}, points[i]);
        if (!position) {
            continue;
        }
        const ImVec4 color =
            i == transformTarget->activePoint ? core::palette::Accent : core::palette::AccentMuted;
        drawList.AddCircle(ImVec2(view.origin.x + position->x, view.origin.y + position->y),
                           RingRadius, ImGui::GetColorU32(color), 0, RingWidth);
    }
    if (!session) {
        return;
    }
    const f32 line = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 position(view.origin.x + NoteMargin, view.origin.y + view.size.y - NoteMargin - line);
    if (isModalRunning) {
        const std::string hint = std::format(
            "Enter or click confirms · Esc or right-click cancels · {} {} {} hold to an axis, "
            "{} to the other two · type a number, Tab for the next · hold Ctrl to switch the "
            "grid{}",
            core::getShortcutLabel(core::Action::ConstrainX),
            core::getShortcutLabel(core::Action::ConstrainY),
            core::getShortcutLabel(core::Action::ConstrainZ),
            core::getShortcutLabel(core::Action::ConstrainPlaneX),
            session->getKind() == TransformKind::Move ? ", Alt the ground" : "");
        drawList.AddText(position, ImGui::GetColorU32(core::palette::TextMuted), hint.c_str());
        position.y -= line;
    }
    const std::string text = session->describe(findConstraint(), findSnapping());
    drawList.AddText(position, ImGui::GetColorU32(core::palette::Text), text.c_str());
}

bool ViewportTransform::drawOptions(const EditTool tool) {
    if (!isTransformTool(tool) && !session) {
        return false;
    }
    const TransformKind kind = session                  ? session->getKind()
                               : tool == EditTool::Move ? TransformKind::Move
                                                        : TransformKind::Rotate;
    drawAxisOptions(kind);
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * 2.0F);
    const Snapping shown = findSnapping();
    if (drawToggle(core::icons::GridFour, shown.isGridOn)) {
        snapping.isGridOn = !snapping.isGridOn;
    }
    ImGui::SetItemTooltip(
        "Snap to the grid: %g units, %g° (hold Ctrl while transforming to switch it)",
        snapping.gridStep, snapping.angleStep);
    if (kind == TransformKind::Move) {
        ImGui::SameLine(0.0F, 1.0F);
        if (drawToggle(core::icons::ArrowLineDown, shown.isGroundOn)) {
            snapping.isGroundOn = !snapping.isGroundOn;
        }
        ImGui::SetItemTooltip(
            "Set each point down on the ground below it (hold Alt while "
            "moving to switch it). The ground is what the frame drew on screen, until "
            "collision is read.");
    }
    return true;
}

void ViewportTransform::drawAxisOptions(const TransformKind kind) {
    for (usize axis = 0; axis < AxisLabels.size(); ++axis) {
        if (axis > 0) {
            ImGui::SameLine(0.0F, 1.0F);
        }
        ImGui::PushID(static_cast<int>(axis));
        const bool isBlocked =
            kind == TransformKind::Rotate && transformTarget && !transformTarget->isTurnable[axis];
        const bool isLit = kind == TransformKind::Rotate
                               ? findConstraint().findRotationAxis() == axis
                               : findConstraint().axes[axis];
        ImGui::BeginDisabled(isBlocked);
        if (drawToggle(AxisLabels[axis], isLit)) {
            if (kind == TransformKind::Rotate) {
                axisConstraint.constrainTo(axis);
            } else {
                axisConstraint.axes[axis] = !axisConstraint.axes[axis];
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                                 ImGuiHoveredFlags_ForTooltip)) {
            const std::string name = axis == 1 ? "Y (up)" : AxisLabels[axis];
            const std::string held =
                kind == TransformKind::Rotate
                    ? core::makeShortcutText(std::format("Turn about {}", name),
                                             findConstrainAction(axis))
                    : std::format("{}; {} holds to the other two",
                                  core::makeShortcutText(std::format("Hold to {}", name),
                                                         findConstrainAction(axis)),
                                  core::getShortcutLabel(findPlaneAction(axis)));
            ImGui::SetTooltip(
                "%s", isBlocked ? std::format("{}: {}", held, transformTarget->turnNote).c_str()
                                : held.c_str());
        }
        ImGui::PopID();
    }
}

} // namespace toki::viewport

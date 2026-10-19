#include "ViewportPanel.h"

#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/FrameStatistics.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/OverlayGeometry.h"
#include "app/viewport/domain/ReferenceGrid.h"
#include "app/viewport/domain/RenderMode.h"
#include "app/viewport/domain/RenderOverride.h"
#include "app/viewport/domain/ScreenPick.h"
#include "app/viewport/domain/SelectionShape.h"
#include "app/viewport/domain/TrianglePick.h"
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "app/viewport/domain/ViewportView.h"
#include "app/viewport/ui/ToolStrip.h"
#include "app/viewport/ui/ViewportBar.h"
#include "app/viewport/ui/ViewportStatusBar.h"
#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/DrawBatches.h"
#include "gfx/Overlay.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"

#include <ImGuizmo.h>
#include <SDL3/SDL_gpu.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr f32 HighlightFill = 0.35F;
constexpr LineStyle WireframeStyle{.color = {210, 214, 222, 255}, .width = 1.0F};
constexpr f32 HighlightEdgeWidth = 1.0F;

constexpr f32 DegreesPerPoint = 0.4F;
/// How far, in points, the mouse may move between press and release for a
/// click to pick, and how near a point it must be.
constexpr f32 ClickDistance = 4.0F;
constexpr f32 BoxFillAlpha = 0.15F;
constexpr f32 SelectionFill = 0.18F;
constexpr f32 SelectionEdge = 0.85F;
constexpr f32 SelectionEdgeWidth = 1.0F;
constexpr f32 BoxWidth = 1.5F;
constexpr f32 BoxShadowAlpha = 0.55F;
constexpr f32 SelectionRingWidth = 2.0F;
constexpr f32 PickRadius = 16.0F;
constexpr float ToolStripMargin = 8.0F;
/// The gap between the selection modes and the rest of the bar, in item
/// spacings.
constexpr float ModeGap = 3.0F;
constexpr float ChipOpacity = 0.8F;
constexpr usize ReportedDiagnostics = 8;

[[nodiscard]] u32 makePixels(const f32 points, const f32 scale) {
    return static_cast<u32>(std::max(std::lround(points * scale), 1L));
}

constexpr f32 XrayOpacity = 0.3F;

[[nodiscard]] ArrowStyle scaleArrowStyle(const f32 scale) {
    const ArrowStyle points;
    return ArrowStyle{.width = points.width * scale,
                      .arrowLength = points.arrowLength * scale,
                      .arrowWidth = points.arrowWidth * scale,
                      .arrowGap = points.arrowGap * scale};
}

} // namespace

ViewportPanel::ViewportPanel(const gfx::Device& device)
    : Panel("viewport", "Viewport", core::icons::Cube, core::DockSlot::Centre), device(device),
      viewportContent(makeReferenceGrid()) {
    viewportTransform.setGroundSource([this] { return listGroundTriangles(); });
    std::expected<gfx::Renderer, base::Diagnostic> created = gfx::Renderer::create(device);
    if (created) {
        renderer.emplace(std::move(*created));
    } else {
        failure = base::describe(created.error());
        core::logError("the viewport cannot draw: {}", failure);
    }
}

void ViewportPanel::setContent(ViewportContent content, const std::optional<glm::vec3> target) {
    viewportContent = std::move(content);
    if (target) {
        orbitCamera.setTarget(*target);
    }
    stale = true;
    reported = false;
}

core::CameraPose ViewportPanel::getCameraPose() const {
    const glm::vec3 target = orbitCamera.getTarget();
    return core::CameraPose{.target = {target[0], target[1], target[2]},
                            .yaw = orbitCamera.getYaw(),
                            .pitch = orbitCamera.getPitch(),
                            .distance = orbitCamera.getDistance()};
}

void ViewportPanel::setCameraPose(const core::CameraPose& pose) {
    orbitCamera.setTarget(glm::vec3(pose.target[0], pose.target[1], pose.target[2]));
    orbitCamera.setAngles(pose.yaw, pose.pitch);
    orbitCamera.setDistance(pose.distance);
    stale = true;
}

void ViewportPanel::setView(const ViewportView& view) {
    if (view.renderMode != viewportView.renderMode) {
        stale = true;
    } else {
        isOverlayStale = true;
    }
    viewportView = view;
}

void ViewportPanel::drawBar() {
    drawSelectionModes(viewportTools);
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * ModeGap);
    drawPickModes(viewportTools, findPickMode());
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * ModeGap);
    if (viewportTools.mode == SelectionMode::Node &&
        viewportTransform.drawOptions(viewportTools.tool)) {
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * ModeGap);
    }
    const ViewportBarEdit edit = drawViewportBar(viewportView, viewportChoices);
    if (edit.view) {
        setView(*edit.view);
    }
    if (edit.choices && viewportChoices && chooseHandler != nullptr) {
        viewportChoices = *edit.choices;
        const std::function<void(const ViewportChoices&)> choose = chooseHandler;
        choose(*edit.choices);
    }
}

void ViewportPanel::draw() {
    drawBar();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size{available.x, available.y - ViewportStatusBar::getHeight()};
    if (size.x < 1.0F || size.y < 1.0F) {
        return;
    }
    const TransformView transformView = makeTransformView(origin, size);
    const bool isModal = viewportTransform.updateModal(transformView);
    if (isModal && ViewportTransform::isOverImage(transformView) &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        isPressSwallowed = true;
    }
    if (!isModal && !viewportTransform.isModal() &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        if (const std::optional<ViewportView> changed = applyViewportHotkeys(viewportView)) {
            setView(*changed);
        }
        applyToolHotkeys(viewportTools);
        if (viewportTools.mode == SelectionMode::Node) {
            viewportTransform.applyHotkeys(viewportTools.tool, transformView);
        }
    }
    // ImGuizmo takes the mouse only while no item is hovered, so over its
    // gizmo the viewport's own button gives way to a dummy of the same size.
    if (isTransforming()) {
        ImGui::Dummy(size);
    } else {
        ImGui::InvisibleButton("##viewport", size,
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
    }
    if (handleInput(size)) {
        stale = true;
    }
    if (ImGui::IsItemActive() && !isPressSwallowed &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, ClickDistance)) {
        boxStart = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
    }
    if (ImGui::IsItemDeactivated()) {
        if (isPressSwallowed) {
            isPressSwallowed = false;
        } else if (boxStart) {
            pickInBox(origin, size);
        } else if (ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <
                   ClickDistance * ClickDistance) {
            pick(origin, size);
        }
        boxStart.reset();
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end{origin.x + size.x, origin.y + size.y};
    if (!renderer) {
        drawList->AddText(origin, ImGui::GetColorU32(core::palette::Danger), failure.c_str());
        return;
    }

    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const u32 width = makePixels(size.x, scale.x);
    const u32 height = makePixels(size.y, scale.y);
    if (!renderTarget || renderTarget->getWidth() != width || renderTarget->getHeight() != height) {
        renderTarget.reset();
        std::expected<gfx::RenderTarget, base::Diagnostic> created =
            gfx::RenderTarget::create(device, width, height);
        if (!created) {
            failure = base::describe(created.error());
            drawList->AddText(origin, ImGui::GetColorU32(core::palette::Danger), failure.c_str());
            return;
        }
        renderTarget.emplace(std::move(*created));
        stale = true;
    }
    if (!renderTarget) {
        return;
    }
    const gfx::RenderTarget& target = *renderTarget;
    if (stale) {
        render(target);
        stale = false;
        isOverlayStale = false;
    } else if (isOverlayStale) {
        static_cast<void>(drawFrame(target));
        isOverlayStale = false;
    }

    const SDL_GPUTexture* color = target.getColor();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto texture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(color));
    drawList->AddImage(texture, origin, end);
    imageOrigin = origin;
    drawList->PushClipRect(origin, end, true);
    drawViewChanges(*drawList, origin, size);
    drawSelectionBoxes(*drawList, transformView);
    viewportTransform.setRingShown(viewportView.overlays.isShown &&
                                   viewportView.overlays.selectionRing);
    viewportTransform.drawOverlay(*drawList, transformView, viewportTools.tool);
    if (viewportTools.mode == SelectionMode::Node) {
        viewportTransform.drawGizmo(*drawList, transformView, viewportTools.tool);
    }
    viewportTransform.updatePreview();
    if (boxStart) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec4 fill = core::palette::Accent;
        fill.w = BoxFillAlpha;
        drawList->AddRectFilled(*boxStart, mouse, ImGui::GetColorU32(fill));
        drawList->AddRect(*boxStart, mouse, ImGui::GetColorU32(core::palette::Accent));
    }
    drawList->PopClipRect();
    drawToolStrip(viewportTools, ImVec2(origin.x + ToolStripMargin, origin.y + ToolStripMargin));
    ImVec2 notePosition{ImGui::GetItemRectMax().x + ToolStripMargin, origin.y + 6.0F};
    if (readingName) {
        const std::string note = std::format("Reading {}…", *readingName);
        drawList->AddText(notePosition, ImGui::GetColorU32(core::palette::TextMuted), note.c_str());
        notePosition.y += ImGui::GetTextLineHeightWithSpacing();
    }
    if (diagnosticCount > 0) {
        const std::string note = std::format("{} diagnostics, see the log", diagnosticCount);
        drawList->AddText(notePosition, ImGui::GetColorU32(core::palette::Warning), note.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, end.y));
    statusBar.draw();
}

gfx::Overlay ViewportPanel::makeSelectionHighlight(const DrawnFrame& drawing,
                                                   const f32 scale) const {
    const std::vector<ClipTriangle> triangles = listOwnedTriangles(
        drawing.rspResult, {.ranges = drawing.drawOutputs, .owners = drawing.drawOwners},
        selectedOwners);
    const ImVec4& accent = core::palette::Accent;
    const auto makeColor = [&accent](const f32 alpha) {
        return std::array<u8, 4>{
            static_cast<u8>(accent.x * 255.0F), static_cast<u8>(accent.y * 255.0F),
            static_cast<u8>(accent.z * 255.0F), static_cast<u8>(alpha * 255.0F)};
    };
    gfx::Overlay overlay;
    for (const ClipTriangle& triangle : triangles) {
        overlay.triangles.push_back(
            {.corners = triangle, .color = makeColor(SelectionFill), .hiddenOpacity = XrayOpacity});
    }
    overlay.lines = makeOutlines(
        triangles, {.color = makeColor(SelectionEdge), .width = SelectionEdgeWidth * scale});
    for (gfx::OverlayLine& line : overlay.lines) {
        line.hiddenOpacity = XrayOpacity;
    }
    return overlay;
}

void ViewportPanel::drawSelectionBoxes(ImDrawList& drawList, const TransformView& view) {
    const ViewportOverlays& overlays = viewportView.overlays;
    if (!overlays.isShown || !overlays.selectionBox || !drawnFrame || selectedOwners.empty()) {
        return;
    }
    if (!selectionBoxes) {
        selectionBoxes =
            findOwnedBoxes(drawnFrame->rspResult,
                           {.ranges = drawnFrame->drawOutputs, .owners = drawnFrame->drawOwners},
                           selectedOwners, glm::inverse(frameViewProjection));
    }
    const glm::mat4 viewProjection = view.projection * view.view;
    const ImU32 color = ImGui::GetColorU32(core::palette::Accent);
    const ImU32 shadow = ImGui::GetColorU32(ImVec4(0.0F, 0.0F, 0.0F, BoxShadowAlpha));
    for (const std::optional<WorldBox>& box : *selectionBoxes) {
        if (!box) {
            continue;
        }
        for (const auto& [start, end] : listBoxEdges(*box)) {
            const std::optional<std::array<glm::vec2, 2>> edge =
                findScreenSegment(viewProjection, {view.size.x, view.size.y}, start, end);
            if (!edge) {
                continue;
            }
            const ImVec2 from(view.origin.x + (*edge)[0].x, view.origin.y + (*edge)[0].y);
            const ImVec2 to(view.origin.x + (*edge)[1].x, view.origin.y + (*edge)[1].y);
            drawList.AddLine(from, to, shadow, BoxWidth + 2.0F);
            drawList.AddLine(from, to, color, BoxWidth);
        }
    }
}

gfx::Overlay ViewportPanel::makeHighlight(const f32 scale) const {
    const auto makeColor = [](const f32 alpha) {
        const ImVec4& accent = core::palette::Accent;
        return std::array<u8, 4>{
            static_cast<u8>(accent.x * 255.0F), static_cast<u8>(accent.y * 255.0F),
            static_cast<u8>(accent.z * 255.0F), static_cast<u8>(alpha * 255.0F)};
    };
    gfx::Overlay overlay;
    for (const ClipTriangle& triangle : highlight) {
        overlay.triangles.push_back(
            {.corners = triangle, .color = makeColor(HighlightFill), .hiddenOpacity = XrayOpacity});
    }
    overlay.lines =
        makeOutlines(highlight, {.color = makeColor(1.0F), .width = HighlightEdgeWidth * scale});
    for (gfx::OverlayLine& line : overlay.lines) {
        line.hiddenOpacity = XrayOpacity;
    }
    return overlay;
}

void ViewportPanel::drawViewChanges(ImDrawList& drawList, const ImVec2 origin,
                                    const ImVec2 size) const {
    const std::vector<std::string> changes = listViewChanges(viewportView);
    if (changes.empty()) {
        return;
    }
    std::string text;
    for (const std::string& change : changes) {
        text += text.empty() ? change : std::format("  ·  {}", change);
    }
    const ImVec2 padding{core::space::Medium, core::space::Small};
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 max{origin.x + size.x - core::space::Medium,
                     origin.y + core::space::Medium + textSize.y + (padding.y * 2.0F)};
    const ImVec2 min{max.x - textSize.x - (padding.x * 2.0F), origin.y + core::space::Medium};
    drawList.AddRectFilled(
        min, max,
        ImGui::GetColorU32(ImVec4(core::palette::Background.x, core::palette::Background.y,
                                  core::palette::Background.z, ChipOpacity)),
        core::space::Small);
    drawList.AddRect(min, max, ImGui::GetColorU32(core::palette::Border), core::space::Small);
    drawList.AddText(ImVec2(min.x + padding.x, min.y + padding.y),
                     ImGui::GetColorU32(core::palette::Warning), text.c_str());
}

glm::mat4 ViewportPanel::makeProjection(const ImVec2 size) const {
    const f32 zFar = viewportContent.lighting ? viewportContent.lighting->zFar : OrbitCamera::Far;
    return OrbitCamera::makeProjectionMatrix(size.x / size.y, zFar);
}

void ViewportPanel::pick(const ImVec2 origin, const ImVec2 size) const {
    if (!pickHandler || !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const std::optional<usize> picked =
        findPickedPoint(makeProjection(size) * orbitCamera.makeViewMatrix(), {size.x, size.y},
                        {mouse.x - origin.x, mouse.y - origin.y}, pickables, PickRadius);
    if (picked) {
        pickHandler({.points = std::span(&*picked, 1)}, findPickMode());
        return;
    }
    std::optional<usize> owner;
    if (drawnFrame) {
        const glm::vec2 point{(mouse.x - origin.x) / size.x * 2.0F - 1.0F,
                              1.0F - (mouse.y - origin.y) / size.y * 2.0F};
        if (const std::optional<usize> output = findPickedTriangle(drawnFrame->rspResult, point)) {
            if (const std::optional<usize> draw = findRangeOf(drawnFrame->drawOutputs, *output);
                draw && *draw < drawnFrame->drawOwners.size()) {
                owner = drawnFrame->drawOwners[*draw];
            }
        }
    }
    pickHandler({.owner = owner}, findPickMode());
}

void ViewportPanel::pickInBox(const ImVec2 origin, const ImVec2 size) const {
    if (!pickHandler || !boxStart || !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const std::vector<usize> picked =
        findPointsInRect(makeProjection(size) * orbitCamera.makeViewMatrix(), {size.x, size.y},
                         {boxStart->x - origin.x, boxStart->y - origin.y},
                         {mouse.x - origin.x, mouse.y - origin.y}, pickables);
    pickHandler({.points = picked}, findPickMode());
}

PickMode ViewportPanel::findPickMode() const {
    return findHeldPickMode().value_or(viewportTools.pickMode);
}

TransformView ViewportPanel::makeTransformView(const ImVec2 origin, const ImVec2 size) const {
    return {.view = orbitCamera.makeViewMatrix(),
            .projection = makeProjection(size),
            .eye = orbitCamera.getEye(),
            .origin = origin,
            .size = size};
}

bool ViewportPanel::isTransforming() const {
    return viewportTools.mode == SelectionMode::Node &&
           viewportTransform.isGizmoActive(viewportTools.tool);
}

std::vector<glm::vec3> ViewportPanel::listGroundTriangles() const {
    std::vector<glm::vec3> triangles;
    if (!drawnFrame) {
        return triangles;
    }
    const glm::mat4 inverse = glm::inverse(frameViewProjection);
    for (const n64::RspOutput& output : drawnFrame->rspResult.outputs) {
        const auto* triangle = std::get_if<n64::Triangle>(&output);
        if (triangle == nullptr) {
            continue;
        }
        const std::optional<usize> image = findImageAt(mappedImages, triangle->address);
        if (!image || *image >= viewportContent.labels.size() ||
            viewportContent.labels[*image].kind != ImageKind::Room) {
            continue;
        }
        for (const n64::ShadedVertex& vertex : triangle->vertices) {
            const glm::vec4 world = inverse * glm::vec4(vertex.position[0], vertex.position[1],
                                                        vertex.position[2], vertex.position[3]);
            triangles.emplace_back(glm::vec3(world) / world.w);
        }
    }
    return triangles;
}

bool ViewportPanel::handleInput(const ImVec2 size) {
    const ImGuiIO& io = ImGui::GetIO();
    bool moved = false;
    if (ImGui::IsItemActive()) {
        const ImVec2 delta = io.MouseDelta;
        const bool isNavigating = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                                  ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool panning = io.KeyShift;
        if (isNavigating && (delta.x != 0.0F || delta.y != 0.0F)) {
            if (panning) {
                orbitCamera.pan(-delta.x / size.y, delta.y / size.y);
            } else {
                orbitCamera.rotate(-delta.x * DegreesPerPoint, delta.y * DegreesPerPoint);
            }
            moved = true;
        }
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0F) {
        orbitCamera.zoom(io.MouseWheel);
        moved = true;
    }
    return moved;
}

void ViewportPanel::render(const gfx::RenderTarget& target) {
    if (!renderer) {
        return;
    }
    const FrameSize size{.width = target.getWidth(), .height = target.getHeight()};
    const RenderMode& mode = viewportView.renderMode;
    Frame frame = runFrame(viewportContent, orbitCamera, size, isTracing, makeRspOverrides(mode));
    FrameStatistics statistics = makeFrameStatistics(frame, viewportContent);
    const auto started = std::chrono::steady_clock::now();
    const u64 generation = isTracing ? ++frameGeneration : 0;
    DrawnFrame drawing{.rspResult = isTracing ? frame.rspResult : std::move(frame.rspResult),
                       .rdpResult = isTracing ? frame.rdpResult : std::move(frame.rdpResult),
                       .generation = generation,
                       .drawOutputs = std::move(frame.drawOutputs)};
    for (const ViewportDraw& draw : viewportContent.draws) {
        drawing.drawOwners.push_back(draw.owner);
    }
    selectionBoxes.reset();
    if (mode != RenderMode{}) {
        drawing.rdpResult = applyRenderMode(std::move(drawing.rdpResult), mode,
                                            makeViewProjection(viewportContent, orbitCamera, size));
    }
    drawnFrame = std::move(drawing);
    mappedImages = frame.mappedImages;
    frameViewProjection = makeViewProjection(viewportContent, orbitCamera, size);
    std::vector<base::Diagnostic> drawn = drawFrame(target);
    statusBar.addFrame(FrameReport{
        .statistics = std::move(statistics),
        .drawCalls = renderer->getDrawCalls(),
        .render = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - started)
                      .count()});
    std::ranges::move(drawn, std::back_inserter(frame.diagnostics));
    diagnosticCount = frame.diagnostics.size();
    const bool isReported = reported;
    reported = true;
    if (!isReported) {
        reportDiagnostics(frame);
    }
    if (isTracing) {
        tracedFrame = TracedFrame{.frame = std::move(frame),
                                  .content = viewportContent,
                                  .size = size,
                                  .generation = generation};
    }
}

std::vector<base::Diagnostic> ViewportPanel::drawFrame(const gfx::RenderTarget& target) {
    if (!renderer.has_value() || !drawnFrame.has_value()) {
        return {};
    }
    const DrawnFrame& drawing = *drawnFrame;
    const FrameSize size{.width = target.getWidth(), .height = target.getHeight()};
    const bool isWireframe = viewportView.renderMode.shading == Shading::Wireframe;
    return renderer->render(drawing.rspResult, isWireframe ? n64::RdpResult{} : drawing.rdpResult,
                            target, gfx::ScreenSize{.width = size.width, .height = size.height},
                            makeOverlay(drawing, size));
}

gfx::Overlay ViewportPanel::makeOverlay(const DrawnFrame& drawing, const FrameSize size) const {
    const ViewportOverlays& overlays = viewportView.overlays;
    gfx::Overlay overlay{.viewport = makeFrameViewport(size)};
    if (viewportView.renderMode.shading == Shading::Wireframe) {
        overlay.lines = makeTriangleEdges(drawing.rspResult, drawing.rdpResult, overlay.viewport,
                                          WireframeStyle);
    }
    const f32 scale = ImGui::GetIO().DisplayFramebufferScale.x;
    if (overlays.isShown) {
        const glm::mat4 viewProjection = makeViewProjection(viewportContent, orbitCamera, size);
        if (overlays.grid) {
            std::ranges::move(makeGridLines(viewProjection, orbitCamera.getTarget()),
                              std::back_inserter(overlay.lines));
        }
        if (overlays.flagLinks) {
            std::ranges::move(
                makeArrowLines(viewportOverlay, viewProjection, size, scaleArrowStyle(scale)),
                std::back_inserter(overlay.lines));
        }
    }
    if (overlays.xray) {
        for (gfx::OverlayLine& line : overlay.lines) {
            line.hiddenOpacity = XrayOpacity;
        }
    }
    if (overlays.isShown && overlays.selectionHighlight) {
        gfx::Overlay selected = makeSelectionHighlight(drawing, scale);
        std::ranges::move(selected.triangles, std::back_inserter(overlay.triangles));
        std::ranges::move(selected.lines, std::back_inserter(overlay.lines));
    }
    if (highlightGeneration != drawing.generation) {
        return overlay;
    }
    gfx::Overlay highlighted = makeHighlight(scale);
    std::ranges::move(highlighted.triangles, std::back_inserter(overlay.triangles));
    std::ranges::move(highlighted.lines, std::back_inserter(overlay.lines));
    return overlay;
}

void ViewportPanel::reportDiagnostics(const Frame& frame) {
    for (usize i = 0; i < std::min(frame.diagnostics.size(), ReportedDiagnostics); ++i) {
        const base::Diagnostic& diagnostic = frame.diagnostics[i];
        if (diagnostic.severity == base::Severity::Error) {
            core::logError("viewport: {}", base::describe(diagnostic));
        } else {
            core::logWarn("viewport: {}", base::describe(diagnostic));
        }
    }
    if (frame.diagnostics.size() > ReportedDiagnostics) {
        core::logWarn("viewport: {} more diagnostics",
                      frame.diagnostics.size() - ReportedDiagnostics);
    }
}

} // namespace toki::viewport

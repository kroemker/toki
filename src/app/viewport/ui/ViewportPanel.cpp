#include "ViewportPanel.h"

#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ReferenceGrid.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/DrawBatches.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"

#include <SDL3/SDL_gpu.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr float OverlayThickness = 2.0F;
constexpr f32 HighlightFill = 0.35F;
constexpr float ArrowLength = 11.0F;
constexpr float ArrowWidth = 5.0F;
constexpr float ArrowGap = 9.0F;

void drawArrowHead(ImDrawList& drawList, const ImVec2 from, const ImVec2 to, const u32 color) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt((dx * dx) + (dy * dy));
    if (length < ArrowLength + ArrowGap) {
        return;
    }
    const ImVec2 direction{dx / length, dy / length};
    const ImVec2 tip{to.x - (direction.x * ArrowGap), to.y - (direction.y * ArrowGap)};
    const ImVec2 base{tip.x - (direction.x * ArrowLength), tip.y - (direction.y * ArrowLength)};
    const ImVec2 side{-direction.y * ArrowWidth, direction.x * ArrowWidth};
    drawList.AddTriangleFilled(tip, ImVec2(base.x + side.x, base.y + side.y),
                               ImVec2(base.x - side.x, base.y - side.y), color);
}

constexpr f32 DegreesPerPoint = 0.4F;
constexpr usize ReportedDiagnostics = 8;

[[nodiscard]] u32 makePixels(const f32 points, const f32 scale) {
    return static_cast<u32>(std::max(std::lround(points * scale), 1L));
}

} // namespace

ViewportPanel::ViewportPanel(const gfx::Device& device)
    : Panel("viewport", "Viewport", core::icons::Cube, core::DockSlot::Centre), device(device),
      viewportContent(makeReferenceGrid()) {
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

void ViewportPanel::draw() {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0F || size.y < 1.0F) {
        return;
    }
    ImGui::InvisibleButton("##viewport", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    if (handleInput(size)) {
        stale = true;
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
    }

    const SDL_GPUTexture* color = target.getColor();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto texture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(color));
    drawList->AddImage(texture, origin, end);
    imageOrigin = origin;
    drawList->PushClipRect(origin, end, true);
    drawOverlay(*drawList, origin, size);
    drawHighlight(*drawList, origin, size);
    drawList->PopClipRect();
    ImVec2 notePosition{origin.x + 8.0F, origin.y + 6.0F};
    if (readingName) {
        const std::string note = std::format("Reading {}…", *readingName);
        drawList->AddText(notePosition, ImGui::GetColorU32(core::palette::TextMuted), note.c_str());
        notePosition.y += ImGui::GetTextLineHeightWithSpacing();
    }
    if (diagnosticCount > 0) {
        const std::string note = std::format("{} diagnostics, see the log", diagnosticCount);
        drawList->AddText(notePosition, ImGui::GetColorU32(core::palette::Warning), note.c_str());
    }
}

void ViewportPanel::drawOverlay(ImDrawList& drawList, const ImVec2 origin,
                                const ImVec2 size) const {
    if (viewportOverlay.lines.empty()) {
        return;
    }
    const f32 zFar = viewportContent.lighting ? viewportContent.lighting->zFar : OrbitCamera::Far;
    const glm::mat4 viewProjection =
        OrbitCamera::makeProjectionMatrix(size.x / size.y, zFar) * orbitCamera.makeViewMatrix();
    for (const OverlayLine& line : viewportOverlay.lines) {
        const std::optional<ScreenSegment> segment =
            projectSegment(viewProjection, line.from, line.to, glm::vec2(size.x, size.y));
        if (!segment) {
            continue;
        }
        const ImVec2 from{origin.x + segment->from.x, origin.y + segment->from.y};
        const ImVec2 to{origin.x + segment->to.x, origin.y + segment->to.y};
        drawList.AddLine(from, to, line.color, OverlayThickness);
        if (line.hasArrowAtTo && segment->isToWhole) {
            drawArrowHead(drawList, from, to, line.color);
        }
        if (line.hasArrowAtFrom && segment->isFromWhole) {
            drawArrowHead(drawList, to, from, line.color);
        }
    }
}

void ViewportPanel::drawHighlight(ImDrawList& drawList, const ImVec2 origin,
                                  const ImVec2 size) const {
    if (highlight.empty() || highlightSize.width == 0 || highlightSize.height == 0) {
        return;
    }
    const ImVec2 scale{size.x / static_cast<f32>(highlightSize.width),
                       size.y / static_cast<f32>(highlightSize.height)};
    const auto place = [&](const glm::vec2& corner) {
        return ImVec2(origin.x + (corner.x * scale.x), origin.y + (corner.y * scale.y));
    };
    const u32 fill = ImGui::GetColorU32(ImVec4(core::palette::Accent.x, core::palette::Accent.y,
                                               core::palette::Accent.z, HighlightFill));
    const u32 edge = ImGui::GetColorU32(core::palette::Accent);
    for (const std::array<glm::vec2, 3>& triangle : highlight) {
        const ImVec2 a = place(triangle[0]);
        const ImVec2 b = place(triangle[1]);
        const ImVec2 c = place(triangle[2]);
        drawList.AddTriangleFilled(a, b, c, fill);
        drawList.AddTriangle(a, b, c, edge);
    }
}

bool ViewportPanel::handleInput(const ImVec2 size) {
    const ImGuiIO& io = ImGui::GetIO();
    bool moved = false;
    if (ImGui::IsItemActive()) {
        const ImVec2 delta = io.MouseDelta;
        const bool panning = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                             ImGui::IsMouseDown(ImGuiMouseButton_Right) || io.KeyShift;
        if (delta.x != 0.0F || delta.y != 0.0F) {
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
    const u32 width = target.getWidth();
    const u32 height = target.getHeight();
    const FrameSize size{.width = width, .height = height};
    Frame frame = runFrame(viewportContent, orbitCamera, size, isTracing);
    std::vector<base::Diagnostic> drawn =
        renderer->render(frame.rspResult, frame.rdpResult, target,
                         gfx::ScreenSize{.width = width, .height = height});
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
                                  .generation = ++frameGeneration};
    }
}

void ViewportPanel::reportDiagnostics(const Frame& frame) {
    for (usize i = 0; i < std::min(frame.diagnostics.size(), ReportedDiagnostics); ++i) {
        core::logWarn("viewport: {}", base::describe(frame.diagnostics[i]));
    }
    if (frame.diagnostics.size() > ReportedDiagnostics) {
        core::logWarn("viewport: {} more diagnostics",
                      frame.diagnostics.size() - ReportedDiagnostics);
    }
}

} // namespace toki::viewport

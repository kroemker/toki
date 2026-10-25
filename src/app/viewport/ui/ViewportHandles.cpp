#include "ViewportHandles.h"

#include "app/core/ui/Theme.h"
#include "app/viewport/domain/ScreenPick.h"

#include <imgui.h>

#include <cmath>
#include <utility>

namespace toki::viewport {

namespace {

constexpr f32 HandleRadius = 5.0F;
constexpr f32 HoverRadius = 9.0F;

[[nodiscard]] glm::vec2 makeImagePoint(const TransformView& view, const ImVec2 point) {
    return {point.x - view.origin.x, point.y - view.origin.y};
}

[[nodiscard]] glm::mat4 makeViewProjection(const TransformView& view) {
    return view.projection * view.view;
}

} // namespace

void ViewportHandles::setHandles(std::vector<ViewportHandle> handles, Handler handler,
                                 PreviewHandler preview) {
    viewportHandles = std::move(handles);
    dragHandler = std::move(handler);
    previewHandler = std::move(preview);
    if (drag && drag->handle >= viewportHandles.size()) {
        end(false);
    }
    if (hovered && *hovered >= viewportHandles.size()) {
        hovered.reset();
    }
}

bool ViewportHandles::update(const TransformView& view) {
    if (drag) {
        follow(view);
        return true;
    }
    hovered.reset();
    if (viewportHandles.empty() || !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) ||
        ImGui::IsAnyItemActive() || !ViewportTransform::isOverImage(view)) {
        return false;
    }
    hovered = findHovered(view);
    if (!hovered) {
        return false;
    }
    ImGui::SetTooltip("%s", viewportHandles[*hovered].tooltip.c_str());
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        drag = Drag{.handle = *hovered, .start = makeImagePoint(view, ImGui::GetIO().MousePos)};
    }
    return true;
}

void ViewportHandles::follow(const TransformView& view) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        end(false);
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        end(true);
        return;
    }
    const ViewportHandle& handle = viewportHandles[drag->handle];
    const std::optional<f32> distance = findAxisDistance(
        makeViewProjection(view), glm::vec2(view.size.x, view.size.y), handle.position,
        handle.direction, drag->start, makeImagePoint(view, ImGui::GetIO().MousePos));
    const s32 rounded = distance ? static_cast<s32>(std::lround(*distance)) : 0;
    if (rounded != drag->distance) {
        drag->distance = rounded;
        if (previewHandler) {
            previewHandler(HandleDrag{.handle = drag->handle, .distance = rounded});
        }
    }
}

void ViewportHandles::end(const bool isConfirmed) {
    const Drag ended = *drag;
    drag.reset();
    if (previewHandler) {
        previewHandler(std::nullopt);
    }
    if (isConfirmed && ended.distance != 0 && dragHandler) {
        dragHandler(HandleDrag{.handle = ended.handle, .distance = ended.distance});
    }
}

std::optional<usize> ViewportHandles::findHovered(const TransformView& view) const {
    std::vector<glm::vec3> points;
    points.reserve(viewportHandles.size());
    for (const ViewportHandle& handle : viewportHandles) {
        points.push_back(handle.position);
    }
    return findPickedPoint(makeViewProjection(view), glm::vec2(view.size.x, view.size.y),
                           makeImagePoint(view, ImGui::GetIO().MousePos), points, HoverRadius);
}

void ViewportHandles::draw(ImDrawList& drawList, const TransformView& view) const {
    const glm::mat4 viewProjection = makeViewProjection(view);
    const glm::vec2 size(view.size.x, view.size.y);
    for (usize i = 0; i < viewportHandles.size(); ++i) {
        const ViewportHandle& handle = viewportHandles[i];
        const bool isDragged = drag && drag->handle == i;
        const glm::vec3 position = isDragged
                                       ? handle.position + glm::normalize(handle.direction) *
                                                               static_cast<f32>(drag->distance)
                                       : handle.position;
        const std::optional<glm::vec2> point = findScreenPosition(viewProjection, size, position);
        if (!point) {
            continue;
        }
        const ImVec2 center{view.origin.x + point->x, view.origin.y + point->y};
        const ImVec2 corner{HandleRadius, HandleRadius};
        const bool isLit = isDragged || (!drag && hovered == i);
        drawList.AddRectFilled(
            ImVec2(center.x - corner.x, center.y - corner.y),
            ImVec2(center.x + corner.x, center.y + corner.y),
            ImGui::GetColorU32(isLit ? core::palette::Text : core::palette::Accent));
        drawList.AddRect(ImVec2(center.x - corner.x, center.y - corner.y),
                         ImVec2(center.x + corner.x, center.y + corner.y),
                         ImGui::GetColorU32(core::palette::Background));
    }
}

} // namespace toki::viewport

#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/ui/ViewportTransform.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct ImDrawList;

namespace toki::viewport {

/// A point of what is selected that is dragged along `direction` alone, such
/// as a side of a box, and what dragging it does.
struct ViewportHandle {
    glm::vec3 position{0.0F};
    glm::vec3 direction{1.0F, 0.0F, 0.0F};
    std::string tooltip;
};

/// A drag of a handle: which one, and how far along its direction, in whole
/// world units.
struct HandleDrag {
    usize handle = 0;
    s32 distance = 0;

    friend bool operator==(const HandleDrag&, const HandleDrag&) = default;
};

/// Handles drawn over the image that the mouse drags, each along its own
/// direction. A drag is told to the preview handler as it runs and to the
/// handler once it is let go, so it is one edit however far it went; Escape
/// or a right click cancels it.
class ViewportHandles {
public:
    using Handler = std::function<void(const HandleDrag&)>;
    /// Shows a drag as it runs, or, given nothing, that it ended.
    using PreviewHandler = std::function<void(const std::optional<HandleDrag>&)>;

    /// Replaces the handles. A drag running keeps running while its handle
    /// is still there.
    void setHandles(std::vector<ViewportHandle> handles, Handler handler,
                    PreviewHandler preview = nullptr);

    /// Finds the handle under the mouse, begins a drag of it on a press and
    /// follows the drag. True while a handle has the mouse, when the
    /// viewport's own button gives way.
    bool update(const TransformView& view);

    void draw(ImDrawList& drawList, const TransformView& view) const;

private:
    struct Drag {
        usize handle = 0;
        glm::vec2 start{0.0F};
        s32 distance = 0;
    };

    [[nodiscard]] std::optional<usize> findHovered(const TransformView& view) const;
    void follow(const TransformView& view);
    void end(bool isConfirmed);

    std::vector<ViewportHandle> viewportHandles;
    Handler dragHandler;
    PreviewHandler previewHandler;
    std::optional<usize> hovered;
    std::optional<Drag> drag;
};

} // namespace toki::viewport

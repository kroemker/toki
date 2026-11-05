#pragma once

#include "app/message/Types.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageLayout.h"
#include "gfx/Device.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

namespace toki::message {

/// Where a box's image was shown: its top left corner on screen, and the
/// game's screen pixel that corner shows, at `scale` window pixels a game one.
struct BoxImage {
    ImVec2 origin;
    ImVec2 size;
    f32 scale = 1.0F;
    f32 left = 0.0F;
    f32 top = 0.0F;

    [[nodiscard]] ImVec2 findScreenPosition(f32 x, f32 y) const {
        return {origin.x + ((x - left) * scale), origin.y + ((y - top) * scale)};
    }

    [[nodiscard]] ImVec2 findGamePosition(ImVec2 screen) const {
        return {left + ((screen.x - origin.x) / scale), top + ((screen.y - origin.y) / scale)};
    }
};

/// Draws message boxes as the game draws them into images an ImGui window
/// shows, each in a slot of its own that is drawn again only when what it
/// shows changes.
class BoxRenderer {
public:
    explicit BoxRenderer(const gfx::Device& device);

    /// The width of a box's image over its height.
    static constexpr float AspectRatio = 272.0F / 84.0F;

    /// Draws `box` into slot `slot`, unless the slot already holds what it
    /// draws, and shows it `width` wide at the cursor, as an item a click
    /// lands on.
    BoxImage draw(usize slot, const LaidBox& box, const MessageArt& art, float width);

private:
    struct Slot {
        std::optional<gfx::RenderTarget> target;
        std::optional<u64> key;
        u8 boxType = 0;
    };

    [[nodiscard]] bool render(Slot& slot, const LaidBox& box, const MessageArt& art);

    const gfx::Device& device;
    std::optional<gfx::Renderer> renderer;
    std::string failure;
    std::vector<Slot> slots;
    GbiCommandCache gbiCommandCache;
};

} // namespace toki::message

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

/// Draws message boxes as the game draws them into images an ImGui window
/// shows, each in a slot of its own that is drawn again only when what it
/// shows changes.
class BoxRenderer {
public:
    explicit BoxRenderer(const gfx::Device& device);

    /// The width of a box's image over its height.
    static constexpr float AspectRatio = 272.0F / 84.0F;

    /// Draws `box` into slot `slot`, unless the slot already holds what it
    /// draws, and shows it `width` wide at the cursor.
    void draw(usize slot, const LaidBox& box, const MessageArt& art, float width);

private:
    struct Slot {
        std::optional<gfx::RenderTarget> target;
        std::optional<u64> key;
        u8 boxType = 0;
        s32 boxY = 0;
    };

    [[nodiscard]] bool render(Slot& slot, const LaidBox& box, const MessageArt& art);

    const gfx::Device& device;
    std::optional<gfx::Renderer> renderer;
    std::string failure;
    std::vector<Slot> slots;
    GbiCommandCache gbiCommandCache;
};

} // namespace toki::message

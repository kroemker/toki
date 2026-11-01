#include "BoxRenderer.h"

#include "app/core/ui/Theme.h"
#include "base/Diagnostic.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

namespace toki::message {

namespace {

constexpr u32 ScreenWidth = 320;
constexpr u32 ScreenHeight = 240;
constexpr u32 Scale = 3;
constexpr float CropLeft = 24.0F;
constexpr float CropWidth = 272.0F;
constexpr float CropAbove = 6.0F;
constexpr float CropHeight = 84.0F;
constexpr u8 BoxTypeNoneNoShadow = 5;
constexpr ImVec4 DarkBackdrop{0.16F, 0.18F, 0.22F, 1.0F};
constexpr ImVec4 LightBackdrop{0.78F, 0.78F, 0.74F, 1.0F};

} // namespace

BoxRenderer::BoxRenderer(const gfx::Device& device) : device(device) {
    std::expected<gfx::Renderer, base::Diagnostic> created = gfx::Renderer::create(device);
    if (created) {
        renderer.emplace(std::move(*created));
    } else {
        failure = base::describe(created.error());
    }
}

bool BoxRenderer::render(Slot& slot, const LaidBox& box, const MessageArt& art) {
    if (!slot.target) {
        std::expected<gfx::RenderTarget, base::Diagnostic> created =
            gfx::RenderTarget::create(device, ScreenWidth * Scale, ScreenHeight * Scale);
        if (!created) {
            failure = base::describe(created.error());
            return false;
        }
        slot.target.emplace(std::move(*created));
    }
    const BoxFrame frame = runBoxFrame(box, art);
    static_cast<void>(renderer->render(frame.rspResult, frame.rdpResult, *slot.target));
    slot.boxType = box.boxType;
    slot.boxY = box.y;
    return true;
}

void BoxRenderer::draw(const usize slotIndex, const u64 key, const LaidBox& box,
                       const MessageArt& art, const float width) {
    const ImVec2 size{width, width / AspectRatio};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!renderer) {
        drawList->AddText(origin, ImGui::GetColorU32(core::palette::Danger), failure.c_str());
        return;
    }
    if (slots.size() <= slotIndex) {
        slots.resize(slotIndex + 1);
    }
    Slot& slot = slots[slotIndex];
    if (slot.key != key) {
        if (!render(slot, box, art)) {
            drawList->AddText(origin, ImGui::GetColorU32(core::palette::Danger), failure.c_str());
            return;
        }
        slot.key = key;
    }

    const ImVec2 end{origin.x + size.x, origin.y + size.y};
    const ImVec4 backdrop = slot.boxType == BoxTypeNoneNoShadow ? LightBackdrop : DarkBackdrop;
    drawList->AddRectFilled(origin, end, ImGui::GetColorU32(backdrop));
    const float top = std::clamp(static_cast<float>(slot.boxY) - CropAbove, 0.0F,
                                 static_cast<float>(ScreenHeight) - CropHeight);
    const ImVec2 uvStart{CropLeft / ScreenWidth, top / ScreenHeight};
    const ImVec2 uvEnd{(CropLeft + CropWidth) / ScreenWidth, (top + CropHeight) / ScreenHeight};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto texture =
        static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(slot.target->getColor()));
    drawList->AddImage(texture, origin, end, uvStart, uvEnd);
}

} // namespace toki::message

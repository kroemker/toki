#include "BoxRenderer.h"

#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageCatalog.h"
#include "base/Diagnostic.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <expected>
#include <span>
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

class KeyMixer {
public:
    void add(const u64 part) {
        key = (key ^ part) * 0x100000001B3ULL;
    }

    [[nodiscard]] u64 getKey() const {
        return key;
    }

private:
    u64 key = 0xCBF29CE484222325ULL;
};

/// What the box draws, so a box whose content is the same is not drawn again
/// when another box of its message is typed in.
[[nodiscard]] u64 makeBoxKey(const LaidBox& box, const MessageArt& art) {
    KeyMixer mixer;
    mixer.add(std::bit_cast<std::uintptr_t>(&art));
    for (const s64 part :
         {s64{box.boxType}, s64{box.x}, s64{box.y}, s64{box.isWide},
          s64{static_cast<u8>(box.endIcon)}, s64{box.endIconX}, s64{box.endIconY}}) {
        mixer.add(static_cast<u64>(part));
    }
    if (box.icon) {
        for (const s64 part : {s64{box.icon->textureIndex}, s64{box.icon->isQuest},
                               s64{box.icon->x}, s64{box.icon->y}, s64{box.icon->size}}) {
            mixer.add(static_cast<u64>(part));
        }
    }
    for (const LaidCharacter& character : box.characters) {
        for (const s64 part :
             {s64{character.fontOffset}, s64{character.x}, s64{character.y},
              s64{character.color.red}, s64{character.color.green}, s64{character.color.blue}}) {
            mixer.add(static_cast<u64>(part));
        }
    }
    return mixer.getKey();
}

} // namespace

BoxRenderer::BoxRenderer(const gfx::Device& device, BoxFrameWorker& boxFrameWorker)
    : device(device), boxFrameWorker(boxFrameWorker), owner(boxFrameWorker.makeOwner()) {
    std::expected<gfx::Renderer, base::Diagnostic> created = gfx::Renderer::create(device);
    if (created) {
        renderer.emplace(std::move(*created));
    } else {
        failure = base::describe(created.error());
    }
}

BoxRenderer::~BoxRenderer() {
    boxFrameWorker.forget(owner);
}

std::span<const base::Diagnostic> BoxRenderer::getDiagnostics(const usize slot) const {
    if (slot >= slots.size()) {
        return {};
    }
    return slots[slot].diagnostics;
}

bool BoxRenderer::render(Slot& slot, const BoxFrame& frame, const u8 boxType) {
    if (!slot.target) {
        std::expected<gfx::RenderTarget, base::Diagnostic> created =
            gfx::RenderTarget::create(device, ScreenWidth * Scale, ScreenHeight * Scale);
        if (!created) {
            failure = base::describe(created.error());
            return false;
        }
        slot.target.emplace(std::move(*created));
    }
    static_cast<void>(renderer->render(frame.rspResult, frame.rdpResult, *slot.target));
    slot.boxType = boxType;
    return true;
}

BoxImage BoxRenderer::draw(const usize slotIndex, const LaidBox& box, const SharedCatalog& catalog,
                           const float width) {
    const MessageArt& art = catalog->art;
    const LaidBox drawn = makeMovedBox(box, static_cast<s32>(CropAbove) - box.y);
    const u64 key = makeBoxKey(drawn, art);
    const ImVec2 size{width, width / AspectRatio};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float top = static_cast<float>(box.y) - CropAbove;
    BoxImage image{
        .origin = origin, .size = size, .scale = width / CropWidth, .left = CropLeft, .top = top};
    ImGui::InvisibleButton("##box", size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!renderer) {
        drawList->AddText(origin, ImGui::GetColorU32(core::palette::Danger), failure.c_str());
        return image;
    }
    if (slots.size() <= slotIndex) {
        slots.resize(slotIndex + 1);
    }
    Slot& slot = slots[slotIndex];
    if (slot.failedKey == key) {
        drawList->AddText(origin, ImGui::GetColorU32(core::palette::Danger), failure.c_str());
        return image;
    }
    const BoxSlot workerSlot{.owner = owner, .index = slotIndex};
    if (slot.key != key) {
        std::optional<FinishedFrame> finished = boxFrameWorker.takeFinished(workerSlot);
        if (finished && finished->key == key) {
            if (!render(slot, finished->frame, drawn.boxType)) {
                slot.failedKey = key;
                drawList->AddText(origin, ImGui::GetColorU32(core::palette::Danger),
                                  failure.c_str());
                return image;
            }
            slot.key = key;
            slot.diagnostics = std::move(finished->frame.diagnostics);
        } else {
            boxFrameWorker.request(workerSlot, key, drawn, catalog);
        }
    }
    if (!slot.key) {
        image.isPending = true;
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                ImGui::GetColorU32(DarkBackdrop));
        return image;
    }

    const ImVec2 end{origin.x + size.x, origin.y + size.y};
    const ImVec4 backdrop = slot.boxType == BoxTypeNoneNoShadow ? LightBackdrop : DarkBackdrop;
    drawList->AddRectFilled(origin, end, ImGui::GetColorU32(backdrop));
    const ImVec2 uvStart{CropLeft / ScreenWidth, 0.0F};
    const ImVec2 uvEnd{(CropLeft + CropWidth) / ScreenWidth, CropHeight / ScreenHeight};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto texture =
        static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(slot.target->getColor()));
    drawList->AddImage(texture, origin, end, uvStart, uvEnd);
    return image;
}

} // namespace toki::message

#include "SignGlyphs.h"

#include "app/message/domain/MessageDrawing.h"

#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace toki::message {

const gfx::ImageTexture* SignGlyphs::findGlyph(const MessageArt& art, const u16 character) {
    constexpr u32 GlyphSize = 16;
    if (uploadedArt != &art) {
        uploadedArt = &art;
        glyphs.clear();
    }
    auto found = glyphs.find(character);
    if (found == glyphs.end()) {
        std::optional<gfx::ImageTexture> texture;
        if (const std::optional<std::vector<u8>> pixels = decodeFontGlyph(art, character)) {
            if (auto uploaded = gfx::ImageTexture::create(device, GlyphSize, GlyphSize, *pixels)) {
                texture = std::move(*uploaded);
            }
        }
        found = glyphs.emplace(character, std::move(texture)).first;
    }
    return found->second ? &*found->second : nullptr;
}

} // namespace toki::message

#include "SignGlyphs.h"

#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageGeometry.h"

#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace toki::message {

namespace {

[[nodiscard]] std::optional<std::vector<u8>> decodeSign(const MessageArt& art,
                                                        const MessageGeometry& geometry,
                                                        const bool isWide, const u16 character) {
    if (!isWide) {
        return decodeFontGlyph(art, character);
    }
    const std::optional<u32> offset = findKanjiOffset(character, geometry);
    return offset ? decodeKanjiGlyph(art, *offset) : std::nullopt;
}

} // namespace

const gfx::ImageTexture* SignGlyphs::findGlyph(const MessageArt& art,
                                               const MessageGeometry& geometry, const bool isWide,
                                               const u16 character) {
    constexpr u32 GlyphSize = 16;
    if (uploadedArt != &art) {
        uploadedArt = &art;
        glyphs.clear();
    }
    auto found = glyphs.find(character);
    if (found == glyphs.end()) {
        std::optional<gfx::ImageTexture> texture;
        if (const std::optional<std::vector<u8>> pixels =
                decodeSign(art, geometry, isWide, character)) {
            if (auto uploaded = gfx::ImageTexture::create(device, GlyphSize, GlyphSize, *pixels)) {
                texture = std::move(*uploaded);
            }
        }
        found = glyphs.emplace(character, std::move(texture)).first;
    }
    return found->second ? &*found->second : nullptr;
}

} // namespace toki::message

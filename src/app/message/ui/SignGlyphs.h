#pragma once

#include "app/message/Types.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageGeometry.h"
#include "gfx/Device.h"
#include "gfx/ImageTexture.h"

#include <map>
#include <optional>

namespace toki::message {

/// The glyphs of the checkout's fonts as textures the UI draws, so that a
/// button that puts a sign in shows it as the game does. Uploaded once per
/// font read.
class SignGlyphs {
public:
    explicit SignGlyphs(const gfx::Device& device) : device(device) {}

    /// The glyph `character` is drawn with in `art`'s font, if it has one:
    /// for Japanese (`isWide`) a Shift-JIS code in the `kanji` font, found by
    /// `geometry`, and otherwise a byte of `nes_font_static`.
    [[nodiscard]] const gfx::ImageTexture*
    findGlyph(const MessageArt& art, const MessageGeometry& geometry, bool isWide, u16 character);

private:
    const gfx::Device& device;
    const MessageArt* uploadedArt = nullptr;
    std::map<u16, std::optional<gfx::ImageTexture>> glyphs;
};

} // namespace toki::message

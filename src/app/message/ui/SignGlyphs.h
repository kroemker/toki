#pragma once

#include "app/message/Types.h"
#include "app/message/domain/MessageDrawing.h"
#include "gfx/Device.h"
#include "gfx/ImageTexture.h"

#include <map>
#include <optional>

namespace toki::message {

/// The glyphs of the checkout's font as textures the UI draws, so that a
/// button that puts a sign in shows it as the game does. Uploaded once per
/// font read.
class SignGlyphs {
public:
    explicit SignGlyphs(const gfx::Device& device) : device(device) {}

    /// The glyph `character` is drawn with in `art`'s font, if it has one.
    [[nodiscard]] const gfx::ImageTexture* findGlyph(const MessageArt& art, u16 character);

private:
    const gfx::Device& device;
    const MessageArt* uploadedArt = nullptr;
    std::map<u16, std::optional<gfx::ImageTexture>> glyphs;
};

} // namespace toki::message

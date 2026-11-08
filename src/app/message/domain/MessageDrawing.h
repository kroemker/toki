#pragma once

#include "app/message/Types.h"
#include "app/message/domain/MessageLayout.h"
#include "base/Diagnostic.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "z64/DisplayList.h"
#include "z64/MemoryImage.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hackeroot {
class Workspace;
struct Prelude;
} // namespace hackeroot

namespace toki::message {

/// What a message box is drawn with, read from the checkout as the build
/// links it: the box, icon and end mark textures of `message_static`, the
/// font of `nes_font_static`, the item icons of `icon_item_static` and
/// `icon_item_24_static`, and the game code's `sSetupDL`, all in one image,
/// with the gbi the box's display list is written against.
struct MessageArt {
    z64::MemoryImage image;
    std::optional<u32> setupList;
    std::unique_ptr<hackeroot::Prelude> gbi;

    /// Whether `N64_BTN_COLORS` gives the end mark the N64's blue rather than
    /// the GameCube's green.
    bool hasN64ButtonColors = true;

    /// Whether the `kanji` font Japanese is drawn with was read. It is read
    /// only when asked for: it is some four thousand textures.
    bool hasKanji = false;

    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;
};

[[nodiscard]] MessageArt readMessageArt(const hackeroot::Workspace& workspace, bool readsKanji);

/// Whether `nes_font_static` was read.
[[nodiscard]] bool hasFont(const MessageArt& art);

/// Whether `nes_font_static` holds a glyph for `character`. The charmap names
/// signs past its end, `[D-Pad]` among them, which the game loads from
/// whatever follows the font; a box draws a red `?` in their place.
[[nodiscard]] bool hasFontGlyph(const MessageArt& art, u16 character);

/// The glyph `nes_font_static` draws `character` with: 16 by 16 pixels, row
/// by row from the top, each white with the font's intensity as its alpha,
/// four bytes a pixel. Nothing when the font has no such glyph.
[[nodiscard]] std::optional<std::vector<u8>> decodeFontGlyph(const MessageArt& art, u16 character);

/// The display list one box is drawn with, as `Message_DrawTextBox`,
/// `Message_DrawText` and `Message_DrawTextboxIcon` build it once the box's
/// text is all shown: the gbi macros they call, written with the box's
/// positions and the art's addresses, and encoded with the checkout's gbi.
[[nodiscard]] std::string writeBoxDisplayList(const LaidBox& box, const MessageArt& art);

/// The words of gbi commands already encoded, by the command as written.
/// Expanding a command through the gbi's macros is most of what drawing a box
/// costs, and a box drawn again, as it is typed, is mostly commands drawn
/// before. Kept for one gbi, and used from one thread.
struct GbiCommandCache {
    const hackeroot::Prelude* gbi = nullptr;
    std::unordered_map<std::string, std::vector<u8>> commands;
};

/// The bytes of `writeBoxDisplayList`, each command encoded once into `cache`
/// and taken from it after.
[[nodiscard]] z64::DisplayList encodeBoxDisplayList(const LaidBox& box, const MessageArt& art,
                                                    GbiCommandCache& cache);

/// What the RSP and RDP made of one box's display list, ready for the GPU,
/// on a screen of 320 by 240.
struct BoxFrame {
    n64::RspResult rspResult;
    n64::RdpResult rdpResult;
    std::vector<base::Diagnostic> diagnostics;
};

[[nodiscard]] BoxFrame runBoxFrame(const LaidBox& box, const MessageArt& art,
                                   GbiCommandCache& cache);

[[nodiscard]] BoxFrame runBoxFrame(const LaidBox& box, const MessageArt& art);

} // namespace toki::message

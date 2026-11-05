#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageGeometry.h"
#include "base/Diagnostic.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace toki::message {

struct TextColor {
    u8 red = 0;
    u8 green = 0;
    u8 blue = 0;

    friend bool operator==(const TextColor&, const TextColor&) = default;
};

/// One character drawn, at the screen position of its top left corner. A
/// sample stands for what the game fills in when it opens the message, the
/// player's name or a timer.
struct LaidCharacter {
    /// The byte, or for Japanese the Shift-JIS code, the character was laid
    /// out from.
    u16 character = 0;
    /// Where its glyph starts in the font: `nes_font_static`, or for Japanese
    /// `kanji`.
    u32 fontOffset = 0;
    s32 x = 0;
    s32 y = 0;
    TextColor color;
    bool isSample = false;
    /// The first byte of the code or character it was laid out from.
    usize byte = 0;
};

/// Where the game's pen stood when it reached a code or character of the box,
/// before it drew it or did what it says, and the color it writes in after.
/// A code that draws nothing, a color or a pause, has its stop all the same.
struct LaidStop {
    usize byte = 0;
    s32 x = 0;
    s32 y = 0;
    TextColor color;
};

/// `ITEM_ICON`: an item's icon, 32 pixels square, or a quest item's, 24.
/// `textureIndex` is the icon's place in `icon_item_static`, or for a quest
/// item in `icon_item_24_static`, counted from `ITEM_MEDALLION_FOREST`.
struct LaidIcon {
    u8 item = 0;
    bool isQuest = false;
    u32 textureIndex = 0;
    s32 x = 0;
    s32 y = 0;
    s32 size = 0;
};

/// `BACKGROUND`: the two halves of a picture from `message_texture_static`
/// drawn behind the text, with a shadow, as the unused messages that have one
/// draw it.
struct LaidBackground {
    u8 picture = 0;
    u8 foreColor = 0;
    u8 backColor = 0;
    u8 yOffset = 0;
    s32 x = 0;
    s32 y = 0;
};

/// A line of text, from where it starts to where its last character ends.
struct LaidLine {
    s32 y = 0;
    s32 startX = 0;
    s32 endX = 0;
};

/// What the box shows once its text is drawn: the triangle that waits for the
/// next box or message, the square that closes it, the arrow of a choice, or
/// nothing where it closes or goes on by itself.
enum class EndIcon : u8 {
    None,
    Triangle,
    Square,
    Arrow,
};

inline constexpr s32 BoxWidth = 256;
inline constexpr s32 BoxHeight = 64;
/// Where every line of every box starts on screen, before an indent.
inline constexpr s32 TextStartX = 65;
/// The same for Japanese (`R_TEXT_INIT_XPOS` of an NTSC build).
inline constexpr s32 WideTextStartX = 50;

/// One box of a message as the game draws it once all its text is shown.
/// `begin` and `end` are the bytes it was laid out from, its closing code
/// included.
struct LaidBox {
    usize begin = 0;
    usize end = 0;
    u8 boxType = 0;
    s32 x = 0;
    s32 y = 0;

    /// Whether it is laid out as Japanese, two bytes a character, and drawn
    /// with the `kanji` font.
    bool isWide = false;

    /// Whether the message lets the game put the box above or below, by where
    /// the player and the actor spoken to stand. It is laid out below.
    bool isPositionVariable = false;

    std::vector<LaidCharacter> characters;
    /// One stop for each code and character of the box in order, its closing
    /// code last; a sample has one, where it starts.
    std::vector<LaidStop> stops;
    std::vector<LaidLine> lines;
    std::optional<LaidIcon> icon;
    std::optional<LaidBackground> background;

    EndIcon endIcon = EndIcon::None;
    s32 endIconX = 0;
    s32 endIconY = 0;

    /// Where the choices of `TWO_CHOICE` or `THREE_CHOICE` are, from the
    /// first; the arrow is drawn at the first.
    std::vector<s32> choiceYs;
};

/// How far a line of `box` may reach before it runs into the box's right
/// margin: as far from its right edge as text starts from its left. The game
/// stops nowhere; past this, text runs over the border or off the box.
[[nodiscard]] s32 findLineLimit(const LaidBox& box);

/// Where the lines of `box` start before an indent.
[[nodiscard]] s32 findLineStart(const LaidBox& box);

/// How far one line of `box` stands below the one before.
[[nodiscard]] s32 findLineSpacing(const LaidBox& box);

/// How tall a character of `box` is drawn.
[[nodiscard]] s32 findCharacterHeight(const LaidBox& box);

struct MessageLayout {
    std::vector<LaidBox> boxes;
    /// Where in the text each byte laid out was encoded from, when the layout
    /// was made from a text rather than from bytes alone.
    std::vector<TextPosition> sources;
    std::vector<base::Diagnostic> diagnostics;
};

/// What a layout needs besides the bytes: the box the message asks for, as
/// the values of its `TEXTBOX_TYPE_` and `TEXTBOX_POS_` constants, and the
/// first item drawn with a quest icon (`ITEM_MEDALLION_FOREST`).
struct LayoutRequest {
    u8 boxType = 0;
    u8 boxPosition = 0;
    s64 firstQuestItem = 0;
};

/// Lays out one message's encoded text box by box, as `Message_Decode` and
/// `Message_DrawText` of `src/code/z_message.c` do for English, German and
/// French on a 4:3 screen of 320 by 240. Box after box is laid out until `END`
/// or the end of the bytes. The tables come from `geometry`; what that file
/// does in code is done here the same way.
[[nodiscard]] MessageLayout layoutMessage(std::span<const u8> bytes,
                                          const MessageGeometry& geometry,
                                          const LayoutRequest& request);

/// Lays out one Japanese message as `Message_Decode` and `Message_DrawTextWide`
/// do on an NTSC build: two bytes a code or character, characters from the
/// `kanji` font found through `geometry.nonKanjiIndices`, and the spacing
/// those functions give some signs.
[[nodiscard]] MessageLayout layoutWideMessage(std::span<const u8> bytes,
                                              const MessageGeometry& geometry,
                                              const LayoutRequest& request);

} // namespace toki::message

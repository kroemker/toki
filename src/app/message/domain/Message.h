#pragma once

#include "app/message/Types.h"
#include "base/Diagnostic.h"

#include <array>
#include <string>
#include <variant>
#include <vector>

namespace toki::message {

/// The languages a message has text for, in the order `DEFINE_MESSAGE` takes
/// them. NTSC builds build Japanese and English, PAL builds English, German
/// and French.
enum class Language : u8 {
    Japanese,
    English,
    German,
    French,
};

inline constexpr usize LanguageCount = 4;

/// Text between quotes, its escapes decoded. A message's text is UTF-8 as the
/// checkout writes it; the build's charmap turns it into the game's bytes.
struct TextPiece {
    std::string text;
};

/// `\n` in a string: the next line of the box.
struct NewlinePiece {};

/// A control code of `message_data_fmt.h`, `COLOR(RED)` or `BOX_BREAK`, with
/// its arguments as written.
struct CodePiece {
    std::string name;
    std::vector<std::string> arguments;
    base::SourceLocation location;
};

using Piece = std::variant<TextPiece, NewlinePiece, CodePiece>;

/// One language's `MSG(...)`. A language the message has no text in is written
/// `MSG(/* MISSING */)`; `placeholder` keeps that comment's word.
struct MessageText {
    std::vector<Piece> pieces;
    bool isPresent = false;
    std::string placeholder;
};

/// Which languages' tables a row is part of. `DEFINE_MESSAGE` is in all of
/// them. `DEFINE_MESSAGE_JPN` is in the Japanese table only and
/// `DEFINE_MESSAGE_NES` in the English, German and French ones, so one ID may
/// be two rows with different boxes. `DEFINE_MESSAGE_FFFC` is the message the
/// game loads its font order from, which must stand right before the last one;
/// it is Japanese on NTSC builds and English on PAL builds.
enum class MessageMacro : u8 {
    Message,
    JapaneseOnly,
    NesOnly,
    FontMessage,
};

/// One `DEFINE_MESSAGE` as the build reads it. Box type and position are the
/// constants written (`TEXTBOX_TYPE_BLUE`, `TEXTBOX_POS_BOTTOM`).
struct Message {
    u16 id = 0;
    std::string boxType;
    std::string boxPosition;
    MessageMacro macro = MessageMacro::Message;
    std::array<MessageText, LanguageCount> texts;

    /// Whether the message comes from `extracted/<version>/text/`, which
    /// `assets/text/message_data.h` includes, rather than from that file.
    bool isVanilla = false;

    base::SourceLocation location;
};

/// Whether the build puts `message`'s text for `language` into that language's
/// table. `isFontMessageEnglish` says where the font message goes.
[[nodiscard]] bool isInLanguage(const Message& message, Language language,
                                bool isFontMessageEnglish);

/// Every message of the checkout in the order the build lays them out, which
/// is the order the game searches them in.
struct MessageTable {
    std::vector<Message> messages;
    std::vector<base::Diagnostic> diagnostics;
};

} // namespace toki::message

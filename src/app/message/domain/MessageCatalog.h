#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageEncoder.h"
#include "app/message/domain/MessageGeometry.h"
#include "app/message/domain/MessageLayout.h"
#include "base/Diagnostic.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace hackeroot {
class Workspace;
} // namespace hackeroot

namespace toki::message {

/// The languages a message can be shown and edited in.
inline constexpr std::array ShownLanguages = {Language::English, Language::German, Language::French,
                                              Language::Japanese};

/// Everything the checkout says about its messages that showing one needs:
/// the messages, how they encode, the tables they are laid out with, and the
/// art they are drawn with. Read once, off the UI thread, and replaced whole
/// when one of `files` changes.
struct MessageCatalog {
    /// Kept alive for the format, whose macros layer over the workspace's.
    std::shared_ptr<const hackeroot::Workspace> workspace;
    MessageTable table;
    std::optional<MessageFormat> format;
    /// Japanese's, encoded two bytes a character.
    std::optional<MessageFormat> wideFormat;
    MessageGeometry geometry;
    MessageArt art;
    bool isFontMessageEnglish = false;
    /// The languages of `ShownLanguages` some message has text in: an NTSC
    /// checkout has no German or French, a PAL one no Japanese.
    std::vector<Language> languages;
    /// The colors `COLOR` takes, as `TextColor` of `message_data_fmt.h` names
    /// them without its prefix: `RED`.
    std::vector<std::string> colors;
    /// The constants `ITEM_ICON`, `HIGHSCORE` and `SFX` take, as the
    /// checkout's `ItemID`, `HighScores` and `SfxId` declare them.
    std::vector<std::string> items;
    std::vector<std::string> highScores;
    std::vector<std::string> sounds;
    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;

    /// The bytes `message` has in `language`, as the build encodes them.
    [[nodiscard]] EncodedText encode(const Message& message, Language language) const;

    /// `message` in `language` laid out box by box; a message whose box the
    /// format cannot name is a layout with a diagnostic saying so.
    [[nodiscard]] MessageLayout layOut(const Message& message, Language language) const;

    /// The charmap `language` is encoded with, if it was read.
    [[nodiscard]] const Charmap* findCharmap(Language language) const;
};

/// Reads the catalog of `workspace`, Japanese encoded with the Shift-JIS table
/// at `shiftJisTable`, and with the `kanji` font it is drawn with if `readsKanji`.
[[nodiscard]] MessageCatalog
readMessageCatalog(const std::shared_ptr<const hackeroot::Workspace>& workspace,
                   const std::filesystem::path& shiftJisTable, bool readsKanji);

} // namespace toki::message

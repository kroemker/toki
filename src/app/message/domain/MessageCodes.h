#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"

#include <span>
#include <string>
#include <string_view>

namespace toki::message {

/// What one argument of a control code is written as.
enum class ArgumentKind : u8 {
    /// A number of pixels, frames or the like, written in decimal.
    Number,
    /// A message ID, written in hex: `0x6023`.
    MessageId,
    /// An `ItemID`: `ITEM_POCKET_EGG`.
    Item,
    /// A `HighScores`: `HS_HORSE_RACE`.
    HighScore,
    /// A `SfxId`: `NA_SE_SY_DECIDE`.
    Sound,
};

struct CodeArgument {
    std::string_view label;
    ArgumentKind kind = ArgumentKind::Number;
    /// What the argument is when the code is put in; for a constant, empty
    /// takes the first the checkout declares.
    std::string_view initial;
};

/// A control code the rail puts in: its macro, and the arguments it takes.
struct InsertableCode {
    std::string_view macro;
    std::span<const CodeArgument> arguments;
};

/// Every control code but `COLOR` and the box breaks, which the rail puts in
/// otherwise, grouped as a person looks for them.
[[nodiscard]] std::span<const InsertableCode> listInsertableCodes();

/// The insertable code `macro` names, if it is one.
[[nodiscard]] const InsertableCode* findInsertableCode(std::string_view macro);

/// `code` with the arguments it is put in with.
[[nodiscard]] CodePiece makeInitialCode(const InsertableCode& code);

/// What `code` does where it stands, in a few words: "Red Text", "Next Box
/// after 20 Frames". Read from what `z_message.c` does with it.
[[nodiscard]] std::string describeCode(const CodePiece& code);

} // namespace toki::message

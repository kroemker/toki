#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Charmap.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/ShiftJis.h"
#include "base/Diagnostic.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cparser {
class MacroTable;
class TypeTable;
} // namespace cparser

namespace hackeroot {
class Workspace;
struct Prelude;
} // namespace hackeroot

namespace toki::message {

struct EncodedText {
    std::vector<u8> bytes;
    /// Where in the text each byte was encoded from, byte by byte.
    std::vector<TextPosition> sources;
    std::vector<base::Diagnostic> diagnostics;
};

/// Turns a message's text into the bytes the build compiles it to. Text goes
/// through the charmap, as `tools/msgenc.py` encodes it; for English, German
/// and French what no entry matches stays UTF-8, and for Japanese, encoded
/// with `shiftJis`, every charmap code is two bytes and what no entry matches
/// is its Shift-JIS bytes. A control code goes through the macro
/// `message_data_fmt.h` defines for it under `MESSAGE_DATA_STATIC`, and its
/// arguments are evaluated against the enumerations of the headers that file
/// includes, as the compiler evaluates them. `END` closes every text, as
/// `MSG(...)` closes it.
class MessageEncoder {
public:
    MessageEncoder(const cparser::MacroTable& macros, const cparser::TypeTable& types,
                   const Charmap& charmap, const ShiftJis* shiftJis = nullptr)
        : macros(macros), types(types), charmap(charmap), shiftJis(shiftJis) {}

    [[nodiscard]] EncodedText encode(const MessageText& text) const;

private:
    void encodeText(std::string_view text, usize piece, EncodedText& encoded) const;
    void encodeCode(const CodePiece& code, usize piece, EncodedText& encoded) const;

    const cparser::MacroTable& macros;
    const cparser::TypeTable& types;
    const Charmap& charmap;
    const ShiftJis* shiftJis;
};

/// What an encoder reads from a checkout: `message_data_static.h` and the
/// headers it includes, `message_data_fmt.h` among them, read with
/// `MESSAGE_DATA_STATIC` defined, and the charmap. The prelude's macros layer
/// over the workspace's, so the workspace must outlive it.
struct MessageFormat {
    std::unique_ptr<hackeroot::Prelude> prelude;
    Charmap charmap;
    /// Set for the Japanese format, which encodes two bytes a character.
    std::optional<ShiftJis> shiftJis;

    [[nodiscard]] MessageEncoder makeEncoder() const;

    /// The values of the box `message` asks for, as its constants say.
    [[nodiscard]] std::expected<LayoutRequest, std::string>
    makeLayoutRequest(const Message& message) const;
};

[[nodiscard]] std::expected<MessageFormat, base::Diagnostic>
readMessageFormat(const hackeroot::Workspace& workspace);

/// The format Japanese is encoded in: `charmap.jpn.txt`, and the control codes
/// as `MESSAGE_WCHAR` makes them, two bytes each.
[[nodiscard]] std::expected<MessageFormat, base::Diagnostic>
readWideMessageFormat(const hackeroot::Workspace& workspace, ShiftJis shiftJis);

/// The `.rodata` the build compiles one language of the message table into:
/// every message's encoded text in table order, each starting on a 4-byte
/// boundary as the compiler aligns a `char` array. Only the rows the build puts
/// into that language's table are encoded (`isInLanguage`).
struct EncodedData {
    std::vector<u8> bytes;

    /// Where each message's text starts, by its index in the table; a message
    /// the language leaves out has none.
    std::vector<std::optional<usize>> offsets;

    std::vector<base::Diagnostic> diagnostics;
};

/// Whether `condition`, an `#if` expression, is true under `macros`; one that
/// cannot be evaluated is false.
[[nodiscard]] bool isConditionTrue(const cparser::MacroTable& macros, std::string_view condition);

/// Whether the build puts the font message among the English texts, as PAL
/// builds do, by `OOT_NTSC` as `macros` answer it.
[[nodiscard]] bool isFontMessageEnglish(const cparser::MacroTable& macros);

[[nodiscard]] EncodedData encodeMessageData(const MessageTable& table,
                                            const MessageEncoder& encoder, Language language,
                                            bool includesFontMessage);

} // namespace toki::message

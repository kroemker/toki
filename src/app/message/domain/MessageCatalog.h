#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageEncoder.h"
#include "app/message/domain/MessageGeometry.h"
#include "app/message/domain/MessageLayout.h"
#include "base/Diagnostic.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace hackeroot {
class Workspace;
} // namespace hackeroot

namespace toki::message {

/// The languages a message can be shown and edited in. Japanese, which is
/// encoded two bytes a character and laid out apart, is not one yet.
inline constexpr std::array ShownLanguages = {Language::English, Language::German,
                                              Language::French};

/// Everything the checkout says about its messages that showing one needs:
/// the messages, how they encode, the tables they are laid out with, and the
/// art they are drawn with. Read once, off the UI thread, and replaced whole
/// when one of `files` changes.
struct MessageCatalog {
    /// Kept alive for the format, whose macros layer over the workspace's.
    std::shared_ptr<const hackeroot::Workspace> workspace;
    MessageTable table;
    std::optional<MessageFormat> format;
    MessageGeometry geometry;
    MessageArt art;
    bool isFontMessageEnglish = false;
    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;

    /// The bytes `message` has in `language`, as the build encodes them.
    [[nodiscard]] EncodedText encode(const Message& message, Language language) const;

    /// `message` in `language` laid out box by box; a message whose box the
    /// format cannot name is a layout with a diagnostic saying so.
    [[nodiscard]] MessageLayout layOut(const Message& message, Language language) const;
};

[[nodiscard]] MessageCatalog
readMessageCatalog(const std::shared_ptr<const hackeroot::Workspace>& workspace);

} // namespace toki::message

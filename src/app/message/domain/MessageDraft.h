#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageKey.h"

#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace toki::message {

/// The messages as they are being edited: the table last read, and the edits
/// of each message made since, kept apart so that one message is saved, or
/// thrown away, without the others. A table read again keeps every edit, on
/// the same message found again in it; a message whose edits are kept says
/// when its row was changed in the checkout under them.
class MessageDraft {
public:
    /// Takes `table` as read, every edit kept on top of it. Returns the
    /// messages whose edits were dropped, because the table has them no more.
    std::vector<MessageKey> setRead(const MessageTable& table);

    /// Throws every edit away and goes back to the table last read.
    void discard();

    /// Throws the edits of one message away.
    void discard(usize message);

    /// The table to write when only `messages` are saved: as last read, with
    /// the edits of those messages and no others.
    [[nodiscard]] MessageTable makeSavedTable(std::span<const usize> messages) const;

    /// Takes `messages` of `saved`, the table written to the checkout, as
    /// read: what they were edited to since the save began still counts as
    /// edits.
    void markSaved(const MessageTable& saved, std::span<const usize> messages);

    [[nodiscard]] const MessageTable& getTable() const {
        return table;
    }

    [[nodiscard]] bool isEdited() const {
        return !edited.empty();
    }

    [[nodiscard]] bool isEdited(const usize message) const {
        return edited.contains(message);
    }

    /// The messages with edits, in the table's order.
    [[nodiscard]] std::vector<usize> listEdited() const {
        return {edited.begin(), edited.end()};
    }

    /// Whether the checkout's row of `message` changed since its edits began.
    [[nodiscard]] bool hasReadChanged(const usize message) const {
        return readChanged.contains(message);
    }

    /// A number that changes with every edit and every table taken.
    [[nodiscard]] u64 getRevision() const {
        return revision;
    }

    /// The one message every change since revision `since` was an edit of, if
    /// they all were, so what shows the table can update only its row.
    [[nodiscard]] std::optional<usize> findOnlyEdited(u64 since) const;

    void setText(usize message, Language language, std::vector<Piece> pieces);
    void setBoxType(usize message, std::string boxType);
    void setBoxPosition(usize message, std::string boxPosition);

private:
    void markEdited(usize message);
    void markReplaced();

    MessageTable table;
    MessageTable read;
    std::set<usize> edited;
    std::set<usize> readChanged;
    u64 revision = 0;
    std::optional<usize> editedAlone;
    u64 editedAloneSince = 0;
};

} // namespace toki::message

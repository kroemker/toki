#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageKey.h"

#include <map>
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
/// when its row was changed in the checkout under them. Each message keeps
/// the steps it was edited in, to be undone and redone on their own; a table
/// read again keeps them as it keeps the edits.
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

    /// A number that changes with every edit of `message` and every table
    /// taken, so what shows one message is not made again for the edits of
    /// another.
    [[nodiscard]] u64 getRevision(usize message) const;

    /// A number that changes with every table taken, the only change that
    /// moves a message to another index.
    [[nodiscard]] u64 getTableRevision() const {
        return tableRevision;
    }

    /// The one message every change since revision `since` was an edit of, if
    /// they all were, so what shows the table can update only its row.
    [[nodiscard]] std::optional<usize> findOnlyEdited(u64 since) const;

    /// Each edit is a step undone at once, unless it `joinsLast`, as one key
    /// typed after another does: then it joins the step of the edit before,
    /// when that was of the same message and nothing was undone since. An
    /// edit that changes nothing is no step and no edit.
    void setText(usize message, Language language, std::vector<Piece> pieces,
                 bool joinsLast = false);
    void setBoxType(usize message, std::string boxType);
    void setBoxPosition(usize message, std::string boxPosition);

    [[nodiscard]] bool canUndo(usize message) const;
    [[nodiscard]] bool canRedo(usize message) const;

    /// Takes `message` back to before its last step, or forward again past the
    /// step last undone. False when there is none.
    bool undo(usize message);
    bool redo(usize message);

private:
    struct History {
        std::vector<Message> undone;
        std::vector<Message> redone;
    };

    void markEdited(usize message);
    void markReplaced();
    void beginStep(usize message, bool joinsLast);

    MessageTable table;
    MessageTable read;
    std::set<usize> edited;
    std::set<usize> readChanged;
    u64 revision = 0;
    u64 tableRevision = 0;
    std::map<usize, u64> messageRevisions;
    std::optional<usize> editedAlone;
    u64 editedAloneSince = 0;
    std::map<usize, History> histories;
    std::optional<usize> lastStep;
};

} // namespace toki::message

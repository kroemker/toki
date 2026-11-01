#pragma once

#include "app/message/Types.h"
#include "app/message/domain/MessageRows.h"
#include "app/message/ui/BoxRenderer.h"
#include "app/message/ui/MessageFocus.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace toki::message {

/// The Assets panel's Messages tab: the checkout's messages in a table, the
/// chips that filter it, and the first box of the one selected, drawn as the
/// game draws it. A click on a row shows the message in the Message panel.
class MessageBrowser {
public:
    using ShowMessage = std::function<void(usize)>;

    MessageBrowser(MessageFocus& focus, BoxRenderer& boxRenderer, ShowMessage showMessage);

    void drawChips();
    void drawTable(const std::string& filter, float height);
    void drawDetails();

private:
    struct RowsKey {
        u64 revision = 0;
        Language language = Language::English;
        std::string filter;
        bool showsVanilla = true;
        bool showsOwn = true;
        std::optional<std::string> boxType;
        MessageColumn column = MessageColumn::Id;
        bool ascending = true;

        friend bool operator==(const RowsKey&, const RowsKey&) = default;
    };

    void updateRows(const RowsKey& key);
    void drawRow(const MessageRow& row);

    MessageFocus& focus;
    BoxRenderer& boxRenderer;
    ShowMessage showMessage;
    bool showsVanilla = true;
    bool showsOwn = true;
    std::optional<std::string> boxType;
    std::optional<RowsKey> rowsKey;
    std::vector<MessageRow> rows;
};

} // namespace toki::message

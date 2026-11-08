#pragma once

#include "app/message/Types.h"
#include "app/message/domain/MessageLayout.h"
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
/// game draws it. A click on a row previews the message in the preview tab.
class MessageBrowser {
public:
    /// Shows the message at an index.
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
        std::optional<std::string> boxType;
        MessageColumn column = MessageColumn::Id;
        bool ascending = true;

        friend bool operator==(const RowsKey&, const RowsKey&) = default;
    };

    struct LayoutKey {
        const MessageCatalog* catalog = nullptr;
        u64 revision = 0;
        usize index = 0;
        Language language = Language::English;

        friend bool operator==(const LayoutKey&, const LayoutKey&) = default;
    };

    /// Brings the row of `message`, the only one edited since, up to date:
    /// made again, sorted again where its column changed, put in or taken out
    /// where the filter now lets it through or not.
    void updateRow(const RowsKey& key, usize message);
    void updateRows(const RowsKey& key);
    void drawRow(const MessageRow& row);

    MessageFocus& focus;
    BoxRenderer& boxRenderer;
    ShowMessage showMessage;
    std::optional<std::string> boxType;
    std::optional<RowsKey> rowsKey;
    std::vector<MessageRow> rows;
    std::optional<LayoutKey> layoutKey;
    MessageLayout layout;
};

} // namespace toki::message

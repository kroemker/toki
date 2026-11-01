#pragma once

#include "app/core/ui/Panel.h"
#include "app/core/ui/PropertySource.h"
#include "app/message/Types.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageMarkup.h"
#include "app/message/ui/BoxRenderer.h"
#include "app/message/ui/MessageFocus.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gfx {
class Device;
} // namespace gfx

namespace toki::message {

/// The message shown, every box of it in turn as the game draws it, each
/// with how far its lines reach into the box. Its form in the Properties
/// panel says what the message is and where it is written.
class MessagePanel : public core::Panel, public core::PropertySource {
public:
    MessagePanel(const gfx::Device& device, MessageFocus& focus, std::function<void()> save);

    [[nodiscard]] bool isOpenByDefault() const override {
        return false;
    }

    /// Opens the panel on top of its dock, showing the message `focus` shows.
    void showMessage();

    void draw() override;

    [[nodiscard]] std::optional<u64> findSelectionKey() const override;
    void drawProperties() override;

private:
    struct ShownKey {
        const MessageCatalog* catalog = nullptr;
        u64 revision = 0;
        usize index = 0;
        Language language = Language::English;

        friend bool operator==(const ShownKey&, const ShownKey&) = default;
    };

    [[nodiscard]] ShownKey makeShownKey() const;
    void drawHeader(const Message& message);
    void drawEditState();
    void drawBox(usize index, const LaidBox* box);
    void drawEditor(usize index);
    [[nodiscard]] const MessageLayout& updateLayout(const Message& message);
    void updateEditor(const Message& message);
    void applyBoxes();

    MessageFocus& focus;
    std::function<void()> save;
    BoxRenderer boxRenderer;
    std::optional<ShownKey> layoutKey;
    MessageLayout layout;
    std::optional<ShownKey> editorKey;
    std::vector<BoxText> boxes;
    std::vector<std::string> markups;
    std::vector<std::optional<MarkupError>> markupErrors;
};

} // namespace toki::message

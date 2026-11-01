#pragma once

#include "app/core/ui/Panel.h"
#include "app/core/ui/PropertySource.h"
#include "app/message/Types.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/ui/BoxRenderer.h"
#include "app/message/ui/MessageFocus.h"

#include <optional>

namespace gfx {
class Device;
} // namespace gfx

namespace toki::message {

/// The message shown, every box of it in turn as the game draws it, each
/// with how far its lines reach into the box. Its form in the Properties
/// panel says what the message is and where it is written.
class MessagePanel : public core::Panel, public core::PropertySource {
public:
    MessagePanel(const gfx::Device& device, MessageFocus& focus);

    [[nodiscard]] bool isOpenByDefault() const override {
        return false;
    }

    /// Opens the panel on top of its dock, showing the message `focus` shows.
    void showMessage();

    void draw() override;

    [[nodiscard]] std::optional<u64> findSelectionKey() const override;
    void drawProperties() override;

private:
    struct LayoutKey {
        const MessageCatalog* catalog = nullptr;
        usize index = 0;
        Language language = Language::English;

        friend bool operator==(const LayoutKey&, const LayoutKey&) = default;
    };

    void drawHeader(const Message& message);
    void drawBox(usize index, const LaidBox& box);
    [[nodiscard]] const MessageLayout& updateLayout(const Message& message);

    MessageFocus& focus;
    BoxRenderer boxRenderer;
    std::optional<LayoutKey> layoutKey;
    MessageLayout layout;
};

} // namespace toki::message

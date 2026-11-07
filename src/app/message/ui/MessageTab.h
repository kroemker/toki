#pragma once

#include "app/core/ui/AssetTab.h"
#include "app/core/ui/Fonts.h"
#include "app/message/Types.h"
#include "app/message/domain/EditableBox.h"
#include "app/message/domain/MessageKey.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageMarkup.h"
#include "app/message/ui/BoxRenderer.h"
#include "app/message/ui/MessageFocus.h"
#include "app/message/ui/SignGlyphs.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gfx {
class Device;
} // namespace gfx

namespace toki::message {

/// One message open in the Message panel: every box of it in turn as the game
/// draws it, each with how far its lines reach into the box. A box is typed in
/// where it is drawn, with a caret where the game's pen stands, or edited as
/// markup beside or under it; the rail at the left puts signs, colors and
/// codes in where the caret is. Its form in the Properties panel says what the
/// message is and where it is written.
class MessageTab : public core::AssetTab {
public:
    /// Saves the edits of the messages at the indices given.
    using SaveMessages = std::function<void(std::vector<usize>, core::AssetTab::Saved)>;

    MessageTab(const gfx::Device& device, const core::Fonts& fonts, MessageFocus& focus,
               SignGlyphs& signGlyphs, MessageKey key, SaveMessages saveMessages);

    /// The index of the message in the draft's table, if it is still there.
    [[nodiscard]] std::optional<usize> findIndex() const;

    [[nodiscard]] std::string getTitle() const override;
    [[nodiscard]] bool isEdited() const override;
    void save(Saved saved) override;
    void discard() override;
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

    /// How the boxes are shown: their scale, window pixels a game pixel, and
    /// whether each box's markup stands beside it rather than under it.
    struct Arrangement {
        f32 scale = 1.0F;
        bool isBeside = false;
    };

    [[nodiscard]] ShownKey makeShownKey(usize index) const;
    void drawHeader(const Message& message);
    void drawZoomMenu();
    void drawEditState(usize index);
    void drawRail();
    void drawStoryboard(const MessageLayout& laidOut);
    void drawBox(usize index, const LaidBox* box, const Arrangement& arrangement);
    void drawBoxImage(usize index, const LaidBox& box, f32 width);
    void drawEditor(usize index, f32 width);
    void zoomByWheel();
    [[nodiscard]] Arrangement findArrangement(f32 width) const;
    void editInBox(usize index, const LaidBox& box, const BoxImage& image);
    void insertIntoTyped(std::span<const Piece> pieces);
    [[nodiscard]] const MessageLayout& updateLayout(usize index, const Message& message);
    void updateEditor(usize index, const Message& message);
    void applyEditable(usize index);
    void applyBoxes();

    const core::Fonts& fonts;
    MessageFocus& focus;
    SignGlyphs& signGlyphs;
    MessageKey key;
    SaveMessages saveMessages;
    BoxRenderer boxRenderer;
    mutable std::optional<u64> indexRevision;
    mutable const MessageCatalog* indexCatalog = nullptr;
    mutable std::optional<usize> index;
    std::optional<ShownKey> layoutKey;
    MessageLayout layout;
    std::vector<TextPosition> boxSources;
    std::optional<ShownKey> editorKey;
    std::vector<BoxText> boxes;
    std::vector<std::string> markups;
    std::vector<std::optional<MarkupError>> markupErrors;
    std::vector<EditableBox> editables;
    std::optional<usize> typedBox;
    bool isDragging = false;
    /// The scale the boxes were last shown at, which zooming steps from.
    f32 shownScale = 1.0F;
};

} // namespace toki::message

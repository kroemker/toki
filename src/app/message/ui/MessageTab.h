#pragma once

#include "app/core/ui/AssetTab.h"
#include "app/core/ui/Fonts.h"
#include "app/message/Types.h"
#include "app/message/domain/EditableBox.h"
#include "app/message/domain/MessageCodes.h"
#include "app/message/domain/MessageKey.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageMarkup.h"
#include "app/message/ui/BoxMarkers.h"
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

/// One message open in a tab: every box of it in turn as the game
/// draws it, each with how far its lines reach into the box. A box is typed in
/// where it is drawn, with a caret where the game's pen stands; the rail at
/// the left puts signs, colors and codes in where the caret is, and a color
/// over a selection draws it in that color. The header sets the box's type and
/// position, and switches to the whole message written as markup.
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

private:
    struct ShownKey {
        const MessageCatalog* catalog = nullptr;
        u64 revision = 0;
        usize index = 0;
        Language language = Language::English;

        friend bool operator==(const ShownKey&, const ShownKey&) = default;
    };

    /// The code of a box whose arguments the code popup edits.
    struct EditedCode {
        usize box = 0;
        usize unit = 0;
    };

    /// The markers of one place, listed while the mouse is over them.
    struct MarkerList {
        usize box = 0;
        std::vector<BoxMarker> markers;
        MarkerShape shape;
    };

    [[nodiscard]] ShownKey makeShownKey(usize index) const;
    void drawHeader(usize index, const Message& message);
    void drawZoomMenu();
    void showMarkup(bool isShown);
    void drawEditState(usize index);
    /// What is under the header: the boxes and the rail, or the markup.
    void drawBody(usize index, const Message& message);
    void drawRail();
    [[nodiscard]] f32 measureRail() const;
    void drawStoryboard(const MessageLayout& laidOut);
    void drawBox(usize index, const LaidBox* box, f32 scale);
    void drawBoxImage(usize index, const LaidBox& box, f32 width);
    void drawMarkup();
    void zoomByWheel();
    /// The boxes' scale, window pixels a game pixel, in a width of `width`.
    [[nodiscard]] f32 findScale(f32 width) const;
    void editInBox(usize index, const LaidBox& box, const BoxImage& image);
    /// Selects the marker clicked in box `index`, if one was, and opens the
    /// code popup for one with arguments. A group under the mouse opens its
    /// list. True when a click landed on a marker.
    [[nodiscard]] bool selectMarker(usize index, std::span<const MarkerGroup> groups);
    /// Selects the code at `unit` of box `index`, as a click on what marks
    /// or draws it does, and opens its arguments when it has any.
    void selectCode(usize index, usize unit);
    void drawMarkerList();
    void handleHotkeys();
    /// Makes the editables again from `boxes`, written to the draft first,
    /// with the caret at `caret` in box `typed`.
    void rebuildEditables(std::optional<usize> typed, usize caret);
    void addBox();
    void splitTypedBox();
    void deleteTypedBox();
    void applyColor(usize color);
    void insertCode(const InsertableCode& insertable);
    void openCodePopup(usize box, usize unit);
    void drawCodePopup();
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
    std::string markup;
    std::optional<MarkupError> markupError;
    std::vector<EditableBox> editables;
    std::optional<usize> typedBox;
    bool isDragging = false;
    bool wasMarkerClicked = false;
    bool isMarkupShown = false;
    std::optional<EditedCode> editedCode;
    std::optional<MarkerList> markerList;
    bool opensMarkerList = false;
    /// The frame the code popup was asked for in. It opens on the next: a
    /// popup opened in the frame of a mouse press is closed at that frame's
    /// end by the press, which Dear ImGui takes for a click outside it.
    std::optional<int> codePopupFrame;
    std::string argumentFilter;
    /// The scale the boxes were last shown at, which zooming steps from.
    f32 shownScale = 1.0F;
};

} // namespace toki::message

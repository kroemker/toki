#include "MessageTab.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/domain/ReadableName.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/ConstantSelectable.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Spinner.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/Charmap.h"
#include "app/message/domain/EditableBox.h"
#include "app/message/domain/MessageCatalog.h"
#include "app/message/domain/MessageCodes.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageMarkup.h"
#include "app/message/domain/MessageRows.h"
#include "app/message/ui/CodeArguments.h"
#include "app/message/ui/MarkupEditor.h"
#include "base/Diagnostic.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

constexpr f32 BoxImageWidth = 272.0F;
constexpr f32 ZoomStep = 0.5F;
constexpr f32 MinZoom = 1.0F;
constexpr f32 MaxZoom = 8.0F;
constexpr f32 MaxFitScale = 6.0F;
constexpr f32 GlyphEms = 1.6F;
constexpr f32 BoxComboEms = 9.0F;
constexpr f32 LanguageComboEms = 8.0F;
constexpr f32 ZoomComboEms = 7.0F;
constexpr std::array ZoomChoices = {1.0F, 1.5F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
constexpr float MeterHeightLines = 0.35F;
constexpr f32 SelectedLineEndWidth = 3.0F;
constexpr f32 TypedFrameThickness = 2.0F;
constexpr f32 ScreenWidth = 320.0F;
constexpr f32 ScreenHeight = 240.0F;
constexpr double CaretBlinkPeriod = 1.2;
constexpr double CaretBlinkShown = 0.8;
constexpr ImWchar FirstTypedCharacter = 0x20;
constexpr ImWchar DeleteCharacter = 0x7F;
constexpr usize Utf8Capacity = 5;
constexpr std::array ColorActions = {
    core::Action::TextColor1, core::Action::TextColor2, core::Action::TextColor3,
    core::Action::TextColor4, core::Action::TextColor5, core::Action::TextColor6,
    core::Action::TextColor7, core::Action::TextColor8,
};

[[nodiscard]] std::string makeSignLabel(const std::string_view sign) {
    if (sign.size() > 2 && sign.front() == '[' && sign.back() == ']') {
        return std::string(sign.substr(1, sign.size() - 2));
    }
    return std::string(sign);
}

void moveVertically(EditableBox& editable, const std::vector<CaretPlace>& places,
                    const LaidBox& box, const s32 direction, const bool extends) {
    const CaretPlace& at = places[editable.getCaret()];
    const bool hasLine = std::ranges::any_of(places, [&](const CaretPlace& place) {
        return direction < 0 ? place.y < at.y : place.y > at.y;
    });
    if (!hasLine) {
        editable.setCaret(direction < 0 ? 0 : editable.getUnits().size(), extends);
        return;
    }
    const s32 height = findCharacterHeight(box);
    const f32 y = static_cast<f32>(at.y + (direction * findLineSpacing(box))) +
                  (static_cast<f32>(height) / 2.0F);
    editable.setCaret(findPlaceAt(places, height, static_cast<f32>(at.x), y), extends);
}

void drawCaret(const EditableBox& editable, const std::vector<CaretPlace>& places,
               const std::span<const DrawnCode> drawnCodes, const LaidBox& box,
               const BoxImage& image) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float height = static_cast<float>(findCharacterHeight(box)) * image.scale;
    const UnitRange selection = editable.getSelection();
    for (usize unit = selection.begin; unit < selection.end; ++unit) {
        const auto drawn = std::ranges::find(drawnCodes, unit, &DrawnCode::unit);
        if (drawn != drawnCodes.end()) {
            drawList->AddRectFilled(image.findScreenPosition(static_cast<f32>(drawn->left),
                                                             static_cast<f32>(drawn->top)),
                                    image.findScreenPosition(static_cast<f32>(drawn->right),
                                                             static_cast<f32>(drawn->bottom)),
                                    ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
            continue;
        }
        const CaretPlace& from = places[unit];
        const CaretPlace& to = places[unit + 1];
        const f32 end = to.y == from.y && to.x > from.x
                            ? static_cast<f32>(to.x)
                            : static_cast<f32>(from.x) + SelectedLineEndWidth;
        const ImVec2 start =
            image.findScreenPosition(static_cast<f32>(from.x), static_cast<f32>(from.y));
        const ImVec2 stop = image.findScreenPosition(end, static_cast<f32>(from.y));
        drawList->AddRectFilled(start, ImVec2(stop.x, stop.y + height),
                                ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
    }
    const CaretPlace& caret = places[editable.getCaret()];
    const ImVec2 top =
        image.findScreenPosition(static_cast<f32>(caret.x), static_cast<f32>(caret.y));
    const bool isShown = !ImGui::GetIO().ConfigInputTextCursorBlink ||
                         std::fmod(ImGui::GetTime(), CaretBlinkPeriod) < CaretBlinkShown;
    if (isShown) {
        drawList->AddLine(top, ImVec2(top.x, top.y + height),
                          ImGui::GetColorU32(core::palette::Accent),
                          std::max(1.5F, image.scale * 0.6F));
    }
    ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
    ime.WantVisible = true;
    ime.WantTextInput = true;
    ime.InputPos = top;
    ime.InputLineHeight = height;
    ime.ViewportId = ImGui::GetWindowViewport()->ID;
}

void moveCaretByKeys(EditableBox& editable, const std::vector<CaretPlace>& places,
                     const LaidBox& box) {
    const bool extends = ImGui::GetIO().KeyShift;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        editable.moveLeft(extends);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        editable.moveRight(extends);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        editable.moveToLineStart(extends);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        editable.moveToLineEnd(extends);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        moveVertically(editable, places, box, -1, extends);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        moveVertically(editable, places, box, 1, extends);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
        editable.selectAll();
    }
}

[[nodiscard]] bool editByClipboard(EditableBox& editable) {
    const bool isCopied = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C);
    const bool isCut = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X);
    if ((isCopied || isCut) && editable.hasSelection()) {
        ImGui::SetClipboardText(formatMarkup(editable.copySelection()).c_str());
        if (isCut) {
            editable.eraseBackward();
            return true;
        }
    }
    if (!ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
        return false;
    }
    const char* clipboard = ImGui::GetClipboardText();
    if (clipboard == nullptr) {
        return false;
    }
    const std::expected<std::vector<Piece>, MarkupError> pasted = parseMarkup(clipboard);
    if (pasted) {
        editable.insert(*pasted);
    } else {
        editable.insertText(clipboard);
    }
    return true;
}

[[nodiscard]] std::string readTypedText() {
    const ImGuiIO& io = ImGui::GetIO();
    std::string typed;
    if (io.KeyCtrl) {
        return typed;
    }
    for (const ImWchar character : io.InputQueueCharacters) {
        if (character < FirstTypedCharacter || character == DeleteCharacter) {
            continue;
        }
        std::array<char, Utf8Capacity> encoded{};
        const int size = ImTextCharToUtf8(encoded.data(), character);
        typed.append(encoded.data(), static_cast<usize>(size));
    }
    return typed;
}

[[nodiscard]] bool editByKeys(EditableBox& editable) {
    bool isChanged = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        editable.eraseBackward();
        isChanged = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        editable.eraseForward();
        isChanged = true;
    }
    if (!ImGui::GetIO().KeyCtrl &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        const std::array<Piece, 1> newline = {NewlinePiece{}};
        editable.insert(newline);
        isChanged = true;
    }
    isChanged = editByClipboard(editable) || isChanged;
    const std::string typed = readTypedText();
    if (!typed.empty()) {
        editable.insertText(typed);
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] const char* describeMacro(const MessageMacro macro) {
    switch (macro) {
    case MessageMacro::Message:
        return "In Every Language";
    case MessageMacro::JapaneseOnly:
        return "In Japanese Only";
    case MessageMacro::NesOnly:
        return "In English, German and French Only";
    case MessageMacro::FontMessage:
        return "The Font Message";
    }
    return "";
}

[[nodiscard]] const char* describeEndIcon(const EndIcon icon) {
    switch (icon) {
    case EndIcon::None:
        return "No Prompt";
    case EndIcon::Triangle:
        return "Continue Prompt";
    case EndIcon::Square:
        return "Close Prompt";
    case EndIcon::Arrow:
        return "Choice Prompt";
    }
    return "";
}

[[nodiscard]] bool hasTextSelected(const EditableBox& editable) {
    const UnitRange selection = editable.getSelection();
    const std::span<const BoxUnit> units = editable.getUnits();
    for (usize unit = selection.begin; unit < selection.end; ++unit) {
        const auto* code = std::get_if<CodePiece>(&units[unit].piece);
        if (code == nullptr || code->name != "COLOR") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool drawToggleButton(const char* label, const bool isOn) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          isOn ? core::palette::AccentMuted : core::palette::SurfaceRaised);
    ImGui::PushStyleColor(ImGuiCol_Text, isOn ? core::palette::Text : core::palette::TextMuted);
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(2);
    return clicked;
}

[[nodiscard]] f32 measureToggleButton(const char* label) {
    return ImGui::CalcTextSize(label).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
}

void setShortcutTooltip(const std::string_view text, const core::Action action) {
    ImGui::SetItemTooltip("%s", core::makeShortcutText(text, action).c_str());
}

void drawMeter(const LaidBox& box, const LaidLine& line, const float width) {
    const s32 start = findLineStart(box);
    const s32 margin = findLineLimit(box);
    const s32 edge = findBoxRightEdge(box);
    const s32 room = edge - start;
    const s32 used = line.endX - start;
    const float fraction = room > 0 ? static_cast<float>(used) / static_cast<float>(room) : 0.0F;
    ImVec4 color = core::palette::Success;
    if (line.endX > edge) {
        color = core::palette::Danger;
    } else if (line.endX > margin) {
        color = core::palette::Warning;
    }
    const float height = ImGui::GetTextLineHeight() * MeterHeightLines;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                            ImGui::GetColorU32(core::palette::SurfaceRaised));
    drawList->AddRectFilled(
        origin, ImVec2(origin.x + width * std::clamp(fraction, 0.0F, 1.0F), origin.y + height),
        ImGui::GetColorU32(color));
    const float marginX =
        origin.x + (width * static_cast<float>(margin - start) / static_cast<float>(room));
    drawList->AddLine(ImVec2(marginX, origin.y), ImVec2(marginX, origin.y + height),
                      ImGui::GetColorU32(core::palette::TextMuted));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%d of %d pixels", used, room);
    }
}

[[nodiscard]] bool drawConstantCombo(const char* id, std::string& value,
                                     const std::vector<std::string>& options,
                                     const std::string_view prefix, const f32 width) {
    bool changed = false;
    const std::string shown = core::makeConstantLabel(value, prefix);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, shown.c_str())) {
        for (const std::string& option : options) {
            const std::string label = core::makeConstantLabel(option, prefix);
            if (core::drawConstantSelectable(label, option, option == value)) {
                value = option;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

[[nodiscard]] CodePiece makeCodeToInsert(const InsertableCode& insertable,
                                         const MessageCatalog& catalog) {
    CodePiece code = makeInitialCode(insertable);
    for (usize index = 0; index < insertable.arguments.size(); ++index) {
        if (!code.arguments[index].empty()) {
            continue;
        }
        const std::vector<std::string>* options = nullptr;
        switch (insertable.arguments[index].kind) {
        case ArgumentKind::Item:
            options = &catalog.items;
            break;
        case ArgumentKind::HighScore:
            options = &catalog.highScores;
            break;
        case ArgumentKind::Sound:
            options = &catalog.sounds;
            break;
        case ArgumentKind::Number:
        case ArgumentKind::MessageId:
            break;
        }
        if (options != nullptr && !options->empty()) {
            code.arguments[index] = options->front();
        }
    }
    return code;
}

void drawScreenPlace(const LaidBox* box) {
    const f32 height = ImGui::GetFrameHeight();
    const f32 width = height * ScreenWidth / ScreenHeight;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + width, origin.y + height);
    drawList->AddRectFilled(origin, end, ImGui::GetColorU32(core::palette::SurfaceRaised));
    drawList->AddRect(origin, end, ImGui::GetColorU32(core::palette::TextMuted));
    if (box == nullptr) {
        return;
    }
    const f32 scale = height / ScreenHeight;
    const ImVec2 boxMin(origin.x + (static_cast<f32>(box->x) * scale),
                        origin.y + (static_cast<f32>(box->y) * scale));
    const ImVec2 boxMax(boxMin.x + (static_cast<f32>(BoxWidth) * scale),
                        boxMin.y + (static_cast<f32>(BoxHeight) * scale));
    drawList->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(core::palette::Accent));
}

[[nodiscard]] f32 findStep(const f32 scale) {
    return std::floor(scale / ZoomStep) * ZoomStep;
}

[[nodiscard]] std::string formatScale(const f32 scale) {
    return std::format("{:.0f}%", scale * 100.0F);
}

[[nodiscard]] ImVec4 makeColor(const TextColor& color) {
    constexpr f32 Full = 255.0F;
    return {static_cast<f32>(color.red) / Full, static_cast<f32>(color.green) / Full,
            static_cast<f32>(color.blue) / Full, 1.0F};
}

} // namespace

MessageTab::MessageTab(const gfx::Device& device, const core::Fonts& fonts, MessageFocus& focus,
                       SignGlyphs& signGlyphs, MessageKey key, SaveMessages saveMessages)
    : fonts(fonts), focus(focus), signGlyphs(signGlyphs), key(key),
      saveMessages(std::move(saveMessages)), boxRenderer(device) {}

std::optional<usize> MessageTab::findIndex() const {
    if (!focus.catalog) {
        return std::nullopt;
    }
    if (indexRevision != focus.draft.getRevision() || indexCatalog != focus.catalog.get()) {
        indexRevision = focus.draft.getRevision();
        indexCatalog = focus.catalog.get();
        index = findMessage(focus.draft.getTable(), key);
    }
    return index;
}

std::string MessageTab::getTitle() const {
    std::string title = std::format("Message 0x{:04X}", key.id);
    if (key.macro == MessageMacro::JapaneseOnly) {
        title += " (Japanese Only)";
    } else if (key.macro == MessageMacro::NesOnly) {
        title += " (No Japanese)";
    }
    if (key.occurrence > 0) {
        title += std::format(" #{}", key.occurrence + 1);
    }
    return title;
}

bool MessageTab::isEdited() const {
    const std::optional<usize> found = findIndex();
    return found && focus.draft.isEdited(*found);
}

void MessageTab::save(Saved saved) {
    const std::optional<usize> found = findIndex();
    if (!found || !saveMessages) {
        if (saved) {
            saved(false);
        }
        return;
    }
    saveMessages({*found}, std::move(saved));
}

void MessageTab::discard() {
    if (const std::optional<usize> found = findIndex()) {
        focus.draft.discard(*found);
        editorKey.reset();
    }
}

MessageTab::ShownKey MessageTab::makeShownKey(const usize shown) const {
    return ShownKey{.catalog = focus.catalog.get(),
                    .revision = focus.draft.getRevision(),
                    .index = shown,
                    .language = focus.language};
}

const MessageLayout& MessageTab::updateLayout(const usize shown, const Message& message) {
    const ShownKey shownKey = makeShownKey(shown);
    if (layoutKey != shownKey) {
        layoutKey = shownKey;
        layout = focus.catalog->layOut(message, focus.language);
        boxSources =
            localizeSources(message.texts[static_cast<usize>(focus.language)], layout.sources);
    }
    return layout;
}

void MessageTab::updateEditor(const usize shown, const Message& message) {
    const ShownKey shownKey = makeShownKey(shown);
    const bool isOtherMessage = !editorKey || editorKey->catalog != shownKey.catalog ||
                                editorKey->index != shownKey.index ||
                                editorKey->language != shownKey.language;
    if (editorKey == shownKey || (!isOtherMessage && ImGui::IsAnyItemActive())) {
        return;
    }
    editorKey = shownKey;
    const MessageText& text = message.texts[static_cast<usize>(focus.language)];
    boxes = splitBoxes(text);
    markup = formatMarkup(text.pieces);
    markupError.reset();

    const usize typed = typedBox.value_or(editables.size());
    const bool keepsCaret = !isOtherMessage && typed < editables.size() && typed < boxes.size();
    const UnitRange caret = keepsCaret ? UnitRange{.begin = editables[typed].getAnchor(),
                                                   .end = editables[typed].getCaret()}
                                       : UnitRange{};
    const Charmap* charmap = focus.catalog->findCharmap(focus.language);
    editables.clear();
    for (const BoxText& box : boxes) {
        editables.emplace_back(box.pieces, charmap);
    }
    if (keepsCaret) {
        editables[typed].setCaret(caret.begin, false);
        editables[typed].setCaret(caret.end, true);
    } else {
        typedBox.reset();
    }
}

void MessageTab::applyEditable(const usize box) {
    boxes[box].pieces = editables[box].makePieces();
    applyBoxes();
}

void MessageTab::applyBoxes() {
    if (const std::optional<usize> found = findIndex()) {
        focus.draft.setText(*found, focus.language, joinBoxes(boxes));
    }
}

void MessageTab::insertIntoTyped(const std::span<const Piece> pieces) {
    if (!typedBox || *typedBox >= editables.size()) {
        return;
    }
    editables[*typedBox].insert(pieces);
    applyEditable(*typedBox);
}

void MessageTab::drawHeader(const usize shown, const Message& message) {
    const f32 em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("0x%04X", message.id);
    if (ImGui::BeginItemTooltip()) {
        const std::string file = core::encodeUtf8(
            message.location.file.lexically_relative(focus.catalog->workspace->getRoot()));
        ImGui::TextUnformatted(describeMacro(message.macro));
        ImGui::TextColored(core::palette::TextMuted, "%s:%u", file.c_str(),
                           static_cast<unsigned>(message.location.line));
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    std::string type = message.boxType;
    if (drawConstantCombo("##boxType", type, listBoxTypes(focus.draft.getTable()), "TEXTBOX_TYPE_",
                          em * BoxComboEms)) {
        focus.draft.setBoxType(shown, std::move(type));
    }
    ImGui::SameLine();
    std::string position = message.boxPosition;
    if (drawConstantCombo("##boxPosition", position, listBoxPositions(focus.draft.getTable()),
                          "TEXTBOX_POS_", em * BoxComboEms)) {
        focus.draft.setBoxPosition(shown, std::move(position));
    }
    ImGui::SameLine();
    drawScreenPlace(layout.boxes.empty() ? nullptr : layout.boxes.data());

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 comboWidth = em * LanguageComboEms;
    const f32 zoomWidth = em * ZoomComboEms;
    const f32 viewWidth = measureToggleButton("Render") + measureToggleButton("Markup");
    const f32 boxesWidth = zoomWidth + measureToggleButton(core::icons::Ruler) +
                           measureToggleButton(core::icons::MapPin) + (style.ItemSpacing.x * 2.0F) +
                           style.ItemInnerSpacing.x;
    const f32 rightWidth =
        viewWidth + comboWidth + style.ItemSpacing.x + (isMarkupShown ? 0.0F : boxesWidth);
    ImGui::SameLine();
    ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - rightWidth, ImGui::GetCursorPosX()));
    if (drawToggleButton("Render", !isMarkupShown)) {
        showMarkup(false);
    }
    setShortcutTooltip("Render", core::Action::ShowMessageRender);
    ImGui::SameLine(0.0F, 0.0F);
    if (drawToggleButton("Markup", isMarkupShown)) {
        showMarkup(true);
    }
    setShortcutTooltip("Markup", core::Action::ShowMessageMarkup);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboWidth);
    const std::vector<Language>& languages = focus.catalog->languages;
    if (!languages.empty() && !std::ranges::contains(languages, focus.language)) {
        focus.language = languages.front();
    }
    if (ImGui::BeginCombo("##language", getLanguageName(focus.language))) {
        for (const Language language : languages) {
            if (ImGui::Selectable(getLanguageName(language), language == focus.language)) {
                focus.language = language;
            }
        }
        ImGui::EndCombo();
    }
    if (!isMarkupShown) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(zoomWidth);
        drawZoomMenu();
        ImGui::SameLine();
        if (drawToggleButton(core::icons::Ruler, focus.showsMeters)) {
            focus.showsMeters = !focus.showsMeters;
        }
        setShortcutTooltip("Meters", core::Action::ToggleMeters);
        ImGui::SameLine(0.0F, style.ItemInnerSpacing.x);
        if (drawToggleButton(core::icons::MapPin, focus.showsCodes)) {
            focus.showsCodes = !focus.showsCodes;
        }
        setShortcutTooltip("Control Codes", core::Action::ToggleControlCodes);
    }
}

void MessageTab::drawZoomMenu() {
    const std::string shown = focus.zoom > 0.0F ? formatScale(focus.zoom)
                                                : std::format("Fit · {}", formatScale(shownScale));
    if (ImGui::BeginCombo("##zoom", shown.c_str())) {
        if (ImGui::Selectable("Fit", focus.zoom <= 0.0F)) {
            focus.zoom = 0.0F;
        }
        for (const f32 scale : ZoomChoices) {
            if (ImGui::Selectable(formatScale(scale).c_str(), focus.zoom == scale)) {
                focus.zoom = scale;
            }
        }
        ImGui::EndCombo();
    }
}

void MessageTab::showMarkup(const bool isShown) {
    isMarkupShown = isShown;
    if (isShown) {
        typedBox.reset();
    }
}

void MessageTab::drawEditState(const usize shown) {
    if (focus.draft.hasReadChanged(shown)) {
        ImGui::TextColored(core::palette::Warning,
                           "%s The checkout's text of this message changed since it was edited "
                           "here; saving writes these edits over it.",
                           core::icons::Warning);
    }
}

void MessageTab::drawRail() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 glyphSize = ImGui::GetFontSize() * GlyphEms;
    const f32 buttonSize = glyphSize + (style.FramePadding.y * 2.0F);
    const f32 cellWidth = glyphSize + (style.FramePadding.x * 2.0F);
    const f32 railWidth = ImGui::GetContentRegionAvail().x;
    const usize columns = std::max<usize>(1, static_cast<usize>((railWidth + style.ItemSpacing.x) /
                                                                (cellWidth + style.ItemSpacing.x)));
    const bool canType = typedBox && *typedBox < editables.size();
    const Charmap* charmap = focus.catalog->findCharmap(focus.language);
    const ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const MessageArt& art = focus.catalog->art;
    const bool checksGlyphs = focus.language != Language::Japanese && hasFont(art);

    if (ImGui::Button(std::format("{} Add Box", core::icons::Plus).c_str(),
                      ImVec2(railWidth, 0.0F))) {
        addBox();
    }
    ImGui::BeginDisabled(!canType || boxes.size() <= 1);
    if (ImGui::Button(std::format("{} Delete Box", core::icons::Trash).c_str(),
                      ImVec2(railWidth, 0.0F))) {
        deleteTypedBox();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s",
                          core::makeShortcutText("Delete Box", core::Action::DeleteBox).c_str());
    }
    ImGui::Separator();

    ImGui::BeginDisabled(!canType);
    if (charmap != nullptr) {
        usize column = 0;
        for (const CharmapEntry* sign : charmap->listSigns()) {
            if (checksGlyphs && !hasFontGlyph(art, sign->code)) {
                continue;
            }
            if (column % columns != 0) {
                ImGui::SameLine();
            }
            ++column;
            ImGui::PushID(sign->text.c_str());
            const gfx::ImageTexture* glyph = signGlyphs.findGlyph(
                art, focus.catalog->geometry, focus.language == Language::Japanese, sign->code);
            bool isClicked = false;
            if (glyph != nullptr) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                const auto texture =
                    static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(glyph->getTexture()));
                isClicked =
                    ImGui::ImageButton("##sign", texture, ImVec2(glyphSize, glyphSize),
                                       ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
            } else {
                isClicked =
                    ImGui::Button(makeSignLabel(sign->text).c_str(), ImVec2(cellWidth, buttonSize));
            }
            if (glyph != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                                                         ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", makeSignLabel(sign->text).c_str());
            }
            if (isClicked && canType) {
                editables[*typedBox].insertText(sign->text);
                applyEditable(*typedBox);
            }
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    const u8 boxType =
        canType && *typedBox < layout.boxes.size() ? layout.boxes[*typedBox].boxType : u8{0};
    const std::vector<std::string>& colors = focus.catalog->colors;
    for (usize color = 0; color < colors.size(); ++color) {
        if (color % columns != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(color));
        const TextColor shade =
            color == 0 ? findDefaultColor(boxType) : findTextColor(boxType, static_cast<u8>(color));
        if (ImGui::ColorButton("##color", makeColor(shade), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(cellWidth, buttonSize))) {
            applyColor(color);
        }
        if (color < ColorActions.size() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                                 ImGuiHoveredFlags_DelayNormal)) {
            const std::string name =
                makeCodeName(CodePiece{.name = "COLOR", .arguments = {colors[color]}});
            ImGui::SetTooltip("%s", core::makeShortcutText(name, ColorActions[color]).c_str());
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    for (const InsertableCode& insertable : listInsertableCodes()) {
        const std::string label = makeCodeName(CodePiece{.name = std::string(insertable.macro)});
        if (ImGui::Button(label.c_str(), ImVec2(railWidth, 0.0F))) {
            insertCode(insertable);
        }
    }
    ImGui::EndDisabled();
}

f32 MessageTab::measureRail() const {
    const ImGuiStyle& style = ImGui::GetStyle();
    f32 width = measureToggleButton(std::format("{} Delete Box", core::icons::Trash).c_str());
    for (const InsertableCode& insertable : listInsertableCodes()) {
        const std::string label = makeCodeName(CodePiece{.name = std::string(insertable.macro)});
        width = std::max(width, measureToggleButton(label.c_str()));
    }
    return width + (style.WindowPadding.x * 2.0F) + style.ScrollbarSize;
}

void MessageTab::rebuildEditables(const std::optional<usize> typed, const usize caret) {
    applyBoxes();
    const Charmap* charmap = focus.catalog->findCharmap(focus.language);
    editables.clear();
    for (const BoxText& box : boxes) {
        editables.emplace_back(box.pieces, charmap);
    }
    markup = formatMarkup(joinBoxes(boxes));
    markupError.reset();
    typedBox = typed && *typed < editables.size() ? typed : std::nullopt;
    if (typedBox) {
        editables[*typedBox].setCaret(caret, false);
    }
    if (const std::optional<usize> found = findIndex()) {
        editorKey = makeShownKey(*found);
    }
}

void MessageTab::addBox() {
    if (boxes.empty()) {
        return;
    }
    const usize after = typedBox && *typedBox < boxes.size() ? *typedBox : boxes.size() - 1;
    BoxText added{.boxBreak = boxes[after].boxBreak};
    boxes[after].boxBreak = CodePiece{.name = "BOX_BREAK"};
    boxes.insert(boxes.begin() + static_cast<std::ptrdiff_t>(after + 1), std::move(added));
    rebuildEditables(after + 1, 0);
}

void MessageTab::splitTypedBox() {
    if (!typedBox || *typedBox >= editables.size()) {
        return;
    }
    const usize split = *typedBox;
    const EditableBox& editable = editables[split];
    const usize caret = editable.getCaret();
    BoxText after{
        .pieces = editable.makePieces(UnitRange{.begin = caret, .end = editable.getUnits().size()}),
        .boxBreak = boxes[split].boxBreak};
    boxes[split].pieces = editable.makePieces(UnitRange{.begin = 0, .end = caret});
    boxes[split].boxBreak = CodePiece{.name = "BOX_BREAK"};
    boxes.insert(boxes.begin() + static_cast<std::ptrdiff_t>(split + 1), std::move(after));
    rebuildEditables(split + 1, 0);
}

void MessageTab::deleteTypedBox() {
    if (!typedBox || *typedBox >= boxes.size() || boxes.size() <= 1) {
        return;
    }
    const usize removed = *typedBox;
    boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(removed));
    if (removed == boxes.size()) {
        boxes.back().boxBreak.reset();
    }
    rebuildEditables(std::nullopt, 0);
}

void MessageTab::applyColor(const usize color) {
    const std::vector<std::string>& colors = focus.catalog->colors;
    if (!typedBox || *typedBox >= editables.size() || color >= colors.size()) {
        return;
    }
    const CodePiece code{.name = "COLOR", .arguments = {colors[color]}};
    EditableBox& editable = editables[*typedBox];
    if (hasTextSelected(editable)) {
        editable.colorSelection(code, CodePiece{.name = "COLOR", .arguments = {colors[0]}});
        applyEditable(*typedBox);
        return;
    }
    const std::array<Piece, 1> pieces = {code};
    insertIntoTyped(pieces);
}

void MessageTab::insertCode(const InsertableCode& insertable) {
    if (!typedBox || *typedBox >= editables.size()) {
        return;
    }
    const std::array<Piece, 1> pieces = {makeCodeToInsert(insertable, *focus.catalog)};
    insertIntoTyped(pieces);
    const usize caret = editables[*typedBox].getCaret();
    if (!insertable.arguments.empty() && caret > 0) {
        openCodePopup(*typedBox, caret - 1);
    }
}

void MessageTab::openCodePopup(const usize box, const usize unit) {
    editedCode = EditedCode{.box = box, .unit = unit};
    codePopupFrame = ImGui::GetFrameCount();
}

void MessageTab::drawCodePopup() {
    if (codePopupFrame && ImGui::GetFrameCount() > *codePopupFrame) {
        ImGui::OpenPopup("##codeArguments");
        codePopupFrame.reset();
    }
    if (codePopupFrame) {
        return;
    }
    if (!ImGui::BeginPopup("##codeArguments")) {
        editedCode.reset();
        return;
    }
    const auto* code =
        editedCode && editedCode->box < editables.size() &&
                editedCode->unit < editables[editedCode->box].getUnits().size()
            ? std::get_if<CodePiece>(&editables[editedCode->box].getUnits()[editedCode->unit].piece)
            : nullptr;
    if (code == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    CodePiece edited = *code;
    ImGui::TextUnformatted(makeCodeName(CodePiece{.name = edited.name}).c_str());
    if (drawCodeArguments(edited, *focus.catalog, focus.draft.getTable(), focus.language,
                          argumentFilter)) {
        editables[editedCode->box].setCode(editedCode->unit, edited);
        applyEditable(editedCode->box);
    }
    ImGui::EndPopup();
}

void MessageTab::handleHotkeys() {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        return;
    }
    if (core::isActionPressed(core::Action::ShowMessageRender)) {
        showMarkup(false);
    }
    if (core::isActionPressed(core::Action::ShowMessageMarkup)) {
        showMarkup(true);
    }
    if (isMarkupShown) {
        return;
    }
    if (core::isActionPressed(core::Action::ToggleMeters)) {
        focus.showsMeters = !focus.showsMeters;
    }
    if (core::isActionPressed(core::Action::ToggleControlCodes)) {
        focus.showsCodes = !focus.showsCodes;
    }
    for (usize color = 0; color < ColorActions.size(); ++color) {
        if (core::isActionPressed(ColorActions[color])) {
            applyColor(color);
        }
    }
    if (core::isActionPressed(core::Action::SplitBox)) {
        splitTypedBox();
    }
    if (core::isActionPressed(core::Action::DeleteBox)) {
        deleteTypedBox();
    }
}

f32 MessageTab::findScale(const f32 width) const {
    if (focus.zoom > 0.0F) {
        return focus.zoom;
    }
    return std::clamp(findStep(width / BoxImageWidth), MinZoom, MaxFitScale);
}

void MessageTab::zoomByWheel() {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.MouseWheel == 0.0F ||
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        return;
    }
    const f32 step = io.MouseWheel > 0.0F ? ZoomStep : -ZoomStep;
    focus.zoom = std::clamp(findStep(shownScale + step + (ZoomStep / 2.0F)), MinZoom, MaxZoom);
}

void MessageTab::drawBoxImage(const usize box, const LaidBox& laid, const f32 width) {
    const BoxImage image = boxRenderer.draw(box, laid, focus.catalog->art, std::max(width, 1.0F));
    if (box < editables.size()) {
        editInBox(box, laid, image);
    }
    if (typedBox == box) {
        const ImVec2 inset(TypedFrameThickness, TypedFrameThickness);
        ImGui::GetWindowDrawList()->AddRect(
            ImVec2(image.origin.x - inset.x, image.origin.y - inset.y),
            ImVec2(image.origin.x + image.size.x + inset.x,
                   image.origin.y + image.size.y + inset.y),
            ImGui::GetColorU32(core::palette::Accent), 0.0F, ImDrawFlags_None, TypedFrameThickness);
    }
    if (!focus.showsMeters) {
        return;
    }
    for (usize line = 0; line < laid.lines.size(); ++line) {
        ImGui::PushID(static_cast<int>(line));
        drawMeter(laid, laid.lines[line], width);
        ImGui::PopID();
    }
}

void MessageTab::drawBox(const usize box, const LaidBox* laid, const f32 scale) {
    ImGui::PushID(static_cast<int>(box));
    if (laid != nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Box %zu · %zu %s · %s", box + 1,
                           laid->lines.size(), laid->lines.size() == 1 ? "line" : "lines",
                           describeEndIcon(laid->endIcon));
        drawBoxImage(box, *laid, BoxImageWidth * scale);
    } else {
        ImGui::TextColored(core::palette::TextMuted,
                           "Box %zu · not drawn: the message ends before it", box + 1);
    }
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::PopID();
}

void MessageTab::drawMarkup() {
    if (markupError) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(core::palette::Danger, "At %zu: %s", markupError->offset + 1,
                           markupError->message.c_str());
        ImGui::PopTextWrapPos();
    }
    const u8 boxType = layout.boxes.empty() ? u8{0} : layout.boxes.front().boxType;
    const std::vector<std::string>& colors = focus.catalog->colors;
    const FindMarkupColor findColor = [&](const std::string_view constant) -> std::optional<u32> {
        const auto found = std::ranges::find(colors, constant);
        if (found == colors.end()) {
            return std::nullopt;
        }
        const auto color = static_cast<u8>(found - colors.begin());
        const TextColor shade =
            color == 0 ? findDefaultColor(boxType) : findTextColor(boxType, color);
        return IM_COL32(shade.red, shade.green, shade.blue, 255);
    };
    if (!drawMarkupEditor("##markup", markup, fonts.getMono(), core::Fonts::MonoSize, findColor)) {
        return;
    }
    std::expected<std::vector<Piece>, MarkupError> pieces = parseMarkup(markup);
    if (!pieces) {
        markupError = std::move(pieces.error());
        return;
    }
    markupError.reset();
    if (const std::optional<usize> found = findIndex()) {
        focus.draft.setText(*found, focus.language, std::move(*pieces));
    }
}

bool MessageTab::selectMarker(const usize box, const std::span<const MarkerGroup> groups) {
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        return false;
    }
    const MarkerGroup* group = findGroupAt(groups, ImGui::GetIO().MousePos);
    if (group == nullptr) {
        return false;
    }
    if (group->markers.size() > 1) {
        if (!markerList || markerList->box != box ||
            markerList->markers.front().unit != group->markers.front().unit) {
            opensMarkerList = true;
        }
        markerList = MarkerList{.box = box, .markers = group->markers, .shape = group->shape};
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return false;
    }
    wasMarkerClicked = true;
    isDragging = false;
    if (group->markers.size() == 1) {
        selectCode(box, group->markers.front().unit);
    }
    return true;
}

void MessageTab::selectCode(const usize box, const usize unit) {
    if (box >= editables.size() || unit >= editables[box].getUnits().size()) {
        return;
    }
    typedBox = box;
    editables[box].setCaret(unit, false);
    editables[box].setCaret(unit + 1, true);
    const auto* code = std::get_if<CodePiece>(&editables[box].getUnits()[unit].piece);
    const InsertableCode* insertable = code != nullptr ? findInsertableCode(code->name) : nullptr;
    if (insertable != nullptr && !insertable->arguments.empty()) {
        openCodePopup(box, unit);
    }
}

void MessageTab::drawMarkerList() {
    constexpr const char* Popup = "##markerList";
    if (!markerList) {
        return;
    }
    if (opensMarkerList) {
        opensMarkerList = false;
        ImGui::OpenPopup(Popup);
    }
    ImGui::SetNextWindowPos(ImVec2(markerList->shape.headMin.x, markerList->shape.headMax.y));
    if (!ImGui::BeginPopup(Popup, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing)) {
        markerList.reset();
        return;
    }
    std::optional<BoxMarker> picked;
    for (usize row = 0; row < markerList->markers.size(); ++row) {
        ImGui::PushID(static_cast<int>(row));
        if (drawMarkerRow(markerList->markers[row])) {
            picked = markerList->markers[row];
        }
        ImGui::PopID();
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowMax(windowMin.x + ImGui::GetWindowWidth(),
                           windowMin.y + ImGui::GetWindowHeight());
    const bool isOverList = mouse.x >= windowMin.x && mouse.x <= windowMax.x &&
                            mouse.y >= windowMin.y && mouse.y <= windowMax.y;
    if (picked || (!isOverList && !markerList->shape.contains(mouse))) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        const usize box = markerList->box;
        markerList.reset();
        if (picked) {
            selectCode(box, picked->unit);
        }
        return;
    }
    ImGui::EndPopup();
}

void MessageTab::editInBox(const usize box, const LaidBox& laid, const BoxImage& image) {
    EditableBox& editable = editables[box];
    const std::vector<CaretPlace> places = findCaretPlaces(editable, laid, boxSources);
    const std::vector<MarkerGroup> groups =
        focus.showsCodes ? groupMarkers(findMarkers(editable, laid, boxSources), laid, image)
                         : std::vector<MarkerGroup>();
    const s32 height = findCharacterHeight(laid);
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 game = image.findGamePosition(io.MousePos);

    const std::vector<DrawnCode> drawnCodes = findDrawnCodes(editable, laid, boxSources);
    const bool isOverMarker =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        findGroupAt(groups, io.MousePos) != nullptr;
    const DrawnCode* drawnCode = ImGui::IsItemHovered() && !isOverMarker
                                     ? findDrawnCodeAt(drawnCodes, game.x, game.y)
                                     : nullptr;
    if (!selectMarker(box, groups) && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        isDragging = drawnCode == nullptr;
        if (drawnCode != nullptr) {
            wasMarkerClicked = true;
            selectCode(box, drawnCode->unit);
        } else {
            const bool extends = io.KeyShift && typedBox == box;
            typedBox = box;
            editable.setCaret(findPlaceAt(places, height, game.x, game.y), extends);
        }
    }
    if (drawnCode != nullptr) {
        if (const auto* code =
                std::get_if<CodePiece>(&editable.getUnits()[drawnCode->unit].piece)) {
            ImGui::SetTooltip("%s", makeCodeName(*code).c_str());
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        isDragging = false;
    }
    drawMarkers(groups, image);
    if (typedBox != box) {
        return;
    }
    if (isDragging && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        editable.setCaret(findPlaceAt(places, height, game.x, game.y), true);
    }
    const bool isTyping = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows |
                                                 ImGuiFocusedFlags_NoPopupHierarchy) &&
                          (!ImGui::IsAnyItemActive() || ImGui::IsItemActive());
    if (!isTyping) {
        return;
    }
    drawCaret(editable, places, drawnCodes, laid, image);
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        typedBox.reset();
        return;
    }
    moveCaretByKeys(editable, places, laid);
    if (editByKeys(editable)) {
        applyEditable(box);
    }
}

void MessageTab::drawStoryboard(const MessageLayout& laidOut) {
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_HorizontalScrollbar |
        (ImGui::GetIO().KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
    if (!ImGui::BeginChild("##storyboard", ImVec2(0.0F, 0.0F), ImGuiChildFlags_None, flags)) {
        ImGui::EndChild();
        return;
    }
    zoomByWheel();
    const f32 frameRoom = TypedFrameThickness * 2.0F;
    ImGui::Indent(frameRoom);
    shownScale = findScale(ImGui::GetContentRegionAvail().x - frameRoom);
    wasMarkerClicked = false;
    const usize count = std::max(laidOut.boxes.size(), boxes.size());
    for (usize box = 0; box < count; ++box) {
        drawBox(box, box < laidOut.boxes.size() ? &laidOut.boxes[box] : nullptr, shownScale);
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
        !ImGui::IsAnyItemHovered() && !wasMarkerClicked) {
        typedBox.reset();
    }
    ImGui::Unindent(frameRoom);
    ImGui::EndChild();
}

void MessageTab::draw() {
    if (!focus.catalog) {
        core::drawBusyCover(ImGui::GetCursorPos(), ImVec2(), "Loading messages");
        return;
    }
    const std::optional<usize> shown = findIndex();
    if (!shown) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Message 0x%04X is no longer among the checkout's messages.", key.id);
        return;
    }
    focus.shown = *shown;
    const Message& message = focus.draft.getTable().messages[*shown];
    const bool isShownInLanguage =
        isInLanguage(message, focus.language, focus.catalog->isFontMessageEnglish);
    if (isShownInLanguage) {
        static_cast<void>(updateLayout(*shown, message));
    }
    drawHeader(*shown, message);
    drawEditState(*shown);
    ImGui::Separator();
    const ImVec2 bodyStart = ImGui::GetCursorPos();
    const bool isKanjiMissing =
        focus.language == Language::Japanese && !focus.catalog->art.hasKanji;
    if (!isShownInLanguage) {
        ImGui::TextColored(core::palette::TextMuted, "The build puts no %s text of this message.",
                           getLanguageName(focus.language));
    } else if (!isKanjiMissing) {
        drawBody(*shown, message);
    }
    if (isKanjiMissing) {
        core::drawBusyCover(bodyStart, ImVec2(), "Loading kanji font");
    } else if (focus.isSaving) {
        core::drawBusyCover(bodyStart, ImVec2(), "Saving messages");
    } else if (focus.isReading) {
        core::drawBusyCover(bodyStart, ImVec2(), "Loading messages");
    }
}

void MessageTab::drawBody(const usize shown, const Message& message) {
    const MessageLayout& laidOut = updateLayout(shown, message);
    updateEditor(shown, message);
    handleHotkeys();
    for (const base::Diagnostic& diagnostic : laidOut.diagnostics) {
        ImGui::TextColored(diagnostic.severity == base::Severity::Error ? core::palette::Danger
                                                                        : core::palette::Warning,
                           "%s", diagnostic.message.c_str());
    }
    if (isMarkupShown) {
        drawMarkup();
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, core::palette::Surface);
    if (ImGui::BeginChild("##rail", ImVec2(measureRail(), 0.0F),
                          ImGuiChildFlags_AlwaysUseWindowPadding)) {
        drawRail();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    drawStoryboard(laidOut);
    drawMarkerList();
    drawCodePopup();
}

} // namespace toki::message

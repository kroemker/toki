#include "MessageTab.h"

#include "app/core/domain/ReadableName.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/ConstantSelectable.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/Charmap.h"
#include "app/message/domain/EditableBox.h"
#include "app/message/domain/MessageCatalog.h"
#include "app/message/domain/MessageMarkup.h"
#include "app/message/domain/MessageRows.h"
#include "base/Diagnostic.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <bit>
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
constexpr f32 BesideMinScale = 2.0F;
constexpr f32 MarkupMinEms = 22.0F;
constexpr f32 GlyphEms = 1.6F;
constexpr f32 LanguageComboEms = 8.0F;
constexpr f32 ZoomComboEms = 7.0F;
constexpr std::array ZoomChoices = {1.0F, 1.5F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
constexpr float MeterHeightLines = 0.35F;
constexpr float NearLimit = 0.9F;
constexpr f32 MarkerHalfWidth = 2.0F;
constexpr f32 SelectedLineEndWidth = 3.0F;
constexpr double CaretBlinkPeriod = 1.2;
constexpr double CaretBlinkShown = 0.8;
constexpr ImWchar FirstTypedCharacter = 0x20;
constexpr ImWchar DeleteCharacter = 0x7F;
constexpr usize Utf8Capacity = 5;

/// The codes without arguments the Code menu puts in; one with arguments is
/// written as markup.
constexpr std::array<std::string_view, 16> InsertedCodes = {
    "NAME",          "TWO_CHOICE",         "THREE_CHOICE", "QUICKTEXT_ENABLE", "QUICKTEXT_DISABLE",
    "UNSKIPPABLE",   "AWAIT_BUTTON_PRESS", "EVENT",        "PERSISTENT",       "OCARINA",
    "MARATHON_TIME", "RACE_TIME",          "POINTS",       "TOKENS",           "FISH_INFO",
    "TIME",
};

[[nodiscard]] std::string makeSignLabel(const std::string_view sign) {
    if (sign.size() > 2 && sign.front() == '[' && sign.back() == ']') {
        return std::string(sign.substr(1, sign.size() - 2));
    }
    return std::string(sign);
}

[[nodiscard]] std::string makeCodeLabel(const CodePiece& code) {
    const std::array<Piece, 1> pieces = {code};
    const std::string label = formatMarkup(pieces);
    return label.substr(1, label.size() - 2);
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

/// A code that draws nothing, a color or a pause, marked where it stands.
struct Marker {
    usize unit = 0;
    CaretPlace place;
    TextColor color;
    bool isColor = false;
    std::string tag;
};

[[nodiscard]] std::vector<Marker> findMarkers(const EditableBox& editable, const LaidBox& box,
                                              const std::span<const TextPosition> sources) {
    std::vector<Marker> markers;
    for (const LaidStop& stop : box.stops) {
        if (stop.byte >= sources.size()) {
            continue;
        }
        const std::optional<usize> unit = editable.findUnit(sources[stop.byte]);
        if (!unit || *unit >= editable.getUnits().size()) {
            continue;
        }
        const auto* code = std::get_if<CodePiece>(&editable.getUnits()[*unit].piece);
        if (code == nullptr || code->name == "ITEM_ICON" ||
            std::ranges::any_of(box.characters, [&](const LaidCharacter& character) {
                return character.byte == stop.byte;
            })) {
            continue;
        }
        const std::array<Piece, 1> piece = {*code};
        markers.push_back(Marker{.unit = *unit,
                                 .place = CaretPlace{.x = stop.x, .y = stop.y},
                                 .color = stop.color,
                                 .isColor = code->name == "COLOR",
                                 .tag = formatMarkup(piece)});
    }
    return markers;
}

void drawMarkers(const std::vector<Marker>& markers, const LaidBox& box, const BoxImage& image) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float height = static_cast<float>(findCharacterHeight(box)) * image.scale;
    const float half = MarkerHalfWidth * image.scale;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const Marker& marker : markers) {
        const ImVec2 top = image.findScreenPosition(static_cast<f32>(marker.place.x),
                                                    static_cast<f32>(marker.place.y));
        const ImU32 color =
            marker.isColor ? IM_COL32(marker.color.red, marker.color.green, marker.color.blue, 255)
                           : ImGui::GetColorU32(core::palette::Accent);
        drawList->AddLine(top, ImVec2(top.x, top.y + height), color,
                          std::max(1.0F, image.scale * 0.5F));
        drawList->AddTriangleFilled(ImVec2(top.x - half, top.y - half),
                                    ImVec2(top.x + half, top.y - half), ImVec2(top.x, top.y + half),
                                    color);
        if (ImGui::IsItemHovered() && std::abs(mouse.x - top.x) <= half &&
            mouse.y >= top.y - half && mouse.y <= top.y + height) {
            ImGui::SetTooltip("%s draws nothing here. Click it to select it.", marker.tag.c_str());
        }
    }
}

void drawCaret(const EditableBox& editable, const std::vector<CaretPlace>& places,
               const LaidBox& box, const BoxImage& image) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float height = static_cast<float>(findCharacterHeight(box)) * image.scale;
    const UnitRange selection = editable.getSelection();
    for (usize unit = selection.begin; unit < selection.end; ++unit) {
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
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
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

[[nodiscard]] u64 mixKey(const MessageCatalog* catalog, const u64 revision, const usize message,
                         const Language language, const usize box) {
    u64 key = std::bit_cast<std::uintptr_t>(catalog);
    for (const u64 part : {revision, u64{message}, u64{static_cast<u8>(language)}, u64{box}}) {
        key = (key ^ part) * 0x100000001B3ULL;
    }
    return key;
}

[[nodiscard]] const char* describeMacro(const MessageMacro macro) {
    switch (macro) {
    case MessageMacro::Message:
        return "Every language";
    case MessageMacro::JapaneseOnly:
        return "Japanese only";
    case MessageMacro::NesOnly:
        return "English, German and French only";
    case MessageMacro::FontMessage:
        return "The font message";
    }
    return "";
}

[[nodiscard]] const char* describeEndIcon(const EndIcon icon) {
    switch (icon) {
    case EndIcon::None:
        return "closes by itself";
    case EndIcon::Triangle:
        return "waits, then goes on";
    case EndIcon::Square:
        return "waits, then closes";
    case EndIcon::Arrow:
        return "waits for a choice";
    }
    return "";
}

void drawMeter(const LaidBox& box, const LaidLine& line, const float width) {
    const s32 limit = findLineLimit(box);
    const s32 room = limit - findLineStart(box);
    const s32 used = line.endX - findLineStart(box);
    const float fraction = room > 0 ? static_cast<float>(used) / static_cast<float>(room) : 0.0F;
    ImVec4 color = core::palette::Success;
    if (line.endX > limit) {
        color = core::palette::Danger;
    } else if (fraction > NearLimit) {
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%d of %d pixels%s", used, room,
                          line.endX > limit ? ", past the box's margin" : "");
    }
}

[[nodiscard]] bool drawConstantCombo(const char* id, std::string& value,
                                     const std::vector<std::string>& options,
                                     const std::string_view prefix) {
    bool changed = false;
    const std::string shown = core::makeConstantLabel(value, prefix);
    ImGui::SetNextItemWidth(-FLT_MIN);
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

[[nodiscard]] std::vector<CodePiece> makeInsertedCodes() {
    std::vector<CodePiece> codes;
    codes.reserve(InsertedCodes.size());
    for (const std::string_view name : InsertedCodes) {
        codes.push_back(CodePiece{.name = std::string(name)});
    }
    return codes;
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
    boxes = splitBoxes(message.texts[static_cast<usize>(focus.language)]);
    markups.clear();
    for (const BoxText& box : boxes) {
        markups.push_back(formatMarkup(box.pieces));
    }
    markupErrors.assign(boxes.size(), std::nullopt);

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

void MessageTab::drawHeader(const Message& message) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("0x%04X", message.id);
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "· %s · %s",
                       core::makeConstantLabel(message.boxType, "TEXTBOX_TYPE_").c_str(),
                       core::makeConstantLabel(message.boxPosition, "TEXTBOX_POS_").c_str());
    const f32 comboWidth = ImGui::GetFontSize() * LanguageComboEms;
    const f32 zoomWidth = ImGui::GetFontSize() * ZoomComboEms;
    const f32 spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(
        std::max(ImGui::GetContentRegionMax().x - comboWidth - zoomWidth - spacing, 0.0F));
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("The languages the checkout has text in: an NTSC version has English "
                          "and Japanese, a PAL one English, German and French.");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(zoomWidth);
    drawZoomMenu();
    if (focus.language == Language::Japanese && !focus.catalog->art.hasKanji) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Reading the kanji font; the text is drawn once it is read.");
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("How large the boxes are drawn, in steps that keep the game's pixels "
                          "whole. Fit draws them as large as the panel holds. Ctrl+Scroll over "
                          "the boxes zooms too.");
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
    const f32 railWidth = ImGui::GetContentRegionAvail().x;
    const bool canType = typedBox && *typedBox < editables.size();
    const Charmap* charmap = focus.catalog->findCharmap(focus.language);
    const ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImGui::BeginDisabled(!canType);
    if (charmap != nullptr) {
        usize column = 0;
        for (const CharmapEntry* sign : charmap->listSigns()) {
            if (column % 2 == 1) {
                ImGui::SameLine();
            }
            ++column;
            ImGui::PushID(sign->text.c_str());
            const gfx::ImageTexture* glyph = signGlyphs.findGlyph(focus.catalog->art, sign->code);
            bool isClicked = false;
            if (glyph != nullptr) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                const auto texture =
                    static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(glyph->getTexture()));
                isClicked =
                    ImGui::ImageButton("##sign", texture, ImVec2(glyphSize, glyphSize),
                                       ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
            } else {
                isClicked = ImGui::Button(makeSignLabel(sign->text).c_str(),
                                          ImVec2(buttonSize, buttonSize));
            }
            if (isClicked && canType) {
                editables[*typedBox].insertText(sign->text);
                applyEditable(*typedBox);
            }
            ImGui::SetItemTooltip("%s: puts it in where the caret is.", sign->text.c_str());
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    const u8 boxType =
        canType && *typedBox < layout.boxes.size() ? layout.boxes[*typedBox].boxType : u8{0};
    const std::vector<std::string>& colors = focus.catalog->colors;
    for (usize color = 0; color < colors.size(); ++color) {
        if (color % 2 == 1) {
            ImGui::SameLine();
        }
        const CodePiece code{.name = "COLOR", .arguments = {colors[color]}};
        ImGui::PushID(static_cast<int>(color));
        const TextColor shade =
            color == 0 ? findDefaultColor(boxType) : findTextColor(boxType, static_cast<u8>(color));
        if (ImGui::ColorButton("##color", makeColor(shade), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(glyphSize + (style.FramePadding.x * 2.0F), buttonSize))) {
            const std::array<Piece, 1> pieces = {code};
            insertIntoTyped(pieces);
        }
        ImGui::SetItemTooltip("{%s}: the text after it in this color.",
                              makeCodeLabel(code).c_str());
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button("Name", ImVec2(railWidth, 0.0F))) {
        const std::array<Piece, 1> pieces = {CodePiece{.name = "NAME"}};
        insertIntoTyped(pieces);
    }
    ImGui::SetItemTooltip("{Name}: the player's name, drawn here as Link.");
    ImGui::SetNextItemWidth(railWidth);
    if (ImGui::BeginCombo("##code", "Code", ImGuiComboFlags_HeightLarge)) {
        for (const CodePiece& code : makeInsertedCodes()) {
            if (ImGui::Selectable(makeCodeLabel(code).c_str())) {
                const std::array<Piece, 1> pieces = {code};
                insertIntoTyped(pieces);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Puts a code in where the caret is; one with arguments is written as "
                          "markup.");
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::Button(std::format("{} Box", core::icons::Plus).c_str(), ImVec2(railWidth, 0.0F)) &&
        !boxes.empty()) {
        boxes.back().boxBreak = CodePiece{.name = "BOX_BREAK"};
        boxes.emplace_back();
        applyBoxes();
        editorKey.reset();
    }
    ImGui::SetItemTooltip("Adds a box after the last one.");
    ImGui::BeginDisabled(!canType || boxes.size() <= 1);
    if (ImGui::Button(std::format("{} Box", core::icons::Trash).c_str(), ImVec2(railWidth, 0.0F)) &&
        canType && boxes.size() > 1) {
        const usize removed = *typedBox;
        boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(removed));
        if (removed == boxes.size()) {
            boxes.back().boxBreak.reset();
        }
        typedBox.reset();
        applyBoxes();
        editorKey.reset();
    }
    ImGui::SetItemTooltip("Removes the box the caret is in.");
    ImGui::EndDisabled();
}

MessageTab::Arrangement MessageTab::findArrangement(const f32 width) const {
    const f32 spacing = ImGui::GetStyle().ItemSpacing.x * 2.0F;
    const f32 markupWidth = ImGui::GetFontSize() * MarkupMinEms;
    if (focus.zoom > 0.0F) {
        return Arrangement{.scale = focus.zoom,
                           .isBeside =
                               width - (BoxImageWidth * focus.zoom) - spacing >= markupWidth};
    }
    const f32 beside = findStep((width - spacing - markupWidth) / BoxImageWidth);
    if (beside >= BesideMinScale) {
        return Arrangement{.scale = std::min(beside, MaxFitScale), .isBeside = true};
    }
    return Arrangement{.scale = std::clamp(findStep(width / BoxImageWidth), MinZoom, MaxFitScale),
                       .isBeside = false};
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
    for (usize line = 0; line < laid.lines.size(); ++line) {
        ImGui::PushID(static_cast<int>(line));
        drawMeter(laid, laid.lines[line], width);
        ImGui::PopID();
    }
}

void MessageTab::drawBox(const usize box, const LaidBox* laid, const Arrangement& arrangement) {
    ImGui::PushID(static_cast<int>(box));
    const f32 width = BoxImageWidth * arrangement.scale;
    if (laid != nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Box %zu · %zu %s · %s", box + 1,
                           laid->lines.size(), laid->lines.size() == 1 ? "line" : "lines",
                           describeEndIcon(laid->endIcon));
    } else {
        ImGui::TextColored(core::palette::TextMuted,
                           "Box %zu · not drawn: the message ends before it", box + 1);
    }
    if (arrangement.isBeside) {
        ImGui::BeginGroup();
        if (laid != nullptr) {
            drawBoxImage(box, *laid, width);
        } else {
            ImGui::Dummy(ImVec2(width, 1.0F));
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x * 2.0F);
        ImGui::BeginGroup();
        if (box < boxes.size()) {
            drawEditor(box, ImGui::GetContentRegionAvail().x);
        }
        ImGui::EndGroup();
    } else {
        if (laid != nullptr) {
            drawBoxImage(box, *laid, width);
        }
        if (box < boxes.size()) {
            drawEditor(box, std::min(width, ImGui::GetContentRegionAvail().x));
        }
    }
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::PopID();
}

void MessageTab::drawEditor(const usize box, const f32 width) {
    std::string& markup = markups[box];
    const f32 lines = static_cast<f32>(std::ranges::count(markup, '\n') + 2);
    ImGui::PushFont(fonts.getMono(), core::Fonts::MonoSize);
    const f32 height = ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0F;
    const bool isChanged =
        ImGui::InputTextMultiline("##markup", &markup, ImVec2(std::max(width, 1.0F), height));
    ImGui::PopFont();
    if (ImGui::IsItemActive()) {
        typedBox.reset();
    }
    if (isChanged) {
        std::expected<std::vector<Piece>, MarkupError> pieces = parseMarkup(markup);
        if (pieces) {
            markupErrors[box].reset();
            boxes[box].pieces = std::move(*pieces);
            applyBoxes();
        } else {
            markupErrors[box] = std::move(pieces.error());
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("{Red} … {Default} colors, {Name} the player's name, {Icon Pocket Egg} "
                          "an item's icon, {Box Break} starts the next box. {{ is a brace.");
    }
    if (markupErrors[box]) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(core::palette::Danger, "At %zu: %s", markupErrors[box]->offset + 1,
                           markupErrors[box]->message.c_str());
        ImGui::PopTextWrapPos();
    }
}

void MessageTab::editInBox(const usize box, const LaidBox& laid, const BoxImage& image) {
    EditableBox& editable = editables[box];
    const std::vector<CaretPlace> places = findCaretPlaces(editable, laid, boxSources);
    const std::vector<Marker> markers = findMarkers(editable, laid, boxSources);
    const s32 height = findCharacterHeight(laid);
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 game = image.findGamePosition(io.MousePos);
    const bool isHovered = ImGui::IsItemHovered();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        const auto marker = std::ranges::find_if(markers, [&](const Marker& found) {
            return std::abs(game.x - static_cast<f32>(found.place.x)) <= MarkerHalfWidth &&
                   game.y >= static_cast<f32>(found.place.y) - MarkerHalfWidth &&
                   game.y <= static_cast<f32>(found.place.y + height);
        });
        const bool extends = io.KeyShift && typedBox == box;
        typedBox = box;
        if (marker != markers.end()) {
            editable.setCaret(marker->unit, false);
            editable.setCaret(marker->unit + 1, true);
            isDragging = false;
        } else {
            editable.setCaret(findPlaceAt(places, height, game.x, game.y), extends);
            isDragging = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        isDragging = false;
    }
    drawMarkers(markers, laid, image);
    if (typedBox != box) {
        if (isHovered && markers.empty()) {
            ImGui::SetItemTooltip("Click to type in the box.");
        }
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
    drawCaret(editable, places, laid, image);
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
    const Arrangement arrangement = findArrangement(ImGui::GetContentRegionAvail().x);
    shownScale = arrangement.scale;
    const usize count = std::max(laidOut.boxes.size(), boxes.size());
    for (usize box = 0; box < count; ++box) {
        drawBox(box, box < laidOut.boxes.size() ? &laidOut.boxes[box] : nullptr, arrangement);
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
        !ImGui::IsAnyItemHovered()) {
        typedBox.reset();
    }
    ImGui::EndChild();
}

void MessageTab::draw() {
    if (!focus.catalog) {
        ImGui::TextColored(core::palette::TextMuted, "Reading the messages…");
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
    drawHeader(message);
    drawEditState(*shown);
    ImGui::Separator();
    if (!isInLanguage(message, focus.language, focus.catalog->isFontMessageEnglish)) {
        ImGui::TextColored(core::palette::TextMuted, "The build puts no %s text of this message.",
                           getLanguageName(focus.language));
        return;
    }
    const MessageLayout& laidOut = updateLayout(*shown, message);
    updateEditor(*shown, message);
    for (const base::Diagnostic& diagnostic : laidOut.diagnostics) {
        ImGui::TextColored(diagnostic.severity == base::Severity::Error ? core::palette::Danger
                                                                        : core::palette::Warning,
                           "%s", diagnostic.message.c_str());
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 buttonWidth = (ImGui::GetFontSize() * GlyphEms) + (style.FramePadding.x * 2.0F);
    const f32 railWidth =
        (buttonWidth * 2.0F) + style.ItemSpacing.x + (style.WindowPadding.x * 2.0F);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, core::palette::Surface);
    if (ImGui::BeginChild("##rail", ImVec2(railWidth, 0.0F),
                          ImGuiChildFlags_AlwaysUseWindowPadding)) {
        drawRail();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    drawStoryboard(laidOut);
}

std::optional<u64> MessageTab::findSelectionKey() const {
    const std::optional<usize> shown = findIndex();
    if (!shown) {
        return std::nullopt;
    }
    return mixKey(focus.catalog.get(), 0, *shown, Language::Japanese, 0);
}

void MessageTab::drawProperties() {
    const std::optional<usize> shown = findIndex();
    if (!shown) {
        ImGui::TextColored(core::palette::TextMuted, "Pick a message to see its properties.");
        return;
    }
    const Message& message = focus.draft.getTable().messages[*shown];
    if (!core::beginProperties("##messageProperties")) {
        return;
    }
    core::drawPropertyLabel("ID");
    ImGui::Text("0x%04X", message.id);
    core::drawPropertyLabel("Box Type", "The box's frame and the colors its text is drawn in.");
    std::string type = message.boxType;
    if (drawConstantCombo("##boxType", type, listBoxTypes(focus.draft.getTable()),
                          "TEXTBOX_TYPE_")) {
        focus.draft.setBoxType(*shown, std::move(type));
    }
    core::drawPropertyLabel("Box Position",
                            "Where on screen the box stands. Variable lets the game put it above "
                            "or below, by where the player and the actor spoken to stand.");
    std::string position = message.boxPosition;
    if (drawConstantCombo("##boxPosition", position, listBoxPositions(focus.draft.getTable()),
                          "TEXTBOX_POS_")) {
        focus.draft.setBoxPosition(*shown, std::move(position));
    }
    core::drawPropertyLabel("Languages", "Which languages' tables the build puts the message in.");
    ImGui::TextUnformatted(describeMacro(message.macro));
    core::drawPropertyLabel("Boxes");
    ImGui::Text("%zu", countBoxes(message.texts[static_cast<usize>(focus.language)]));
    core::drawPropertyLabel("Source", "The file the message is written in.");
    const std::string file = core::encodeUtf8(
        message.location.file.lexically_relative(focus.catalog->workspace->getRoot()));
    ImGui::TextUnformatted(std::format("{}:{}", file, message.location.line).c_str());
    core::endProperties();
}

} // namespace toki::message

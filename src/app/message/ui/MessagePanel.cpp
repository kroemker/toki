#include "MessagePanel.h"

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

constexpr float MaxBoxWidth = 272.0F * 2.0F;
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

/// Lays items out in rows, starting a new one where the next would pass
/// `right`.
struct RowFlow {
    float right = 0.0F;
    bool isFirst = true;

    void placeNext(const float width) {
        if (!isFirst) {
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x + width > right) {
                ImGui::NewLine();
            }
        }
        isFirst = false;
    }
};

/// A code that draws nothing, a color or a pause, marked where it stands.
struct Marker {
    usize unit = 0;
    CaretPlace place;
    TextColor color;
    bool isColor = false;
    std::string tag;
};

[[nodiscard]] bool drawSignButtons(EditableBox& editable, const Charmap& charmap, RowFlow& flow) {
    const float padding = ImGui::GetStyle().FramePadding.x * 2.0F;
    bool isChanged = false;
    for (const CharmapEntry* sign : charmap.listSigns()) {
        const std::string label = makeSignLabel(sign->text);
        flow.placeNext(ImGui::CalcTextSize(label.c_str()).x + padding);
        if (ImGui::SmallButton(std::format("{}##{}", label, sign->text).c_str())) {
            editable.insertText(sign->text);
            isChanged = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Puts %s where the caret is.", sign->text.c_str());
        }
    }
    return isChanged;
}

[[nodiscard]] std::optional<CodePiece> drawCodeMenu(const char* id, const char* label,
                                                    const std::vector<CodePiece>& codes,
                                                    RowFlow& flow) {
    const float width = ImGui::GetFontSize() * 7.0F;
    flow.placeNext(width);
    ImGui::SetNextItemWidth(width);
    std::optional<CodePiece> picked;
    if (ImGui::BeginCombo(id, label)) {
        for (const CodePiece& code : codes) {
            if (ImGui::Selectable(makeCodeLabel(code).c_str())) {
                picked = code;
            }
        }
        ImGui::EndCombo();
    }
    return picked;
}

[[nodiscard]] std::optional<CodePiece> drawCodeMenus(const std::vector<std::string>& colors,
                                                     RowFlow& flow) {
    std::vector<CodePiece> colorCodes;
    colorCodes.reserve(colors.size());
    for (const std::string& color : colors) {
        colorCodes.push_back(CodePiece{.name = "COLOR", .arguments = {color}});
    }
    std::vector<CodePiece> otherCodes;
    otherCodes.reserve(InsertedCodes.size());
    for (const std::string_view name : InsertedCodes) {
        otherCodes.push_back(CodePiece{.name = std::string(name)});
    }
    std::optional<CodePiece> picked = drawCodeMenu("##color", "Color", colorCodes, flow);
    if (std::optional<CodePiece> other = drawCodeMenu("##code", "Code", otherCodes, flow)) {
        picked = std::move(other);
    }
    return picked;
}

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

} // namespace

MessagePanel::MessagePanel(const gfx::Device& device, const core::Fonts& fonts, MessageFocus& focus,
                           std::function<void()> save)
    : Panel("message", "Message", core::icons::Chat, core::DockSlot::Centre), fonts(fonts),
      focus(focus), save(std::move(save)), boxRenderer(device) {}

void MessagePanel::showMessage() {
    setOpen(true);
    ImGui::SetWindowFocus(getWindowTitle().c_str());
}

MessagePanel::ShownKey MessagePanel::makeShownKey() const {
    return ShownKey{.catalog = focus.catalog.get(),
                    .revision = focus.draft.getRevision(),
                    .index = *focus.shown,
                    .language = focus.language};
}

const MessageLayout& MessagePanel::updateLayout(const Message& message) {
    const ShownKey key = makeShownKey();
    if (layoutKey != key) {
        layoutKey = key;
        layout = focus.catalog->layOut(message, focus.language);
        boxSources =
            localizeSources(message.texts[static_cast<usize>(focus.language)], layout.sources);
    }
    return layout;
}

void MessagePanel::updateEditor(const Message& message) {
    const ShownKey key = makeShownKey();
    const bool isOtherMessage = !editorKey || editorKey->catalog != key.catalog ||
                                editorKey->index != key.index ||
                                editorKey->language != key.language;
    if (editorKey == key || (!isOtherMessage && ImGui::IsAnyItemActive())) {
        return;
    }
    editorKey = key;
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

void MessagePanel::applyEditable(const usize index) {
    boxes[index].pieces = editables[index].makePieces();
    applyBoxes();
}

void MessagePanel::applyBoxes() {
    focus.draft.setText(*focus.shown, focus.language, joinBoxes(boxes));
}

void MessagePanel::drawHeader(const Message& message) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("0x%04X", message.id);
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "· %s · %s",
                       core::makeConstantLabel(message.boxType, "TEXTBOX_TYPE_").c_str(),
                       core::makeConstantLabel(message.boxPosition, "TEXTBOX_POS_").c_str());
    const float comboWidth = ImGui::GetFontSize() * 8.0F;
    ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - comboWidth, 0.0F));
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
    if (focus.language == Language::Japanese && !focus.catalog->art.hasKanji) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Reading the kanji font; the text is drawn once it is read.");
    }
}

void MessagePanel::drawEditState() {
    if (!focus.draft.isEdited()) {
        return;
    }
    if (focus.draft.hasReadChanged()) {
        ImGui::TextColored(core::palette::Warning,
                           "The checkout's messages changed since they were edited here.");
    } else {
        ImGui::TextColored(core::palette::Accent, "Edited, not saved.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save") && save) {
        save();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Discard Edits")) {
        ImGui::OpenPopup("##discard");
    }
    if (ImGui::BeginPopup("##discard")) {
        ImGui::TextUnformatted("Throw away every edit of every message?");
        if (ImGui::Button("Discard")) {
            focus.draft.discard();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MessagePanel::drawEditor(const usize index) {
    std::string& markup = markups[index];
    const float lines = static_cast<float>(std::ranges::count(markup, '\n') + 2);
    const float width = std::min(ImGui::GetContentRegionAvail().x, MaxBoxWidth);
    ImGui::PushFont(fonts.getMono(), core::Fonts::MonoSize);
    const float height =
        ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0F;
    const bool isChanged = ImGui::InputTextMultiline("##markup", &markup, ImVec2(width, height));
    ImGui::PopFont();
    if (ImGui::IsItemActive()) {
        typedBox.reset();
    }
    if (isChanged) {
        std::expected<std::vector<Piece>, MarkupError> pieces = parseMarkup(markup);
        if (pieces) {
            markupErrors[index].reset();
            boxes[index].pieces = std::move(*pieces);
            applyBoxes();
        } else {
            markupErrors[index] = std::move(pieces.error());
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("{Red} … {Default} colors, {Name} the player's name, {Icon Pocket Egg} "
                          "an item's icon, {Box Break} starts the next box. {{ is a brace.");
    }
    if (markupErrors[index]) {
        ImGui::TextColored(core::palette::Danger, "At %zu: %s", markupErrors[index]->offset + 1,
                           markupErrors[index]->message.c_str());
    }
    if (boxes.size() > 1 && ImGui::SmallButton("Remove Box")) {
        boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(index));
        if (index == boxes.size()) {
            boxes.back().boxBreak.reset();
        }
        applyBoxes();
        editorKey.reset();
    }
}

void MessagePanel::drawSigns(const usize index) {
    RowFlow flow{.right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x};
    const Charmap* charmap = focus.catalog->findCharmap(focus.language);
    bool isChanged = charmap != nullptr && drawSignButtons(editables[index], *charmap, flow);
    if (const std::optional<CodePiece> code = drawCodeMenus(focus.catalog->colors, flow)) {
        const std::array<Piece, 1> pieces = {*code};
        editables[index].insert(pieces);
        isChanged = true;
    }
    if (isChanged) {
        applyEditable(index);
    }
}

void MessagePanel::editInBox(const usize index, const LaidBox& box, const BoxImage& image) {
    EditableBox& editable = editables[index];
    const std::vector<CaretPlace> places = findCaretPlaces(editable, box, boxSources);
    const std::vector<Marker> markers = findMarkers(editable, box, boxSources);
    const s32 height = findCharacterHeight(box);
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 game = image.findGamePosition(io.MousePos);
    const bool isHovered = ImGui::IsItemHovered();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        const auto marker = std::ranges::find_if(markers, [&](const Marker& found) {
            return std::abs(game.x - static_cast<f32>(found.place.x)) <= MarkerHalfWidth &&
                   game.y >= static_cast<f32>(found.place.y) - MarkerHalfWidth &&
                   game.y <= static_cast<f32>(found.place.y + height);
        });
        const bool extends = io.KeyShift && typedBox == index;
        typedBox = index;
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
    drawMarkers(markers, box, image);
    if (typedBox != index) {
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
    drawCaret(editable, places, box, image);
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        typedBox.reset();
        return;
    }
    moveCaretByKeys(editable, places, box);
    if (editByKeys(editable)) {
        applyEditable(index);
    }
}

void MessagePanel::drawBox(const usize index, const LaidBox* box) {
    ImGui::PushID(static_cast<int>(index));
    const float width = std::min(ImGui::GetContentRegionAvail().x, MaxBoxWidth);
    if (box != nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Box %zu · %zu %s · %s", index + 1,
                           box->lines.size(), box->lines.size() == 1 ? "line" : "lines",
                           describeEndIcon(box->endIcon));
        const BoxImage image =
            boxRenderer.draw(index, *box, focus.catalog->art, std::max(width, 1.0F));
        if (index < editables.size()) {
            editInBox(index, *box, image);
        }
        if (typedBox == index) {
            drawSigns(index);
        }
        for (usize line = 0; line < box->lines.size(); ++line) {
            ImGui::PushID(static_cast<int>(line));
            drawMeter(*box, box->lines[line], width);
            ImGui::PopID();
        }
    } else {
        ImGui::TextColored(core::palette::TextMuted,
                           "Box %zu · not drawn: the message ends before it", index + 1);
    }
    if (index < boxes.size()) {
        drawEditor(index);
    }
    ImGui::Spacing();
    ImGui::PopID();
}

void MessagePanel::draw() {
    const Message* message = focus.findShown();
    if (message == nullptr) {
        ImGui::TextColored(core::palette::TextMuted,
                           focus.catalog ? "Pick a message in the Assets panel's Messages tab."
                                         : "Reading the messages…");
        return;
    }
    drawHeader(*message);
    drawEditState();
    ImGui::Separator();
    if (!isInLanguage(*message, focus.language, focus.catalog->isFontMessageEnglish)) {
        ImGui::TextColored(core::palette::TextMuted, "The build puts no %s text of this message.",
                           getLanguageName(focus.language));
        return;
    }
    const MessageLayout& laidOut = updateLayout(*message);
    updateEditor(*message);
    for (const base::Diagnostic& diagnostic : laidOut.diagnostics) {
        ImGui::TextColored(diagnostic.severity == base::Severity::Error ? core::palette::Danger
                                                                        : core::palette::Warning,
                           "%s", diagnostic.message.c_str());
    }
    if (!ImGui::BeginChild("##storyboard")) {
        ImGui::EndChild();
        return;
    }
    const usize count = std::max(laidOut.boxes.size(), boxes.size());
    for (usize index = 0; index < count; ++index) {
        drawBox(index, index < laidOut.boxes.size() ? &laidOut.boxes[index] : nullptr);
    }
    if (ImGui::Button("Add Box")) {
        boxes.back().boxBreak = CodePiece{.name = "BOX_BREAK"};
        boxes.emplace_back();
        applyBoxes();
        editorKey.reset();
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
        !ImGui::IsAnyItemHovered()) {
        typedBox.reset();
    }
    ImGui::EndChild();
}

std::optional<u64> MessagePanel::findSelectionKey() const {
    if (focus.findShown() == nullptr) {
        return std::nullopt;
    }
    return mixKey(focus.catalog.get(), 0, *focus.shown, Language::Japanese, 0);
}

void MessagePanel::drawProperties() {
    const Message* message = focus.findShown();
    if (message == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Pick a message to see its properties.");
        return;
    }
    if (!core::beginProperties("##messageProperties")) {
        return;
    }
    const usize index = *focus.shown;
    core::drawPropertyLabel("ID");
    ImGui::Text("0x%04X", message->id);
    core::drawPropertyLabel("Box Type", "The box's frame and the colors its text is drawn in.");
    std::string type = message->boxType;
    if (drawConstantCombo("##boxType", type, listBoxTypes(focus.draft.getTable()),
                          "TEXTBOX_TYPE_")) {
        focus.draft.setBoxType(index, std::move(type));
    }
    core::drawPropertyLabel("Box Position",
                            "Where on screen the box stands. Variable lets the game put it above "
                            "or below, by where the player and the actor spoken to stand.");
    std::string position = message->boxPosition;
    if (drawConstantCombo("##boxPosition", position, listBoxPositions(focus.draft.getTable()),
                          "TEXTBOX_POS_")) {
        focus.draft.setBoxPosition(index, std::move(position));
    }
    core::drawPropertyLabel("Languages", "Which languages' tables the build puts the message in.");
    ImGui::TextUnformatted(describeMacro(message->macro));
    core::drawPropertyLabel("Boxes");
    ImGui::Text("%zu", countBoxes(message->texts[static_cast<usize>(focus.language)]));
    core::drawPropertyLabel("Source", "The file the message is written in.");
    const std::string file = core::encodeUtf8(
        message->location.file.lexically_relative(focus.catalog->workspace->getRoot()));
    ImGui::TextUnformatted(std::format("{}:{}", file, message->location.line).c_str());
    core::endProperties();
}

} // namespace toki::message

#include "MessagePanel.h"

#include "app/core/domain/ReadableName.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/ConstantSelectable.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageCatalog.h"
#include "app/message/domain/MessageMarkup.h"
#include "app/message/domain/MessageRows.h"
#include "base/Diagnostic.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::message {

namespace {

constexpr float MaxBoxWidth = 272.0F * 2.0F;
constexpr float MeterHeightLines = 0.35F;
constexpr float NearLimit = 0.9F;

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

MessagePanel::MessagePanel(const gfx::Device& device, MessageFocus& focus,
                           std::function<void()> save)
    : Panel("message", "Message", core::icons::Chat, core::DockSlot::Centre), focus(focus),
      save(std::move(save)), boxRenderer(device) {}

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
    const float height =
        ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0F;
    if (ImGui::InputTextMultiline("##markup", &markup, ImVec2(width, height))) {
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

void MessagePanel::drawBox(const usize index, const LaidBox* box) {
    ImGui::PushID(static_cast<int>(index));
    const float width = std::min(ImGui::GetContentRegionAvail().x, MaxBoxWidth);
    if (box != nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Box %zu · %zu %s · %s", index + 1,
                           box->lines.size(), box->lines.size() == 1 ? "line" : "lines",
                           describeEndIcon(box->endIcon));
        boxRenderer.draw(index, *box, focus.catalog->art, std::max(width, 1.0F));
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

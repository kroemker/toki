#include "MessagePanel.h"

#include "app/core/domain/ReadableName.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/ConstantSelectable.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageCatalog.h"
#include "app/message/domain/MessageRows.h"
#include "base/Diagnostic.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstdint>
#include <format>
#include <string>

namespace toki::message {

namespace {

constexpr float MaxBoxWidth = 272.0F * 2.0F;
constexpr float MeterHeightLines = 0.35F;
constexpr float NearLimit = 0.9F;

[[nodiscard]] u64 makeBoxKey(const MessageCatalog* catalog, const usize message,
                             const Language language, const usize box) {
    u64 key = std::bit_cast<std::uintptr_t>(catalog);
    for (const u64 part : {u64{message}, u64{static_cast<u8>(language)}, u64{box}}) {
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
    const s32 room = limit - TextStartX;
    const s32 used = line.endX - TextStartX;
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

} // namespace

MessagePanel::MessagePanel(const gfx::Device& device, MessageFocus& focus)
    : Panel("message", "Message", core::icons::Chat, core::DockSlot::Centre), focus(focus),
      boxRenderer(device) {}

void MessagePanel::showMessage() {
    setOpen(true);
    ImGui::SetWindowFocus(getWindowTitle().c_str());
}

const MessageLayout& MessagePanel::updateLayout(const Message& message) {
    const LayoutKey key{
        .catalog = focus.catalog.get(), .index = *focus.shown, .language = focus.language};
    if (layoutKey != key) {
        layoutKey = key;
        layout = focus.catalog->layOut(message, focus.language);
    }
    return layout;
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
    if (ImGui::BeginCombo("##language", getLanguageName(focus.language))) {
        for (const Language language : ShownLanguages) {
            if (ImGui::Selectable(getLanguageName(language), language == focus.language)) {
                focus.language = language;
            }
        }
        ImGui::EndCombo();
    }
}

void MessagePanel::drawBox(const usize index, const LaidBox& box) {
    ImGui::PushID(static_cast<int>(index));
    ImGui::TextColored(core::palette::TextMuted, "Box %zu · %zu %s · %s", index + 1,
                       box.lines.size(), box.lines.size() == 1 ? "line" : "lines",
                       describeEndIcon(box.endIcon));
    const float width = std::min(ImGui::GetContentRegionAvail().x, MaxBoxWidth);
    ImGui::BeginGroup();
    boxRenderer.draw(index, makeBoxKey(focus.catalog.get(), *focus.shown, focus.language, index),
                     box, focus.catalog->art, std::max(width, 1.0F));
    for (usize line = 0; line < box.lines.size(); ++line) {
        ImGui::PushID(static_cast<int>(line));
        drawMeter(box, box.lines[line], width);
        ImGui::PopID();
    }
    ImGui::EndGroup();
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
    ImGui::Separator();
    if (!isInLanguage(*message, focus.language, focus.catalog->isFontMessageEnglish)) {
        ImGui::TextColored(core::palette::TextMuted, "The build puts no %s text of this message.",
                           getLanguageName(focus.language));
        return;
    }
    const MessageLayout& laidOut = updateLayout(*message);
    for (const base::Diagnostic& diagnostic : laidOut.diagnostics) {
        ImGui::TextColored(diagnostic.severity == base::Severity::Error ? core::palette::Danger
                                                                        : core::palette::Warning,
                           "%s", diagnostic.message.c_str());
    }
    if (!ImGui::BeginChild("##storyboard")) {
        ImGui::EndChild();
        return;
    }
    for (usize index = 0; index < laidOut.boxes.size(); ++index) {
        drawBox(index, laidOut.boxes[index]);
    }
    ImGui::EndChild();
}

std::optional<u64> MessagePanel::findSelectionKey() const {
    if (focus.findShown() == nullptr) {
        return std::nullopt;
    }
    return makeBoxKey(focus.catalog.get(), *focus.shown, Language::Japanese, 0);
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
    core::drawPropertyLabel("ID");
    ImGui::Text("0x%04X", message->id);
    core::drawPropertyLabel("Box Type", "The box's frame and the colors its text is drawn in.");
    const std::string type = core::makeConstantLabel(message->boxType, "TEXTBOX_TYPE_");
    ImGui::TextUnformatted(type.c_str());
    core::drawConstantName(type, message->boxType);
    core::drawPropertyLabel("Box Position",
                            "Where on screen the box stands. Variable lets the game put it above "
                            "or below, by where the player and the actor spoken to stand.");
    const std::string position = core::makeConstantLabel(message->boxPosition, "TEXTBOX_POS_");
    ImGui::TextUnformatted(position.c_str());
    core::drawConstantName(position, message->boxPosition);
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

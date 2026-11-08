#include "CodeArguments.h"

#include "app/core/domain/ReadableName.h"
#include "app/core/ui/ConstantSelectable.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageCatalog.h"
#include "app/message/domain/MessageCodes.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cfloat>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

constexpr usize PreviewLength = 40;

[[nodiscard]] std::optional<u16> parseMessageId(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
    }
    u16 id = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id, 16);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return id;
}

[[nodiscard]] std::string makePreview(const MessageTable& table, const std::string_view argument,
                                      const Language language) {
    const std::optional<u16> id = parseMessageId(argument);
    if (!id) {
        return "no message ID";
    }
    for (const Message& message : table.messages) {
        if (message.id != *id) {
            continue;
        }
        const MessageText& text = message.texts[static_cast<usize>(language)];
        std::string preview;
        for (const Piece& piece : text.pieces) {
            if (const auto* run = std::get_if<TextPiece>(&piece)) {
                preview += run->text;
            } else if (std::holds_alternative<NewlinePiece>(piece)) {
                preview += ' ';
            }
            if (preview.size() >= PreviewLength) {
                break;
            }
        }
        return preview.size() > PreviewLength ? preview.substr(0, PreviewLength) + "…" : preview;
    }
    return "no such message";
}

[[nodiscard]] bool drawConstantChoice(std::string& value, const std::vector<std::string>& options,
                                      const std::string_view prefix, std::string& filter) {
    bool isChanged = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo("##constant", core::makeConstantLabel(value, prefix).c_str(),
                           ImGuiComboFlags_HeightLarge)) {
        return false;
    }
    if (ImGui::IsWindowAppearing()) {
        filter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter", &filter);
    for (const std::string& option : options) {
        const std::string label = core::makeConstantLabel(option, prefix);
        if (!core::isConstantFilteredIn(label, option, filter)) {
            continue;
        }
        if (core::drawConstantSelectable(label, option, option == value)) {
            value = option;
            isChanged = true;
        }
    }
    ImGui::EndCombo();
    return isChanged;
}

[[nodiscard]] bool drawArgument(std::string& value, const CodeArgument& argument,
                                const MessageCatalog& catalog, const MessageTable& table,
                                const Language language, std::string& filter) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    switch (argument.kind) {
    case ArgumentKind::Number:
        return ImGui::InputText("##number", &value, ImGuiInputTextFlags_CharsDecimal);
    case ArgumentKind::MessageId: {
        const bool isChanged = ImGui::InputText("##id", &value);
        ImGui::TextColored(core::palette::TextMuted, "%s",
                           makePreview(table, value, language).c_str());
        return isChanged;
    }
    case ArgumentKind::Item:
        return drawConstantChoice(value, catalog.items, "ITEM_", filter);
    case ArgumentKind::HighScore:
        return drawConstantChoice(value, catalog.highScores, "HS_", filter);
    case ArgumentKind::Sound:
        return drawConstantChoice(value, catalog.sounds, "NA_SE_", filter);
    }
    return false;
}

} // namespace

bool drawCodeArguments(CodePiece& code, const MessageCatalog& catalog, const MessageTable& table,
                       const Language language, std::string& filter) {
    const InsertableCode* insertable = findInsertableCode(code.name);
    if (insertable == nullptr || insertable->arguments.empty()) {
        return false;
    }
    if (code.arguments.size() < insertable->arguments.size()) {
        code.arguments.resize(insertable->arguments.size());
    }
    bool isChanged = false;
    if (!core::beginProperties("##codeArguments")) {
        return false;
    }
    for (usize index = 0; index < insertable->arguments.size(); ++index) {
        const CodeArgument& argument = insertable->arguments[index];
        ImGui::PushID(static_cast<int>(index));
        core::drawPropertyLabel(argument.label);
        isChanged =
            drawArgument(code.arguments[index], argument, catalog, table, language, filter) ||
            isChanged;
        ImGui::PopID();
    }
    core::endProperties();
    return isChanged;
}

} // namespace toki::message

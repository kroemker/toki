#include "MessageBrowser.h"

#include "app/core/domain/ReadableName.h"
#include "app/core/ui/Spinner.h"
#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageCatalog.h"

#include <imgui.h>

#include <algorithm>
#include <bit>
#include <cfloat>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace toki::message {

namespace {

constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                       ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV;

[[nodiscard]] bool drawChip(const char* label, const bool isOn) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          isOn ? core::palette::AccentMuted : core::palette::SurfaceRaised);
    ImGui::PushStyleColor(ImGuiCol_Text, isOn ? core::palette::Text : core::palette::TextMuted);
    const bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    return clicked;
}

void setupColumn(const char* label, const MessageColumn column, const ImGuiTableColumnFlags flags) {
    ImGui::TableSetupColumn(label, flags, 0.0F, static_cast<ImGuiID>(column));
}

[[nodiscard]] std::string makeBoxTypeLabel(const std::string_view name) {
    return core::makeConstantLabel(name, "TEXTBOX_TYPE_");
}

[[nodiscard]] std::string makePositionLabel(const std::string_view name) {
    return core::makeConstantLabel(name, "TEXTBOX_POS_");
}

} // namespace

MessageBrowser::MessageBrowser(MessageFocus& focus, BoxRenderer& boxRenderer,
                               ShowMessage showMessage)
    : focus(focus), boxRenderer(boxRenderer), showMessage(std::move(showMessage)) {}

void MessageBrowser::drawChips() {
    if (focus.catalog) {
        for (const std::string& type : listBoxTypes(focus.draft.getTable())) {
            const std::string label = makeBoxTypeLabel(type);
            ImGui::PushID(type.c_str());
            if (drawChip(label.c_str(), boxType == type)) {
                boxType = boxType == type ? std::nullopt : std::optional(type);
            }
            ImGui::PopID();
        }
    }
    ImGui::NewLine();
}

void MessageBrowser::updateRows(const RowsKey& key) {
    if (rowsKey == key) {
        return;
    }
    if (rowsKey) {
        RowsKey edited = *rowsKey;
        edited.revision = key.revision;
        const std::optional<usize> message = focus.draft.findOnlyEdited(rowsKey->revision);
        if (edited == key && message) {
            rowsKey = key;
            const auto row = std::ranges::find(rows, *message, &MessageRow::index);
            if (row != rows.end()) {
                *row = makeMessageRow(focus.draft.getTable().messages[*message], *message,
                                      key.language);
            }
            return;
        }
    }
    rowsKey = key;
    rows =
        makeMessageRows(focus.draft.getTable(), key.language, focus.catalog->isFontMessageEnglish,
                        MessageFilter{.text = key.filter, .boxType = key.boxType});
    sortMessageRows(rows, key.column, key.ascending);
}

void MessageBrowser::drawTable(const std::string& filter, const float height) {
    const ImVec2 start = ImGui::GetCursorPos();
    if (!focus.catalog) {
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, height));
        core::drawBusyCover(start, ImVec2(0.0F, height), "Loading messages");
        return;
    }
    if (!ImGui::BeginTable("##messages", 5, TableFlags, ImVec2(0.0F, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    setupColumn("ID", MessageColumn::Id,
                ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed);
    setupColumn("Text", MessageColumn::Text, ImGuiTableColumnFlags_WidthStretch);
    setupColumn("Type", MessageColumn::Type, ImGuiTableColumnFlags_WidthFixed);
    setupColumn("Position", MessageColumn::Position,
                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide);
    setupColumn("Boxes", MessageColumn::Boxes, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    RowsKey key{.revision = focus.draft.getRevision(),
                .language = focus.language,
                .filter = filter,
                .boxType = boxType};
    if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        specs != nullptr && specs->SpecsCount > 0) {
        key.column = static_cast<MessageColumn>(specs->Specs[0].ColumnUserID);
        key.ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
    }
    updateRows(key);

    if (rows.empty()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(core::palette::TextMuted, "No messages match.");
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            drawRow(rows[static_cast<usize>(index)]);
        }
    }
    ImGui::EndTable();
    if (focus.isReading) {
        core::drawBusyCover(start, ImVec2(0.0F, height), "Loading messages");
    }
}

void MessageBrowser::drawRow(const MessageRow& row) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(static_cast<int>(row.index));
    const std::string id = std::format("0x{:04X}", row.id);
    if (ImGui::Selectable(id.c_str(), focus.shown == row.index,
                          ImGuiSelectableFlags_SpanAllColumns)) {
        focus.shown = row.index;
        if (showMessage) {
            showMessage(row.index);
        }
    }
    ImGui::PopID();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.text.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(makeBoxTypeLabel(row.boxType).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(makePositionLabel(row.boxPosition).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%zu", row.boxCount);
}

void MessageBrowser::drawDetails() {
    const Message* message = focus.findShown();
    if (message == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Select a message to see it.");
        return;
    }
    ImGui::Text("0x%04X", message->id);
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "· %s · %s",
                       makeBoxTypeLabel(message->boxType).c_str(),
                       makePositionLabel(message->boxPosition).c_str());
    const LayoutKey key{.catalog = focus.catalog.get(),
                        .revision = focus.draft.getRevision(),
                        .index = *focus.shown,
                        .language = focus.language};
    if (layoutKey != key) {
        layoutKey = key;
        layout = focus.catalog->layOut(*message, focus.language);
    }
    if (layout.boxes.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "No text in this language.");
        return;
    }
    const float width = std::min(ImGui::GetContentRegionAvail().x,
                                 ImGui::GetContentRegionAvail().y * BoxRenderer::AspectRatio);
    boxRenderer.draw(0, layout.boxes.front(), focus.catalog->art, std::max(width, 1.0F));
}

} // namespace toki::message

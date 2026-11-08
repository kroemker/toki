#include "MarkupEditor.h"

#include "app/core/ui/Theme.h"
#include "app/message/domain/MessageMarkup.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <cfloat>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::message {

namespace {

[[nodiscard]] ImU32 findSpanColor(const MarkupSpan& span, const FindMarkupColor& findColor) {
    switch (span.kind) {
    case MarkupSpanKind::Text:
    case MarkupSpanKind::Brace:
    case MarkupSpanKind::NewLine:
        return ImGui::GetColorU32(core::palette::Text);
    case MarkupSpanKind::Code:
        return ImGui::GetColorU32(core::palette::Accent);
    case MarkupSpanKind::BoxBreak:
        return ImGui::GetColorU32(core::palette::Warning);
    case MarkupSpanKind::Color:
        if (const std::optional<u32> color = findColor ? findColor(span.color) : std::nullopt) {
            return *color;
        }
        return ImGui::GetColorU32(core::palette::Accent);
    case MarkupSpanKind::Open:
        return ImGui::GetColorU32(core::palette::Danger);
    }
    return ImGui::GetColorU32(core::palette::Text);
}

void drawSpans(const std::string& markup, ImGuiWindow& field, const ImGuiID id,
               const FindMarkupColor& findColor) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 origin{field.Pos.x + style.FramePadding.x,
                  field.Pos.y + style.FramePadding.y - field.Scroll.y};
    if (ImGui::GetActiveID() == id) {
        if (const ImGuiInputTextState* state = ImGui::GetInputTextState(id)) {
            origin.x -= state->Scroll.x;
        }
    }
    const f32 lineHeight = ImGui::GetFontSize();
    ImDrawList* drawList = field.DrawList;
    drawList->PushClipRect(field.InnerClipRect.Min, field.InnerClipRect.Max, true);
    ImVec2 at = origin;
    for (const MarkupSpan& span : findMarkupSpans(markup)) {
        if (span.kind == MarkupSpanKind::NewLine) {
            at = ImVec2(origin.x, at.y + lineHeight);
            continue;
        }
        if (at.y > field.InnerClipRect.Max.y) {
            break;
        }
        const char* begin = markup.data() + span.begin;
        const char* end = markup.data() + span.end;
        const f32 width = ImGui::CalcTextSize(begin, end).x;
        if (at.y + lineHeight >= field.InnerClipRect.Min.y) {
            drawList->AddText(at, findSpanColor(span, findColor), begin, end);
        }
        at.x += width;
    }
    drawList->PopClipRect();
}

} // namespace

bool drawMarkupEditor(const char* id, std::string& markup, ImFont* font, const f32 fontSize,
                      const FindMarkupColor& findColor, const bool isReloaded) {
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    const ImGuiID fieldId = ImGui::GetID(id);
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(fieldId);
        isReloaded && state != nullptr) {
        state->ReloadUserBufAndKeepSelection();
    }
    ImGui::PushFont(font, fontSize);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    const bool isChanged = ImGui::InputTextMultiline(id, &markup, ImVec2(-FLT_MIN, -FLT_MIN),
                                                     ImGuiInputTextFlags_NoUndoRedo);
    ImGui::PopStyleColor();
    // A hack until Dear ImGui's field can color its text: the child window the
    // field scrolls in is found by the name BeginChildEx gives it, which is
    // internal to Dear ImGui and may change with its version.
    const std::string childName = std::format("{}/{}_{:08X}", parent->Name, id, fieldId);
    if (ImGuiWindow* field = ImGui::FindWindowByName(childName.c_str())) {
        drawSpans(markup, *field, fieldId, findColor);
    }
    ImGui::PopFont();
    return isChanged;
}

} // namespace toki::message

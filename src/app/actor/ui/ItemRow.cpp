#include "ItemRow.h"

#include "app/actor/Types.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace toki::actor {

namespace {

constexpr float BadgeShare = 0.4F;
constexpr float DetailShare = 0.35F;
constexpr float TagAlpha = 0.2F;
constexpr float DetailsIndent = 1.5F;
constexpr const char* Ellipsis = "…";

[[nodiscard]] float measureText(const std::string& text) {
    return ImGui::CalcTextSize(text.c_str()).x;
}

[[nodiscard]] float measureBox(const std::string& text) {
    return measureText(text) + (ImGui::GetStyle().FramePadding.x * 2.0F);
}

float drawBox(ImDrawList& drawList, const ImVec2 at, const std::string& text, const ImVec4& color,
              const ImVec4& background) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = measureBox(text);
    const float height = ImGui::GetTextLineHeight() + style.FramePadding.y;
    const float top = at.y + ((ImGui::GetFrameHeight() - height) * 0.5F);
    drawList.AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width, top + height),
                           ImGui::GetColorU32(background), style.FrameRounding);
    drawList.AddText(ImVec2(at.x + style.FramePadding.x, top + (style.FramePadding.y * 0.5F)),
                     ImGui::GetColorU32(color), text.c_str());
    return width;
}

struct FittedBadges {
    std::vector<RowBadge> shown;
    float width = 0.0F;
};

[[nodiscard]] FittedBadges fitBadges(const std::vector<RowBadge>& badges, const float space) {
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    FittedBadges fitted;
    for (usize i = 0; i < badges.size(); ++i) {
        const float width = measureBox(badges[i].text) + spacing;
        if (fitted.width + width > space) {
            fitted.shown.push_back(RowBadge{.text = std::format("+{}", badges.size() - i),
                                            .color = core::palette::TextMuted});
            fitted.width += measureBox(fitted.shown.back().text) + spacing;
            break;
        }
        fitted.shown.push_back(badges[i]);
        fitted.width += width;
    }
    return fitted;
}

[[nodiscard]] std::string describeRow(const ItemRow& row) {
    std::string described = row.name;
    if (!row.detail.empty()) {
        described += "  ·  " + row.detail;
    }
    for (const RowBadge& badge : row.badges) {
        described += "  ·  " + badge.text;
    }
    return described;
}

[[nodiscard]] RowAction drawTools(const ItemRow& row, const ImVec2 at) {
    RowAction action = RowAction::None;
    const float size = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(at);
    const auto drawTool = [&](const char* icon, const char* tooltip, const RowAction clicked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        if (ImGui::Button(icon, ImVec2(size, size))) {
            action = clicked;
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine(0.0F, 0.0F);
    };
    if (row.canRaise) {
        drawTool(core::icons::ArrowUp, "Move it up", RowAction::Raise);
    }
    if (row.canCopy) {
        drawTool(core::icons::Copy, "Copy it", RowAction::Copy);
    }
    if (row.canRemove) {
        drawTool(core::icons::Trash, "Remove it", RowAction::Remove);
    }
    ImGui::NewLine();
    return action;
}

} // namespace

std::string makeFittedText(const std::string& text, const float width) {
    if (measureText(text) <= width) {
        return text;
    }
    const float ellipsis = ImGui::CalcTextSize(Ellipsis).x;
    usize length = text.size();
    while (length > 0) {
        --length;
        while (length > 0 && (static_cast<u8>(text[length]) & 0xC0U) == 0x80U) {
            --length;
        }
        if (ImGui::CalcTextSize(text.data(), text.data() + length).x + ellipsis <= width) {
            break;
        }
    }
    return text.substr(0, length) + Ellipsis;
}

RowResult drawItemRow(const char* id, const ItemRow& row) {
    ImGui::PushID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID openId = ImGui::GetID("##open");
    bool isOpen = storage->GetBool(openId, false);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    const usize toolCount =
        (row.canRaise ? 1U : 0U) + (row.canCopy ? 1U : 0U) + (row.canRemove ? 1U : 0U);
    const float toolsWidth = static_cast<float>(toolCount) * height;
    const bool isHovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseHoveringRect(start, ImVec2(start.x + width, start.y + height));

    if (ImGui::Selectable("##row", isOpen, ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(width, height))) {
        isOpen = !isOpen;
        storage->SetBool(openId, isOpen);
    }
    const bool isTextHovered = ImGui::IsItemHovered();

    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    float x = start.x + style.FramePadding.x;
    if (!row.tag.empty()) {
        const ImVec4 background(row.tagColor.x, row.tagColor.y, row.tagColor.z, TagAlpha);
        x += drawBox(drawList, ImVec2(x, start.y), row.tag, row.tagColor, background) +
             style.ItemSpacing.x;
    }
    const float right = start.x + width - toolsWidth - style.ItemSpacing.x;
    const float space = std::max(right - x, 0.0F);
    const FittedBadges badges = fitBadges(row.badges, space * BadgeShare);
    const float textSpace = std::max(space - badges.width, 0.0F);
    const std::string detail = row.detail.empty()
                                   ? std::string()
                                   : makeFittedText(row.detail, std::min(measureText(row.detail),
                                                                         textSpace * DetailShare));
    const float detailWidth = detail.empty() ? 0.0F : measureText(detail) + style.ItemSpacing.x;
    const std::string name = makeFittedText(row.name, std::max(textSpace - detailWidth, 0.0F));
    const float textY = start.y + style.FramePadding.y;
    drawList.AddText(ImVec2(x, textY), ImGui::GetColorU32(core::palette::Text), name.c_str());
    x += measureText(name) + style.ItemSpacing.x;
    if (!detail.empty()) {
        drawList.AddText(ImVec2(x, textY), ImGui::GetColorU32(core::palette::TextMuted),
                         detail.c_str());
    }
    float badgeX = right - badges.width;
    for (const RowBadge& badge : badges.shown) {
        badgeX += drawBox(drawList, ImVec2(badgeX, start.y), badge.text, badge.color,
                          core::palette::SurfaceRaised) +
                  style.ItemInnerSpacing.x;
    }

    RowResult result{.isOpen = isOpen};
    if (isHovered && toolCount > 0) {
        result.action = drawTools(row, ImVec2(start.x + width - toolsWidth, start.y));
    }
    if (isTextHovered &&
        (name != row.name || detail != row.detail || badges.shown.size() != row.badges.size())) {
        ImGui::SetTooltip("%s", describeRow(row).c_str());
    }
    ImGui::PopID();
    return result;
}

void beginRowDetails() {
    ImGui::Indent(ImGui::GetFontSize() * DetailsIndent);
    ImGui::BeginGroup();
}

void endRowDetails() {
    ImGui::EndGroup();
    const ImVec2 top = ImGui::GetItemRectMin();
    const ImVec2 bottom = ImGui::GetItemRectMax();
    const float x = top.x - (ImGui::GetFontSize() * DetailsIndent * 0.5F);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, top.y), ImVec2(x, bottom.y),
                                        ImGui::GetColorU32(core::palette::Accent), 2.0F);
    ImGui::Unindent(ImGui::GetFontSize() * DetailsIndent);
    ImGui::Spacing();
}

} // namespace toki::actor

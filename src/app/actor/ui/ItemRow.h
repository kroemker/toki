#pragma once

#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// A short note at the end of an item row on what is set, "xlu" or "faces cam".
struct RowBadge {
    std::string text;
    ImVec4 color;
};

/// One line of a list of parts or children: a coloured kind tag, a name, a
/// muted detail, and badges, all cut to the width there is, never wrapped.
/// `tag` is left out when empty. The tools to move it up, copy and remove it
/// are shown while the row is hovered.
struct ItemRow {
    std::string tag;
    ImVec4 tagColor;
    std::string name;
    std::string detail;
    std::vector<RowBadge> badges;
    bool canRaise = false;
    bool canCopy = false;
    bool canRemove = false;
};

/// What was clicked on an item row.
enum class RowAction {
    None,
    Raise,
    Copy,
    Remove,
};

/// What drawing an item row did: what was clicked, and whether the row is
/// open, which clicking it toggles and ImGui keeps by the row's id.
struct RowResult {
    RowAction action = RowAction::None;
    bool isOpen = false;
};

/// Draws `row` across the width there is, under the id `id`.
[[nodiscard]] RowResult drawItemRow(const char* id, const ItemRow& row);

/// Starts what an open row shows, indented below it and marked by a line on
/// its left, and ends it.
void beginRowDetails();
void endRowDetails();

/// The colour of a card's WHEN band, and of its background.
inline constexpr ImVec4 WhenColor = core::palette::Warning;
inline constexpr ImVec4 WhenBackground{0.910F, 0.718F, 0.314F, 0.07F};

/// Starts a card's bands, a table of a labelled gutter and the band's body,
/// and ends them; `beginBands` says whether to draw them.
[[nodiscard]] bool beginBands();
void endBands();

/// Starts one band of a card: its label in `color` in the gutter, the row
/// tinted by `background` when given, and the cursor in its body.
void drawBand(const char* label, const ImVec4& color, const std::optional<ImVec4>& background);

/// `text` as it fits in `width`, cut with "…" when it does not.
[[nodiscard]] std::string makeFittedText(const std::string& text, float width);

} // namespace toki::actor

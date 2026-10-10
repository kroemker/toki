#pragma once

#include <imgui.h>

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

/// `text` as it fits in `width`, cut with "…" when it does not.
[[nodiscard]] std::string makeFittedText(const std::string& text, float width);

} // namespace toki::actor

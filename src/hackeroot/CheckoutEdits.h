#pragma once

#include "hackeroot/EditPlan.h"
#include "hackeroot/Types.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {

/// Sets the `#define name` on line `line` (from 1) of `text` to `value`,
/// keeping its indent and a comment after it. The line must define `name`
/// itself, not a name that starts with it, as `written`, the value it was read
/// with; otherwise the file moved on since it was read, and nothing changes:
/// the status is `Add`, as for a line that defines nothing.
[[nodiscard]] EditStatus setDefine(std::string& text, usize line, std::string_view name,
                                   std::string_view written, std::string_view value);

/// Where something is in a text, from its first character to one past its last.
struct TextRange {
    usize begin = 0;
    usize end = 0;
};

/// The comment that marks what Toki manages in a checkout's code: `// toki: <what>`.
/// A block Toki adds entries to is marked `// toki: <title> begin` and
/// `// toki: <title> end`, and a file Toki writes whole, such as a scene's, is
/// marked `// toki: scene`.
[[nodiscard]] std::string makeTokiMarker(std::string_view what);

/// The block of `text` Toki adds `title`'s entries to, between
/// `// toki: <title> begin` and `// toki: <title> end`, its first line to one
/// past its last, if it has one.
[[nodiscard]] std::optional<TextRange> findManagedBlock(std::string_view text,
                                                        std::string_view title);

/// Adds `entry` at the end of the block Toki adds `title`'s entries to, made
/// before `anchor` the first time, where a line starts with it, or at the end
/// of `text` without one, so a file whose last lines must stay last, as
/// `#if TESTSUITE_MODE`, keeps them.
void addToManagedBlock(std::string& text, std::string_view title, std::string_view entry,
                       std::string_view anchor);

/// The `beginseg` .. `endseg` of `spec` named `name`.
[[nodiscard]] std::optional<TextRange> findSpecSegment(std::string_view spec,
                                                       std::string_view name);

/// The lines of `spec`'s segment named `name` that writing `block` in its place
/// would lose, as a person wrote them, trimmed: what was added by hand to a
/// segment a tool writes. None when there is no such segment.
[[nodiscard]] std::vector<std::string>
listLostSegmentLines(std::string_view spec, std::string_view name, std::string_view block);

/// Makes the segment named `name` of `spec` read `block`: as it is, replaced
/// where it is, or added to Toki's block of `title` before `anchor`.
[[nodiscard]] EditStatus putSpecSegment(std::string& spec, std::string_view name,
                                        std::string_view block, std::string_view title,
                                        std::string_view anchor);

/// Makes the segment named `name` of `spec` link `objects`, in order, as
/// `$(BUILD_DIR)/<object>`, in place of the files it included, keeping every
/// other line it has: what a person set, such as `romalign`, stays. Nothing
/// when `spec` has no such segment.
[[nodiscard]] std::optional<EditStatus>
setSpecSegmentIncludes(std::string& spec, std::string_view name,
                       std::span<const std::string> objects);

/// `putSpecSegment` as an item of `plan`'s edit `spec`, labelled `label`, and a
/// warning in `plan` naming each line the segment had that its rewrite loses
/// (`listLostSegmentLines`), since those are someone's own.
[[nodiscard]] EditItem putSpecSegmentItem(FileEdit& spec, EditPlan& plan, std::string_view name,
                                          std::string_view block, std::string_view title,
                                          std::string_view anchor, std::string label);

/// Replaces the first call to `unsetMacro` in `text`, `DEFINE_ACTOR_UNSET(...)`,
/// with `entry`, leaving the slot's ID comment in front of it, unless its
/// argument is one of `kept`. False when there is none.
[[nodiscard]] bool replaceUnsetEntry(std::string& text, std::string_view unsetMacro,
                                     std::string_view entry,
                                     const std::vector<std::string_view>& kept);

/// Inserts `line` before the line of `text` that is `anchor` once its spaces
/// are trimmed, or at the end of `text` without one.
void insertLineBefore(std::string& text, std::string_view anchor, std::string_view line);

} // namespace hackeroot

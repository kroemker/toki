#pragma once

#include "hackeroot/Types.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {

/// Whether a thing an edit makes so is so already, is added, or is changed.
enum class EditStatus : u8 {
    Present,
    Add,
    Update,
};

/// One thing an edit makes so, as a person reads it:
/// `scene segment "oceantest_scene"`.
struct EditItem {
    EditStatus status = EditStatus::Present;
    std::string label;
};

/// A file of the checkout and what an edit makes of it: its text as it was
/// read, with `\n` line endings, the text it is to be written as, and each
/// thing that changes. A file that does not exist yet has no text before.
struct FileEdit {
    std::filesystem::path file;
    std::optional<std::string> before;
    std::string after;
    std::vector<EditItem> items;
};

/// The edits a tool would make to a checkout, to be shown before they are
/// made, and what the tool warns of.
struct EditPlan {
    std::vector<FileEdit> files;
    std::vector<std::string> warnings;

    /// Whether applying it changes any file.
    [[nodiscard]] bool hasWork() const;
};

/// What `applyEditPlan` wrote, by path relative to the checkout.
struct EditsApplied {
    std::vector<std::filesystem::path> written;
};

/// The text of `file` with `\n` line endings, or why it cannot be read.
[[nodiscard]] std::expected<std::string, std::string>
readCheckoutText(const std::filesystem::path& file);

/// Writes every file of `plan` whose text changes, under `repository`, each
/// with the line endings it had (`\n` for a new one), through a file beside
/// it that replaces it whole. Nothing is written when a file is no longer
/// what the plan read, or a new one exists already: the plan is stale and is
/// made again. When a write fails, every file written before it is put back as
/// it was, and every file and folder made is removed, so a plan is written
/// whole or not at all.
[[nodiscard]] std::expected<EditsApplied, std::string>
applyEditPlan(const std::filesystem::path& repository, const EditPlan& plan);

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

/// The block of `text` Toki adds `title`'s entries to, between
/// `// Toki64 <title> — managed block` and `// Toki64 end`, its first line to
/// one past its last, if it has one.
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

#pragma once

#include "hackeroot/Types.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hackeroot {

/// Whether a thing an edit makes so is so already, is added, is changed, or is
/// removed.
enum class EditStatus : u8 {
    Present,
    Add,
    Update,
    Remove,
};

/// One thing an edit makes so, as a person reads it:
/// `scene segment "oceantest_scene"`.
struct EditItem {
    EditStatus status = EditStatus::Present;
    std::string label;
};

/// A file of the checkout and what an edit makes of it: its text as it was
/// read, with `\n` line endings, the text it is to be written as, and each
/// thing that changes. A file that does not exist yet has no text before, and
/// a file the edit removes has no text after it.
struct FileEdit {
    std::filesystem::path file;
    std::optional<std::string> before;
    std::string after;
    bool isRemoved = false;
    std::vector<EditItem> items;
};

/// A file copied into the checkout as it is, byte for byte, such as the
/// textures of a scene that is copied out of `extracted/`. `isPresent` when
/// the copy is there already, with the same bytes.
struct FileCopy {
    std::filesystem::path from;
    std::filesystem::path to;
    bool isPresent = false;
};

/// The edits a tool would make to a checkout, to be shown before they are
/// made, and what the tool warns of.
struct EditPlan {
    std::vector<FileEdit> files;
    std::vector<FileCopy> copies;
    std::vector<std::string> warnings;

    /// Whether applying it changes any file.
    [[nodiscard]] bool hasWork() const;
};

/// What `applyEditPlan` did, by path relative to the checkout.
struct EditsApplied {
    std::vector<std::filesystem::path> written;
    std::vector<std::filesystem::path> removed;
};

/// The text of `file` with `\n` line endings, or why it cannot be read.
[[nodiscard]] std::expected<std::string, std::string>
readCheckoutText(const std::filesystem::path& file);

/// A copy of `from`, an absolute path, to `to` under `repository`, there
/// already when `to` holds the same bytes.
[[nodiscard]] FileCopy makeFileCopy(const std::filesystem::path& repository,
                                    std::filesystem::path from, std::filesystem::path to);

/// Writes every file of `plan` whose text changes, under `repository`, each
/// with the line endings it had (`\n` for a new one), through a file beside
/// it that replaces it whole, then copies its copies and removes the files it
/// removes. Nothing is done when a file is no longer what the plan read, a new
/// one or a copy's destination exists already, or a copy's source cannot be
/// read: the plan is stale and is made again. When a step fails, every one
/// before it is undone, and every folder made is removed, so a plan is applied
/// whole or not at all.
[[nodiscard]] std::expected<EditsApplied, std::string>
applyEditPlan(const std::filesystem::path& repository, const EditPlan& plan);

} // namespace hackeroot

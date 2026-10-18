#include "EditPlan.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

[[nodiscard]] std::string normalizeLineEndings(const std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (usize i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        normalized += text[i];
    }
    return normalized;
}

[[nodiscard]] std::string makeLineEndings(const std::string_view text,
                                          const std::string_view lineEnding) {
    if (lineEnding == "\n") {
        return std::string(text);
    }
    std::string written;
    written.reserve(text.size() + (text.size() / 32));
    for (const char letter : text) {
        if (letter == '\n') {
            written += lineEnding;
        } else {
            written += letter;
        }
    }
    return written;
}

[[nodiscard]] std::expected<std::string, std::string> readRaw(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::unexpected(std::format("cannot read {}", file.string()));
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

[[nodiscard]] std::expected<void, std::string> writeWhole(const std::filesystem::path& file,
                                                          const std::string& text) {
    std::error_code status;
    std::filesystem::create_directories(file.parent_path(), status);
    if (status) {
        return std::unexpected(
            std::format("cannot create {}: {}", file.parent_path().string(), status.message()));
    }
    std::filesystem::path written = file;
    written += ".toki-writing";
    {
        std::ofstream stream(written, std::ios::binary | std::ios::trunc);
        stream << text;
        stream.close();
        if (!stream) {
            std::filesystem::remove(written, status);
            return std::unexpected(std::format("cannot write {}", written.string()));
        }
    }
    std::filesystem::rename(written, file, status);
    if (status) {
        std::error_code ignored;
        std::filesystem::remove(written, ignored);
        return std::unexpected(
            std::format("cannot replace {}: {}", file.string(), status.message()));
    }
    return {};
}

[[nodiscard]] bool isChanged(const FileEdit& edit) {
    return edit.isRemoved || !edit.before || *edit.before != edit.after;
}

[[nodiscard]] std::vector<std::filesystem::path>
listMissingFolders(const std::filesystem::path& repository, std::filesystem::path folder) {
    std::vector<std::filesystem::path> missing;
    std::error_code status;
    while (!folder.empty() && folder != repository && !std::filesystem::exists(folder, status)) {
        missing.push_back(folder);
        folder = folder.parent_path();
    }
    return missing;
}

/// Applies a plan step by step, keeping how to undo each step taken, so that
/// a failed step puts back everything before it.
class PlanApplier {
public:
    PlanApplier(const std::filesystem::path& repository, const EditPlan& plan)
        : repository(repository), plan(plan) {}

    [[nodiscard]] std::expected<EditsApplied, std::string> apply();

private:
    [[nodiscard]] std::expected<void, std::string> check();
    [[nodiscard]] std::expected<void, std::string> writeFiles();
    [[nodiscard]] std::expected<void, std::string> copyFiles();
    [[nodiscard]] std::expected<void, std::string> removeFiles();
    void undo();

    const std::filesystem::path& repository;
    const EditPlan& plan;
    std::vector<std::string> lineEndings;
    std::vector<std::function<void()>> undoSteps;
    std::vector<std::filesystem::path> madeFolders;
    EditsApplied applied;
};

std::expected<void, std::string> PlanApplier::check() {
    for (const FileEdit& edit : plan.files) {
        const std::filesystem::path path = repository / edit.file;
        lineEndings.emplace_back("\n");
        if (!isChanged(edit)) {
            continue;
        }
        std::error_code status;
        const bool exists = std::filesystem::exists(path, status);
        if (!edit.before) {
            if (exists) {
                return std::unexpected(std::format("{} exists already, so nothing was written",
                                                   edit.file.generic_string()));
            }
            continue;
        }
        const std::expected<std::string, std::string> raw = readRaw(path);
        if (!raw) {
            return std::unexpected(raw.error());
        }
        if (normalizeLineEndings(*raw) != *edit.before) {
            return std::unexpected(
                std::format("{} changed since it was read, so nothing was written; look again",
                            edit.file.generic_string()));
        }
        if (raw->contains("\r\n")) {
            lineEndings.back() = "\r\n";
        }
    }
    for (const FileCopy& copy : plan.copies) {
        if (copy.isPresent) {
            continue;
        }
        std::error_code status;
        if (std::filesystem::exists(repository / copy.to, status)) {
            return std::unexpected(
                std::format("{} exists already, so nothing was written", copy.to.generic_string()));
        }
        if (!std::filesystem::is_regular_file(copy.from, status)) {
            return std::unexpected(std::format("{} cannot be read, so nothing was written",
                                               copy.from.generic_string()));
        }
    }
    return {};
}

std::expected<void, std::string> PlanApplier::writeFiles() {
    for (usize i = 0; i < plan.files.size(); ++i) {
        const FileEdit& edit = plan.files[i];
        if (!isChanged(edit) || edit.isRemoved) {
            continue;
        }
        const std::filesystem::path path = repository / edit.file;
        if (!edit.before) {
            std::ranges::move(listMissingFolders(repository, path.parent_path()),
                              std::back_inserter(madeFolders));
        }
        if (std::expected<void, std::string> written =
                writeWhole(path, makeLineEndings(edit.after, lineEndings[i]));
            !written) {
            return written;
        }
        undoSteps.emplace_back([path, &edit, ending = lineEndings[i]] {
            if (edit.before) {
                static_cast<void>(writeWhole(path, makeLineEndings(*edit.before, ending)));
            } else {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        });
        applied.written.push_back(edit.file);
    }
    return {};
}

std::expected<void, std::string> PlanApplier::copyFiles() {
    for (const FileCopy& copy : plan.copies) {
        if (copy.isPresent) {
            continue;
        }
        const std::filesystem::path path = repository / copy.to;
        std::ranges::move(listMissingFolders(repository, path.parent_path()),
                          std::back_inserter(madeFolders));
        std::error_code status;
        std::filesystem::create_directories(path.parent_path(), status);
        if (!status) {
            std::filesystem::copy_file(copy.from, path, status);
        }
        if (status) {
            return std::unexpected(std::format("cannot copy {} to {}: {}", copy.from.string(),
                                               path.string(), status.message()));
        }
        undoSteps.emplace_back([path] {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        });
        applied.written.push_back(copy.to);
    }
    return {};
}

std::expected<void, std::string> PlanApplier::removeFiles() {
    for (usize i = 0; i < plan.files.size(); ++i) {
        const FileEdit& edit = plan.files[i];
        if (!edit.isRemoved || !edit.before) {
            continue;
        }
        const std::filesystem::path path = repository / edit.file;
        std::error_code status;
        std::filesystem::remove(path, status);
        if (status) {
            return std::unexpected(
                std::format("cannot remove {}: {}", path.string(), status.message()));
        }
        undoSteps.emplace_back([path, &edit, ending = lineEndings[i]] {
            static_cast<void>(writeWhole(path, makeLineEndings(*edit.before, ending)));
        });
        applied.removed.push_back(edit.file);
    }
    return {};
}

void PlanApplier::undo() {
    for (auto step = undoSteps.rbegin(); step != undoSteps.rend(); ++step) {
        (*step)();
    }
    std::ranges::sort(madeFolders, std::greater{},
                      [](const std::filesystem::path& folder) { return folder.native().size(); });
    for (const std::filesystem::path& folder : madeFolders) {
        std::error_code ignored;
        std::filesystem::remove(folder, ignored);
    }
}

std::expected<EditsApplied, std::string> PlanApplier::apply() {
    if (std::expected<void, std::string> checked = check(); !checked) {
        return std::unexpected(checked.error());
    }
    for (const auto step :
         {&PlanApplier::writeFiles, &PlanApplier::copyFiles, &PlanApplier::removeFiles}) {
        if (std::expected<void, std::string> done = (this->*step)(); !done) {
            undo();
            return std::unexpected(
                std::format("{}; everything done before it was put back as it was", done.error()));
        }
    }
    return std::move(applied);
}

} // namespace

bool EditPlan::hasWork() const {
    return std::ranges::any_of(files, isChanged) ||
           std::ranges::any_of(copies, [](const FileCopy& copy) { return !copy.isPresent; });
}

std::expected<std::string, std::string> readCheckoutText(const std::filesystem::path& file) {
    std::expected<std::string, std::string> raw = readRaw(file);
    if (!raw) {
        return raw;
    }
    return normalizeLineEndings(*raw);
}

FileCopy makeFileCopy(const std::filesystem::path& repository, std::filesystem::path from,
                      std::filesystem::path to) {
    FileCopy copy{.from = std::move(from), .to = std::move(to)};
    const std::expected<std::string, std::string> existing = readRaw(repository / copy.to);
    if (existing) {
        const std::expected<std::string, std::string> source = readRaw(copy.from);
        copy.isPresent = source && *source == *existing;
    }
    return copy;
}

std::expected<EditsApplied, std::string> applyEditPlan(const std::filesystem::path& repository,
                                                       const EditPlan& plan) {
    return PlanApplier(repository, plan).apply();
}

} // namespace hackeroot

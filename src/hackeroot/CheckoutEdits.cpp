#include "CheckoutEdits.h"

#include "hackeroot/Types.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::string_view ManagedEnd = "// Toki64 end";
constexpr std::string_view DefineDirective = "#define ";

[[nodiscard]] std::string makeManagedBegin(const std::string_view title) {
    return std::format("// Toki64 {} — managed block", title);
}

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
    return !edit.before || *edit.before != edit.after;
}

[[nodiscard]] std::string_view trimSpaces(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::optional<TextRange> findLine(const std::string& text, const usize line) {
    usize begin = 0;
    for (usize at = 1; at < line; ++at) {
        const usize newline = text.find('\n', begin);
        if (newline == std::string::npos) {
            return std::nullopt;
        }
        begin = newline + 1;
    }
    if (begin > text.size()) {
        return std::nullopt;
    }
    const usize end = std::min(text.find('\n', begin), text.size());
    return TextRange{.begin = begin, .end = end};
}

} // namespace

bool EditPlan::hasWork() const {
    return std::ranges::any_of(files, isChanged);
}

std::expected<std::string, std::string> readCheckoutText(const std::filesystem::path& file) {
    std::expected<std::string, std::string> raw = readRaw(file);
    if (!raw) {
        return raw;
    }
    return normalizeLineEndings(*raw);
}

std::expected<EditsApplied, std::string> applyEditPlan(const std::filesystem::path& repository,
                                                       const EditPlan& plan) {
    std::vector<std::string> lineEndings;
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
    EditsApplied applied;
    for (usize i = 0; i < plan.files.size(); ++i) {
        const FileEdit& edit = plan.files[i];
        if (!isChanged(edit)) {
            continue;
        }
        if (std::expected<void, std::string> written =
                writeWhole(repository / edit.file, makeLineEndings(edit.after, lineEndings[i]));
            !written) {
            return std::unexpected(written.error());
        }
        applied.written.push_back(edit.file);
    }
    return applied;
}

EditStatus setDefine(std::string& text, const usize line, const std::string_view name,
                     const std::string_view value) {
    const std::optional<TextRange> range = findLine(text, line);
    if (!range) {
        return EditStatus::Add;
    }
    const std::string_view written(text.data() + range->begin, range->end - range->begin);
    const usize indent = written.find_first_not_of(" \t");
    if (indent == std::string_view::npos) {
        return EditStatus::Add;
    }
    std::string_view rest = written.substr(indent);
    if (!rest.starts_with(DefineDirective)) {
        return EditStatus::Add;
    }
    rest.remove_prefix(DefineDirective.size());
    rest = trimSpaces(rest);
    if (!rest.starts_with(name)) {
        return EditStatus::Add;
    }
    rest.remove_prefix(name.size());
    const usize comment = rest.find("//");
    const std::string_view current =
        trimSpaces(comment == std::string_view::npos ? rest : rest.substr(0, comment));
    if (current == value) {
        return EditStatus::Present;
    }
    std::string replaced =
        std::format("{}{}{} {}", written.substr(0, indent), DefineDirective, name, value);
    if (comment != std::string_view::npos) {
        replaced += ' ';
        replaced += rest.substr(comment);
    }
    text.replace(range->begin, range->end - range->begin, replaced);
    return EditStatus::Update;
}

std::optional<TextRange> findManagedBlock(const std::string_view text,
                                          const std::string_view title) {
    const std::string begin = makeManagedBegin(title);
    const usize start = text.find(begin);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const usize end = text.find(ManagedEnd, start + begin.size());
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return TextRange{.begin = start, .end = end + ManagedEnd.size()};
}

void addToManagedBlock(std::string& text, const std::string_view title,
                       const std::string_view entry, const std::string_view anchor) {
    if (!findManagedBlock(text, title)) {
        const std::string block = std::format("{}\n{}\n\n", makeManagedBegin(title), ManagedEnd);
        const usize at = anchor.empty() ? std::string::npos : text.find(anchor);
        if (at != std::string::npos) {
            text.insert(at, block);
        } else {
            if (!text.empty() && !text.ends_with('\n')) {
                text += '\n';
            }
            text += '\n';
            text += block;
        }
    }
    const std::optional<TextRange> block = findManagedBlock(text, title);
    text.insert(block->end - ManagedEnd.size(), std::format("{}\n\n", entry));
}

std::optional<TextRange> findSpecSegment(const std::string_view spec, const std::string_view name) {
    const std::string marker = std::format("name \"{}\"", name);
    usize at = 0;
    while ((at = spec.find(marker, at)) != std::string_view::npos) {
        const usize after = at + marker.size();
        if (after < spec.size() && spec[after] != '\n' && spec[after] != '\r' &&
            spec[after] != ' ') {
            at = after;
            continue;
        }
        const usize begin = spec.rfind("beginseg", at);
        const usize end = spec.find("endseg", after);
        if (begin == std::string_view::npos || end == std::string_view::npos) {
            return std::nullopt;
        }
        return TextRange{.begin = begin, .end = end + std::string_view("endseg").size()};
    }
    return std::nullopt;
}

EditStatus putSpecSegment(std::string& spec, const std::string_view name,
                          const std::string_view block, const std::string_view title,
                          const std::string_view anchor) {
    if (const std::optional<TextRange> found = findSpecSegment(spec, name)) {
        if (std::string_view(spec).substr(found->begin, found->end - found->begin) == block) {
            return EditStatus::Present;
        }
        spec.replace(found->begin, found->end - found->begin, block);
        return EditStatus::Update;
    }
    addToManagedBlock(spec, title, block, anchor);
    return EditStatus::Add;
}

bool replaceUnsetEntry(std::string& text, const std::string_view unsetMacro,
                       const std::string_view entry, const std::vector<std::string_view>& kept) {
    usize at = 0;
    while ((at = text.find(unsetMacro, at)) != std::string::npos) {
        const usize open = at + unsetMacro.size();
        const usize close = text.find(')', open);
        if (close == std::string::npos) {
            return false;
        }
        const std::string_view argument =
            trimSpaces(std::string_view(text).substr(open, close - open));
        if (std::ranges::contains(kept, argument)) {
            at = close;
            continue;
        }
        text.replace(at, close + 1 - at, entry);
        return true;
    }
    return false;
}

void insertLineBefore(std::string& text, const std::string_view anchor,
                      const std::string_view line) {
    usize begin = 0;
    while (begin < text.size()) {
        const usize end = std::min(text.find('\n', begin), text.size());
        if (trimSpaces(std::string_view(text).substr(begin, end - begin)) == anchor) {
            text.insert(begin, std::format("{}\n", line));
            return;
        }
        begin = end + 1;
    }
    if (!text.empty() && !text.ends_with('\n')) {
        text += '\n';
    }
    text += std::format("{}\n", line);
}

} // namespace hackeroot

#include "CheckoutEdits.h"

#include "hackeroot/TextLines.h"
#include "hackeroot/Types.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::string_view DefineDirective = "#define ";

[[nodiscard]] std::string makeManagedBegin(const std::string_view title) {
    return makeTokiMarker(std::format("{} begin", title));
}

[[nodiscard]] std::string makeManagedEnd(const std::string_view title) {
    return makeTokiMarker(std::format("{} end", title));
}

[[nodiscard]] usize findLineStart(const std::string_view text, const std::string_view anchor) {
    if (anchor.empty()) {
        return std::string_view::npos;
    }
    usize at = 0;
    while ((at = text.find(anchor, at)) != std::string_view::npos) {
        if (at == 0 || text[at - 1] == '\n') {
            return at;
        }
        ++at;
    }
    return std::string_view::npos;
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

std::string makeTokiMarker(const std::string_view what) {
    return std::format("// toki: {}", what);
}

EditStatus setDefine(std::string& text, const usize line, const std::string_view name,
                     const std::string_view written, const std::string_view value) {
    const std::optional<TextRange> range = findLine(text, line);
    if (!range) {
        return EditStatus::Add;
    }
    const std::string_view original(text.data() + range->begin, range->end - range->begin);
    const usize indent = original.find_first_not_of(" \t");
    if (indent == std::string_view::npos) {
        return EditStatus::Add;
    }
    std::string_view rest = original.substr(indent);
    if (!rest.starts_with(DefineDirective)) {
        return EditStatus::Add;
    }
    rest.remove_prefix(DefineDirective.size());
    rest = trimSpaces(rest);
    if (!rest.starts_with(name) ||
        (rest.size() > name.size() && rest[name.size()] != ' ' && rest[name.size()] != '\t')) {
        return EditStatus::Add;
    }
    rest.remove_prefix(name.size());
    const usize comment = std::min(rest.find("//"), rest.find("/*"));
    if (comment != std::string_view::npos && rest.substr(comment).starts_with("/*") &&
        !rest.substr(comment).contains("*/")) {
        return EditStatus::Add;
    }
    const std::string_view current =
        trimSpaces(comment == std::string_view::npos ? rest : rest.substr(0, comment));
    if (current == value) {
        return EditStatus::Present;
    }
    if (current != written) {
        return EditStatus::Add;
    }
    std::string replaced =
        std::format("{}{}{} {}", original.substr(0, indent), DefineDirective, name, value);
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
    const std::string end = makeManagedEnd(title);
    const usize start = text.find(begin);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const usize last = text.find(end, start + begin.size());
    if (last == std::string_view::npos) {
        return std::nullopt;
    }
    return TextRange{.begin = start, .end = last + end.size()};
}

void addToManagedBlock(std::string& text, const std::string_view title,
                       const std::string_view entry, const std::string_view anchor) {
    if (!findManagedBlock(text, title)) {
        const std::string block =
            std::format("{}\n{}\n\n", makeManagedBegin(title), makeManagedEnd(title));
        const usize at = findLineStart(text, anchor);
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
    text.insert(block->end - makeManagedEnd(title).size(), std::format("{}\n\n", entry));
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

std::vector<std::string> listLostSegmentLines(const std::string_view spec,
                                              const std::string_view name,
                                              const std::string_view block) {
    const std::optional<TextRange> found = findSpecSegment(spec, name);
    if (!found) {
        return {};
    }
    const std::vector<std::string_view> wanted = splitLines(block);
    const auto isWanted = [&wanted](const std::string_view line) {
        return std::ranges::any_of(
            wanted, [line](const std::string_view kept) { return trimSpaces(kept) == line; });
    };
    std::vector<std::string> lost;
    for (const std::string_view line :
         splitLines(spec.substr(found->begin, found->end - found->begin))) {
        const std::string_view trimmed = trimSpaces(line);
        if (!trimmed.empty() && !isWanted(trimmed)) {
            lost.emplace_back(trimmed);
        }
    }
    return lost;
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

std::optional<EditStatus> setSpecSegmentIncludes(std::string& spec, const std::string_view name,
                                                 const std::span<const std::string> objects) {
    const std::optional<TextRange> found = findSpecSegment(spec, name);
    if (!found) {
        return std::nullopt;
    }
    const std::string_view segment =
        std::string_view(spec).substr(found->begin, found->end - found->begin);
    std::string rewritten;
    std::string indent = "    ";
    bool isWritten = false;
    const auto writeIncludes = [&] {
        for (const std::string& object : objects) {
            rewritten += std::format("{}include \"$(BUILD_DIR)/{}\"\n", indent, object);
        }
        isWritten = true;
    };
    for (const std::string_view line : splitLines(segment)) {
        const std::string_view trimmed = trimSpaces(line);
        if (trimmed.starts_with("include ")) {
            if (!isWritten) {
                indent = std::string(line.substr(0, line.find_first_not_of(" \t")));
                writeIncludes();
            }
            continue;
        }
        if (!isWritten && (trimmed.starts_with("number ") || trimmed == "endseg")) {
            writeIncludes();
        }
        rewritten += std::format("{}\n", line);
    }
    rewritten.pop_back();
    if (rewritten == segment) {
        return EditStatus::Present;
    }
    spec.replace(found->begin, found->end - found->begin, rewritten);
    return EditStatus::Update;
}

EditItem putSpecSegmentItem(FileEdit& spec, EditPlan& plan, const std::string_view name,
                            const std::string_view block, const std::string_view title,
                            const std::string_view anchor, std::string label) {
    const std::vector<std::string> lost = listLostSegmentLines(spec.after, name, block);
    const EditStatus status = putSpecSegment(spec.after, name, block, title, anchor);
    if (status == EditStatus::Update && !lost.empty()) {
        std::string lines;
        for (const std::string& line : lost) {
            lines += std::format("{}{}", lines.empty() ? "" : "; ", line);
        }
        plan.warnings.push_back(std::format(
            "{}: rewriting segment \"{}\" drops what it has now and the tool does not write: {}",
            spec.file.generic_string(), name, lines));
    }
    return EditItem{.status = status, .label = std::move(label)};
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

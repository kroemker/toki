#include "HeaderSymbols.h"

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize MaximumCommands = 64;

[[nodiscard]] std::optional<usize> findCode(const z64::SymbolData& header, const u8 code) {
    for (usize at = 0;
         at + HeaderCommandSize <= header.bytes.size() && at < MaximumCommands * HeaderCommandSize;
         at += HeaderCommandSize) {
        if (header.bytes[at] == code) {
            return at;
        }
        if (header.bytes[at] == static_cast<u8>(CommandCode::End)) {
            break;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<usize> findEnd(const z64::SymbolData& header) {
    return findCode(header, static_cast<u8>(CommandCode::End));
}

/// The name of the alternate header list `front`, an image's main header,
/// points at.
[[nodiscard]] std::optional<std::string> findAlternates(const z64::SymbolData& front) {
    const std::optional<usize> command = findCommand(front, CommandCode::AlternateHeaderList);
    return command ? findPointee(front, *command + HeaderPointerSize) : std::nullopt;
}

} // namespace

std::optional<usize> findCommand(const z64::SymbolData& header, const CommandCode code) {
    return findCode(header, static_cast<u8>(code));
}

std::optional<std::string> findPointee(const z64::SymbolData& symbol, const usize offset) {
    const auto found = std::ranges::find(symbol.relocations, offset, &z64::Relocation::offset);
    if (found == symbol.relocations.end()) {
        return std::nullopt;
    }
    return found->symbol;
}

void insertBytes(z64::SymbolData& symbol, const usize at, const usize size) {
    symbol.bytes.insert(symbol.bytes.begin() + static_cast<std::ptrdiff_t>(at), size, 0);
    for (z64::Relocation& relocation : symbol.relocations) {
        if (relocation.offset >= at) {
            relocation.offset += size;
        }
    }
}

void eraseBytes(z64::SymbolData& symbol, const usize at, const usize size) {
    const auto start = symbol.bytes.begin() + static_cast<std::ptrdiff_t>(at);
    symbol.bytes.erase(start, start + static_cast<std::ptrdiff_t>(size));
    std::erase_if(symbol.relocations, [at, size](const z64::Relocation& relocation) {
        return relocation.offset >= at && relocation.offset < at + size;
    });
    for (z64::Relocation& relocation : symbol.relocations) {
        if (relocation.offset >= at + size) {
            relocation.offset -= size;
        }
    }
}

std::optional<usize> insertCommand(z64::SymbolData& header, const CommandCode code) {
    const std::optional<usize> end = findEnd(header);
    if (!end) {
        return std::nullopt;
    }
    insertBytes(header, *end, HeaderCommandSize);
    header.bytes[*end] = static_cast<u8>(code);
    return end;
}

usize countPointers(const SceneModel& model, const std::string_view name) {
    usize count = 0;
    for (const std::vector<z64::SymbolData>& symbols : model.getImages()) {
        for (const z64::SymbolData& symbol : symbols) {
            count += static_cast<usize>(
                std::ranges::count(symbol.relocations, name, &z64::Relocation::symbol));
        }
    }
    return count;
}

std::string makeFreeName(const SceneModel& model, const std::string_view base) {
    std::string name(base);
    for (usize suffix = 1; model.findSymbol(name) != nullptr; ++suffix) {
        name = std::format("{}_{}", base, suffix);
    }
    return name;
}

std::expected<std::string, base::Diagnostic> findHeaderName(const SceneModel& model,
                                                            const HeaderSite& site) {
    if (site.image >= model.getImages().size() || model.getImages()[site.image].empty()) {
        return std::unexpected(base::makeError(
            {}, site.image == 0 ? std::string("the scene has no header")
                                : std::format("the scene has no room {}", site.image - 1)));
    }
    const z64::SymbolData& front = model.getImages()[site.image].front();
    if (site.layer == 0) {
        return front.name;
    }
    const std::optional<std::string> alternates = findAlternates(front);
    const z64::SymbolData* headers = alternates ? model.findSymbol(*alternates) : nullptr;
    const std::optional<std::string> header =
        headers != nullptr ? findPointee(*headers, (site.layer - 1) * HeaderPointerSize)
                           : std::nullopt;
    if (header) {
        return *header;
    }
    if (site.image != 0) {
        return front.name;
    }
    return std::unexpected(
        base::makeError({}, std::format("the scene has no header for layer {}", site.layer)));
}

std::expected<std::string, base::Diagnostic> ownHeader(SceneModel& model, const HeaderSite& site) {
    std::expected<std::string, base::Diagnostic> name = findHeaderName(model, site);
    if (!name) {
        return name;
    }
    const z64::SymbolData& front = model.getImages()[site.image].front();
    const bool isMain = *name == front.name;
    if (countPointers(model, *name) + (isMain ? 1 : 0) <= 1) {
        return name;
    }
    usize copyLayer = site.layer;
    if (const std::optional<std::string> alternates = findAlternates(front); isMain && alternates) {
        const std::vector<z64::Relocation>& entries = model.findSymbol(*alternates)->relocations;
        if (const auto entry = std::ranges::find(entries, *name, &z64::Relocation::symbol);
            entry != entries.end()) {
            copyLayer = entry->offset / HeaderPointerSize + 1;
        }
    }
    z64::SymbolData copy = *model.findSymbol(*name);
    copy.name = makeFreeName(model, std::format("{}_header{:02}", front.name, copyLayer));
    copy.startsFile = false;
    if (const std::optional<usize> command = findCommand(copy, CommandCode::AlternateHeaderList)) {
        eraseBytes(copy, *command, HeaderCommandSize);
    }
    std::vector<z64::SymbolData> pointing;
    if (isMain) {
        for (const std::vector<z64::SymbolData>& symbols : model.getImages()) {
            for (const z64::SymbolData& symbol : symbols) {
                if (std::ranges::contains(symbol.relocations, *name, &z64::Relocation::symbol)) {
                    pointing.push_back(symbol);
                }
            }
        }
        for (z64::SymbolData& symbol : pointing) {
            for (z64::Relocation& relocation : symbol.relocations) {
                if (relocation.symbol == *name) {
                    relocation.symbol = copy.name;
                }
            }
        }
    } else {
        z64::SymbolData headers = *model.findSymbol(*findAlternates(front));
        std::ranges::find(headers.relocations, (site.layer - 1) * HeaderPointerSize,
                          &z64::Relocation::offset)
            ->symbol = copy.name;
        pointing.push_back(std::move(headers));
        name = copy.name;
    }
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(site.image, std::move(copy));
        !added) {
        return std::unexpected(added.error());
    }
    for (z64::SymbolData& symbol : pointing) {
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(symbol)); !set) {
            return std::unexpected(set.error());
        }
    }
    return name;
}

} // namespace toki::scene

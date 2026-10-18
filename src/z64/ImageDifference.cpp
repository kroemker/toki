#include "ImageDifference.h"

#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"
#include "z64/Types.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace z64 {

namespace {

std::string makeTypeText(const SymbolData& symbol) {
    return symbol.isArray ? symbol.type + "[]" : symbol.type;
}

std::string compareBytes(const SymbolData& expected, const SymbolData& actual) {
    if (expected.bytes.size() != actual.bytes.size()) {
        return std::format("is {} bytes, not {}", actual.bytes.size(), expected.bytes.size());
    }
    const auto [left, right] = std::ranges::mismatch(expected.bytes, actual.bytes);
    if (left == expected.bytes.end()) {
        return {};
    }
    return std::format("byte 0x{:X} is 0x{:02X}, not 0x{:02X}", left - expected.bytes.begin(),
                       *right, *left);
}

std::string compareRelocations(const SymbolData& expected, const SymbolData& actual) {
    const usize count = std::min(expected.relocations.size(), actual.relocations.size());
    for (usize i = 0; i < count; ++i) {
        const Relocation& left = expected.relocations[i];
        const Relocation& right = actual.relocations[i];
        if (left.offset != right.offset || left.symbol != right.symbol) {
            return std::format("points at '{}' from 0x{:X}, not at '{}' from 0x{:X}", right.symbol,
                               right.offset, left.symbol, left.offset);
        }
    }
    if (expected.relocations.size() > count) {
        const Relocation& missing = expected.relocations[count];
        return std::format("does not point at '{}' from 0x{:X}", missing.symbol, missing.offset);
    }
    if (actual.relocations.size() > count) {
        const Relocation& extra = actual.relocations[count];
        return std::format("points at '{}' from 0x{:X} as well", extra.symbol, extra.offset);
    }
    return {};
}

} // namespace

std::vector<ImageDifference> compareSymbols(const MemoryImage& expected, const MemoryImage& actual,
                                            const u8 segment) {
    std::map<std::string, SymbolData, std::less<>> left;
    for (SymbolData& symbol : listSymbolData(expected, segment)) {
        left.emplace(symbol.name, std::move(symbol));
    }
    std::map<std::string, SymbolData, std::less<>> right;
    for (SymbolData& symbol : listSymbolData(actual, segment)) {
        right.emplace(symbol.name, std::move(symbol));
    }
    std::vector<ImageDifference> differences;
    const auto report = [&differences](const std::string_view name, std::string message) {
        differences.push_back(
            ImageDifference{.symbol = std::string(name), .message = std::move(message)});
    };
    for (const auto& [name, symbol] : left) {
        const auto found = right.find(name);
        if (found == right.end()) {
            report(name, "is missing");
            continue;
        }
        const SymbolData& other = found->second;
        if (symbol.type != other.type || symbol.isArray != other.isArray) {
            report(name,
                   std::format("is a {}, not a {}", makeTypeText(other), makeTypeText(symbol)));
            continue;
        }
        if (std::string bytes = compareBytes(symbol, other); !bytes.empty()) {
            report(name, std::move(bytes));
        }
        if (std::string relocations = compareRelocations(symbol, other); !relocations.empty()) {
            report(name, std::move(relocations));
        }
    }
    for (const auto& [name, symbol] : right) {
        if (!left.contains(name)) {
            report(name, "is new");
        }
    }
    return differences;
}

} // namespace z64

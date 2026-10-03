#include "MacroTable.h"

#include "cparser/Token.h"
#include "cparser/Types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cparser {

std::optional<Macro> parseDefinition(std::span<const Token> line) {
    if (line.empty() || line.front().kind != TokenKind::Identifier) {
        return std::nullopt;
    }

    Macro macro;
    macro.name = std::string(line.front().text);
    usize cursor = 1;

    if (cursor < line.size() && isPunctuator(line[cursor], "(") && !line[cursor].spaceBefore) {
        macro.functionLike = true;
        ++cursor;
        bool afterName = false;
        while (cursor < line.size() && !isPunctuator(line[cursor], ")")) {
            if (line[cursor].kind == TokenKind::Identifier) {
                macro.parameters.emplace_back(line[cursor].text);
                afterName = true;
            } else if (isPunctuator(line[cursor], "...")) {
                if (!afterName) {
                    macro.parameters.emplace_back("__VA_ARGS__");
                }
                macro.variadic = true;
            } else {
                afterName = false;
            }
            ++cursor;
        }
        if (cursor < line.size()) {
            ++cursor;
        }
    }

    macro.body.assign(line.begin() + static_cast<std::ptrdiff_t>(cursor), line.end());
    return macro;
}

void MacroTable::define(Macro macro) {
    if (const auto hidden = undefined.find(macro.name); hidden != undefined.end()) {
        undefined.erase(hidden);
    }
    std::string name = macro.name;
    macros.insert_or_assign(std::move(name), std::move(macro));
}

void MacroTable::defineIfAbsent(Macro macro) {
    if (contains(macro.name)) {
        return;
    }
    define(std::move(macro));
}

void MacroTable::remove(std::string_view name) {
    if (const auto found = macros.find(name); found != macros.end()) {
        macros.erase(found);
    }
    if (base != nullptr && base->contains(name)) {
        undefined.emplace(name);
    }
}

std::vector<std::string_view> MacroTable::definedNames() const {
    std::vector<std::string_view> names;
    names.reserve(macros.size());
    for (const auto& [name, macro] : macros) {
        names.emplace_back(name);
    }
    return names;
}

const Macro* MacroTable::find(std::string_view name) const {
    if (const auto found = macros.find(name); found != macros.end()) {
        return &found->second;
    }
    if (base == nullptr || undefined.contains(name)) {
        return nullptr;
    }
    return base->find(name);
}

} // namespace cparser

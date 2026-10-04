#pragma once

#include "cparser/Expression.h"
#include "cparser/Token.h"
#include "z64/DisplayList.h"
#include "z64/Types.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cparser {
class MacroTable;
}

namespace z64 {

struct DisplayListContext {
    /// The gbi's macros, as the build's own `gbi.h` defines them under its
    /// configuration, with the file's own macros layered over them, since a
    /// command's arguments name sizes the file's header defines
    /// (`cparser::MacroTable::defineFrom`). Each element expands against these.
    const cparser::MacroTable* macros = nullptr;

    /// The addresses of the symbols the list points at: vertices, textures,
    /// palettes, matrices and other lists.
    std::function<std::optional<cparser::SymbolAddress>(std::string_view)> findSymbol;

    /// Declared element counts, for `ARRAY_COUNT` in a command's arguments.
    std::function<std::optional<usize>(std::string_view)> findArrayLength;
};

/// Turns the interiors of `Gfx` arrays' braces into the words the build would
/// compile them to, by expanding every element with the gbi's own macros and
/// evaluating each `{ w0, w1 }` pair. Braces may be left out, as C allows and
/// `gsSPTextureRectangle` does: loose words pair up into commands. An element
/// that is no gbi macro, or does not come out as pairs of words, is an error
/// naming it and makes no commands, so the commands after it move up.
///
/// The real gbi's macros are verbose, a few hundred tokens per command, and
/// display lists repeat themselves, so an encoder remembers the words of
/// every element it encoded cleanly. That holds because its context never
/// changes: one encoder serves one file under one layout of its symbols.
class DisplayListEncoder {
public:
    explicit DisplayListEncoder(DisplayListContext context);

    [[nodiscard]] DisplayList encode(std::span<const cparser::Token> initializer);

private:
    void encodeElement(std::span<const cparser::Token> element, DisplayList& list);

    [[nodiscard]] std::optional<u32> evaluateWord(std::span<const cparser::Token> word,
                                                  DisplayList& list) const;

    [[nodiscard]] std::optional<std::vector<u32>>
    evaluateWords(std::span<const cparser::Token> expanded, const std::string& name,
                  const base::SourceLocation& location, DisplayList& list) const;

    DisplayListContext context;
    cparser::EvalContext evalContext;
    std::unordered_map<std::string, std::vector<u32>> remembered;
};

} // namespace z64

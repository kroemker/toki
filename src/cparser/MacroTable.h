#pragma once

#include "cparser/Token.h"
#include "cparser/Types.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cparser {

struct Macro {
    std::string name;
    std::vector<std::string> parameters;
    std::vector<Token> body;
    bool functionLike = false;

    /// The last parameter takes every argument past the named ones, commas
    /// included. It is `__VA_ARGS__` for `...`, or the name GNU's `args...`
    /// spelling gives it.
    bool variadic = false;
};

/// Reads the tokens after `#define` into a macro. A function-like macro is one
/// whose `(` touches the name, which is why the scanner records whether a token
/// was preceded by space.
[[nodiscard]] std::optional<Macro> parseDefinition(std::span<const Token> line);

/// The `#define`s in scope while preprocessing, as a stack of layers. A table
/// made over a `base` answers from its own definitions first, then from the
/// base, and never writes to the base: an `#undef` of a base name hides it in
/// this layer only. That is how the macros scraped from a checkout's headers
/// are shared by every file read against them without any one file's defines,
/// undefs or include guards leaking into the next.
///
/// Bodies hold tokens that borrow from the `SourceSet` their definitions were
/// read from, so that set must outlive the table. The base is borrowed as
/// well: it must outlive, and must not move out from under, every layer made
/// over it.
class MacroTable {
public:
    MacroTable() = default;

    explicit MacroTable(const MacroTable* base) : base(base) {}

    void define(Macro macro);

    /// Keeps an existing definition of the same name, in this layer or below.
    /// Used while scraping a header tree, where both arms of an unevaluated
    /// `#if` are seen and the first one read is the one taken.
    void defineIfAbsent(Macro macro);

    void remove(std::string_view name);

    [[nodiscard]] const Macro* find(std::string_view name) const;

    [[nodiscard]] bool contains(std::string_view name) const {
        return find(name) != nullptr;
    }

    /// Definitions made in this layer, not counting the base.
    [[nodiscard]] usize count() const {
        return macros.size();
    }

    /// The names this layer defines itself, in no particular order.
    [[nodiscard]] std::vector<std::string_view> getDefinedNames() const;

private:
    struct Hash {
        using is_transparent = void;

        [[nodiscard]] usize operator()(std::string_view name) const {
            return std::hash<std::string_view>{}(name);
        }
    };

    const MacroTable* base = nullptr;
    std::unordered_map<std::string, Macro, Hash, std::equal_to<>> macros;
    std::unordered_set<std::string, Hash, std::equal_to<>> undefined;
};

} // namespace cparser

#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorModel.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace toki::actor {

/// An overlay's main file, read for what the model rules ask of it: its
/// functions, its file-scope data, the object symbols its asset headers
/// declare, and the literals in its calls. The unit must outlive it.
class OverlayCode {
public:
    OverlayCode(const cparser::TranslationUnit& unit, std::filesystem::path main);

    /// A file-scope definition of `name`.
    [[nodiscard]] const cparser::Declaration* findDefinition(std::string_view name) const;

    /// A file-scope definition whose type is one of `types`, as the profile is
    /// an `ActorProfile` (or an `ActorInit` in older checkouts).
    [[nodiscard]] const cparser::Declaration*
    findDefinitionOfType(std::span<const std::string_view> types) const;

    /// Every function the main file defines, in source order.
    [[nodiscard]] std::vector<const cparser::FunctionDefinition*> listFunctions() const;

    /// `name` and every function of the main file it calls directly, in the
    /// order first called: setup and draw helpers, one level deep.
    [[nodiscard]] std::vector<const cparser::FunctionDefinition*>
    listReachable(std::string_view name) const;

    /// The object whose header declares `name`, with the type it declares it
    /// with, located where the overlay names it.
    [[nodiscard]] std::optional<ObjectSymbol> findObjectSymbol(const cparser::Token& name) const;

    /// Whether `name` is declared at file scope, rather than a local.
    [[nodiscard]] bool isDeclared(std::string_view name) const;

    /// Whether `name` is declared in a folder named as the overlay's is, its
    /// own or its extracted assets': data the overlay carries itself rather
    /// than takes from an object.
    [[nodiscard]] bool isOverlayData(std::string_view name) const;

    /// A constant expression's value, if every name in it is known.
    [[nodiscard]] std::optional<f64> evaluateNumber(std::span<const cparser::Token> tokens) const;

private:
    [[nodiscard]] const cparser::FunctionDefinition* findFunction(std::string_view name) const;

    const cparser::TranslationUnit& unit;
    std::filesystem::path main;
    cparser::ParseResult parsed;
};

/// The symbol an argument names, if it names exactly one: `&gSkel`,
/// `(SkeletonHeader*)&gSkel`, `gDL` or `SEGMENTED_TO_VIRTUAL(gDL)`. `NULL`
/// names none.
[[nodiscard]] const cparser::Token* findLiteralSymbol(std::span<const cparser::Token> argument);

} // namespace toki::actor

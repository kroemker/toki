#include "OverlayCode.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorModel.h"
#include "base/Diagnostic.h"
#include "cparser/Calls.h"
#include "cparser/Expression.h"
#include "cparser/Initializer.h"
#include "cparser/MacroTable.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"
#include "cparser/Value.h"
#include "z64/DecompDialect.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] std::optional<std::string> findObjectFolder(const std::filesystem::path& header) {
    const std::filesystem::path folder = header.parent_path();
    if (folder.parent_path().filename() != "objects") {
        return std::nullopt;
    }
    return folder.filename().string();
}

[[nodiscard]] usize findClose(const std::span<const cparser::Token> tokens) {
    int nesting = 0;
    for (usize at = 0; at < tokens.size(); ++at) {
        if (cparser::isPunctuator(tokens[at], "(")) {
            ++nesting;
        } else if (cparser::isPunctuator(tokens[at], ")") && --nesting == 0) {
            return at;
        }
    }
    return tokens.size();
}

[[nodiscard]] bool isCast(const std::span<const cparser::Token> inside) {
    return !inside.empty() && std::ranges::all_of(inside, [](const cparser::Token& token) {
        return token.kind == cparser::TokenKind::Identifier || cparser::isPunctuator(token, "*");
    });
}

[[nodiscard]] std::optional<std::span<const cparser::Token>>
findLocalArray(const std::span<const cparser::Token> body, const usize name) {
    if (name + 1 >= body.size() || !cparser::isPunctuator(body[name + 1], "[")) {
        return std::nullopt;
    }
    usize open = name + 1;
    while (open < body.size() && !cparser::isPunctuator(body[open], "=") &&
           !cparser::isPunctuator(body[open], ";")) {
        ++open;
    }
    if (open + 1 >= body.size() || !cparser::isPunctuator(body[open], "=") ||
        !cparser::isPunctuator(body[open + 1], "{")) {
        return std::nullopt;
    }
    const usize begin = open + 2;
    usize close = begin;
    int nesting = 1;
    for (; close < body.size(); ++close) {
        if (cparser::isPunctuator(body[close], "{")) {
            ++nesting;
        } else if (cparser::isPunctuator(body[close], "}") && --nesting == 0) {
            break;
        }
    }
    return body.subspan(begin, close - begin);
}

constexpr f64 MaskBits = 16.0;

} // namespace

std::span<const cparser::Token> stripParentheses(std::span<const cparser::Token> tokens) {
    while (tokens.size() > 2 && cparser::isPunctuator(tokens.front(), "(") &&
           findClose(tokens) + 1 == tokens.size()) {
        tokens = tokens.subspan(1, tokens.size() - 2);
    }
    return tokens;
}

bool isWholeCall(const std::span<const cparser::Token> tokens, const std::string_view callee) {
    return tokens.size() > 3 && cparser::isIdentifier(tokens[0], callee) &&
           cparser::isPunctuator(tokens[1], "(") &&
           findClose(tokens.subspan(1)) + 2 == tokens.size();
}

OverlayCode::OverlayCode(const cparser::TranslationUnit& unit, std::filesystem::path main)
    : unit(unit), main(std::move(main)),
      parsed(cparser::parseDeclarations(
          unit,
          cparser::EvalContext{.macros = &unit.macros, .dialect = &z64::getDecompDialect()})) {
    const std::filesystem::path preferred = std::filesystem::path(this->main).make_preferred();
    for (const cparser::FunctionDefinition& function : parsed.functions) {
        if (std::filesystem::path(function.location.file).make_preferred() == preferred) {
            mainFunctions.push_back(&function);
        }
    }
}

const cparser::Declaration* OverlayCode::findDefinition(const std::string_view name) const {
    const auto found =
        std::ranges::find_if(parsed.declarations, [&](const cparser::Declaration& declaration) {
            return declaration.name == name && declaration.hasInitializer;
        });
    return found == parsed.declarations.end() ? nullptr : &*found;
}

const cparser::Declaration*
OverlayCode::findDefinitionOfType(const std::span<const std::string_view> types) const {
    const auto found =
        std::ranges::find_if(parsed.declarations, [&](const cparser::Declaration& declaration) {
            return declaration.hasInitializer && std::ranges::contains(types, declaration.type);
        });
    return found == parsed.declarations.end() ? nullptr : &*found;
}

const cparser::FunctionDefinition* OverlayCode::findFunction(const std::string_view name) const {
    const auto found = std::ranges::find(mainFunctions, name, &cparser::FunctionDefinition::name);
    return found == mainFunctions.end() ? nullptr : *found;
}

std::vector<const cparser::FunctionDefinition*> OverlayCode::listFunctions() const {
    return mainFunctions;
}

std::vector<const cparser::FunctionDefinition*>
OverlayCode::listReachable(const std::string_view name) const {
    std::vector<const cparser::FunctionDefinition*> reachable;
    const cparser::FunctionDefinition* root = findFunction(name);
    if (root == nullptr) {
        return reachable;
    }
    reachable.push_back(root);
    const std::vector<cparser::Call> calls =
        cparser::findCalls(root->body, [this](const std::string_view callee) {
            return findFunction(callee) != nullptr;
        });
    for (const cparser::Call& call : calls) {
        const cparser::FunctionDefinition* callee = findFunction(call.callee);
        if (!std::ranges::contains(reachable, callee)) {
            reachable.push_back(callee);
        }
    }
    return reachable;
}

std::optional<ObjectSymbol> OverlayCode::findObjectSymbol(const cparser::Token& name) const {
    for (const cparser::Declaration& declaration : parsed.declarations) {
        if (declaration.name != name.text || !declaration.isExtern) {
            continue;
        }
        if (std::optional<std::string> object = findObjectFolder(declaration.location.file)) {
            return ObjectSymbol{.name = std::string(name.text),
                                .object = std::move(*object),
                                .type = std::string(declaration.type),
                                .location = cparser::locate(name)};
        }
    }
    return std::nullopt;
}

bool OverlayCode::isDeclared(const std::string_view name) const {
    return std::ranges::contains(parsed.declarations, name, &cparser::Declaration::name);
}

bool OverlayCode::isOverlayData(const std::string_view name) const {
    return std::ranges::any_of(parsed.declarations, [&](const cparser::Declaration& declaration) {
        return declaration.name == name &&
               declaration.location.file.parent_path().filename() == main.parent_path().filename();
    });
}

std::vector<std::span<const cparser::Token>>
OverlayCode::findArrayElements(const std::string_view name,
                               const cparser::FunctionDefinition& within) const {
    if (const cparser::Declaration* definition = findDefinition(name)) {
        return cparser::splitElements(definition->initializer);
    }
    const std::span<const cparser::Token> body = within.body;
    for (usize at = 0; at + 1 < body.size(); ++at) {
        if (!cparser::isIdentifier(body[at], name)) {
            continue;
        }
        if (std::optional<std::span<const cparser::Token>> list = findLocalArray(body, at)) {
            return cparser::splitElements(*list);
        }
    }
    return {};
}

std::vector<ArrayDefinition> OverlayCode::listArrays() const {
    std::vector<ArrayDefinition> arrays;
    const std::filesystem::path folder = main.parent_path().filename();
    const std::filesystem::path* lastFile = nullptr;
    bool isLastOwn = false;
    for (const cparser::Declaration& declaration : parsed.declarations) {
        if (!declaration.isArray || !declaration.hasInitializer || !declaration.isBraced) {
            continue;
        }
        if (lastFile == nullptr || *lastFile != declaration.location.file) {
            lastFile = &declaration.location.file;
            isLastOwn = lastFile->parent_path().filename() == folder;
        }
        if (isLastOwn) {
            arrays.push_back(
                ArrayDefinition{.name = declaration.name,
                                .elements = cparser::splitElements(declaration.initializer),
                                .location = declaration.location});
        }
    }
    for (const cparser::FunctionDefinition* function : mainFunctions) {
        const std::span<const cparser::Token> body = function->body;
        for (usize at = 1; at + 1 < body.size(); ++at) {
            if (body[at].kind != cparser::TokenKind::Identifier ||
                body[at - 1].kind != cparser::TokenKind::Identifier) {
                continue;
            }
            if (std::optional<std::span<const cparser::Token>> list = findLocalArray(body, at)) {
                arrays.push_back(ArrayDefinition{.name = body[at].text,
                                                 .elements = cparser::splitElements(*list),
                                                 .location = cparser::locate(body[at])});
            }
        }
    }
    return arrays;
}

std::optional<ParamsMacro> OverlayCode::findParamsMacro(const std::string_view name) const {
    if (const auto known = paramsMacros.find(name); known != paramsMacros.end()) {
        return known->second;
    }
    std::optional<ParamsMacro> found = readParamsMacro(name);
    paramsMacros.emplace(std::string(name), found);
    return found;
}

std::optional<ParamsMacro> OverlayCode::readParamsMacro(const std::string_view name) const {
    const cparser::Macro* macro = unit.macros.find(name);
    if (macro == nullptr || !macro->functionLike || macro->parameters.empty()) {
        return std::nullopt;
    }
    const std::span<const cparser::Token> body = stripParentheses(macro->body);
    const std::vector<cparser::Call> calls =
        cparser::findCalls(body, [](const std::string_view callee) {
            return callee == "PARAMS_GET_U" || callee == "PARAMS_GET_S";
        });
    if (calls.empty() || !isWholeCall(body, calls.front().callee) ||
        calls.front().arguments.size() != 3) {
        return std::nullopt;
    }
    const cparser::Call& read = calls.front();
    const std::span<const cparser::Token> source = stripParentheses(read.arguments[0]);
    const auto isParameter = [&](const cparser::Token& token) {
        return token.kind == cparser::TokenKind::Identifier &&
               std::ranges::contains(macro->parameters, token.text);
    };
    const bool takesParams = source.size() == 1 && isParameter(source.front());
    const bool readsActor = !source.empty() && cparser::isIdentifier(source.back(), "params") &&
                            std::ranges::any_of(source, isParameter);
    const std::optional<f64> shift = evaluateNumber(read.arguments[1]);
    const std::optional<f64> width = evaluateNumber(read.arguments[2]);
    if ((!takesParams && !readsActor) || !shift || !width || *width < 1.0 || *shift < 0.0 ||
        *shift + *width > MaskBits) {
        return std::nullopt;
    }
    return ParamsMacro{.shift = static_cast<u8>(*shift),
                       .width = static_cast<u8>(*width),
                       .isSigned = read.callee == "PARAMS_GET_S",
                       .takesParams = takesParams};
}

std::optional<f64> OverlayCode::evaluateNumber(const std::span<const cparser::Token> tokens) const {
    std::vector<base::Diagnostic> diagnostics;
    const std::expected<cparser::Value, base::Diagnostic> value = cparser::evaluate(
        tokens,
        cparser::EvalContext{
            .macros = &unit.macros, .dialect = &z64::getDecompDialect(), .unknownIsZero = false},
        diagnostics);
    const bool isKnown = std::ranges::none_of(diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.severity != base::Severity::Note;
    });
    if (!value || !isKnown) {
        return std::nullopt;
    }
    return cparser::getReal(*value);
}

const cparser::Token* findLiteralSymbol(std::span<const cparser::Token> argument) {
    while (!argument.empty()) {
        if (cparser::isPunctuator(argument.front(), "(")) {
            const usize close = findClose(argument);
            if (close + 1 >= argument.size() || !isCast(argument.subspan(1, close - 1))) {
                return nullptr;
            }
            argument = argument.subspan(close + 1);
        } else if (cparser::isPunctuator(argument.front(), "&")) {
            argument = argument.subspan(1);
        } else if (isWholeCall(argument, "SEGMENTED_TO_VIRTUAL")) {
            argument = argument.subspan(2, argument.size() - 3);
        } else {
            break;
        }
    }
    if (argument.size() != 1 || argument.front().kind != cparser::TokenKind::Identifier ||
        cparser::isIdentifier(argument.front(), "NULL")) {
        return nullptr;
    }
    return &argument.front();
}

} // namespace toki::actor

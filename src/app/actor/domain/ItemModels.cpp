#include "ItemModels.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/OverlayCode.h"
#include "app/actor/domain/Pass.h"
#include "base/Diagnostic.h"
#include "cparser/Calls.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"
#include "cparser/TypeTable.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::string_view DrawTable = "sDrawItemTable";
constexpr std::string_view GetItemTable = "sGetItemTable";
constexpr std::string_view SetupPrefix = "Gfx_SetupDL_";
constexpr std::string_view SetupConstant = "SETUPDL_";
constexpr u8 ScrolledSegment = 0x08;
constexpr std::array<std::string_view, 12> KnownCalls{
    "Gfx_SetupDL",      "MATRIX_FINALIZE_AND_LOAD",
    "gSPDisplayList",   "gSPSegment",
    "Gfx_TwoTexScroll", "Matrix_Push",
    "Matrix_Pop",       "Matrix_Translate",
    "Matrix_Scale",     "Matrix_ReplaceRotation",
    "OPEN_DISPS",       "CLOSE_DISPS"};

[[nodiscard]] std::optional<u8> readTrailingNumber(const std::string_view text,
                                                   const std::string_view prefix) {
    if (!text.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view digits = text.substr(prefix.size());
    unsigned value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc() || value > 0xFF) {
        return std::nullopt;
    }
    return static_cast<u8>(value);
}

[[nodiscard]] Pass findPass(const std::span<const cparser::Token> argument) {
    return std::ranges::any_of(
               argument,
               [](const cparser::Token& token) { return token.text.starts_with("POLY_XLU_DISP"); })
               ? Pass::Translucent
               : Pass::Opaque;
}

[[nodiscard]] std::optional<usize> findListIndex(const std::span<const cparser::Token> argument) {
    for (usize i = 0; i + 2 < argument.size(); ++i) {
        if (cparser::isIdentifier(argument[i], "dlists") &&
            cparser::isPunctuator(argument[i + 1], "[")) {
            unsigned index = 0;
            const std::string_view text = argument[i + 2].text;
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), index);
            if (error == std::errc()) {
                return index;
            }
        }
    }
    return std::nullopt;
}

/// The setup display list and pass of a call such as `Gfx_SetupDL_25Xlu`.
[[nodiscard]] std::optional<std::pair<u8, Pass>> findNamedSetup(const std::string_view callee) {
    const bool isOpaque = callee.ends_with("Opa");
    if (!isOpaque && !callee.ends_with("Xlu")) {
        return std::nullopt;
    }
    const std::optional<u8> setup =
        readTrailingNumber(callee.substr(0, callee.size() - 3), SetupPrefix);
    if (!setup) {
        return std::nullopt;
    }
    return std::pair(*setup, isOpaque ? Pass::Opaque : Pass::Translucent);
}

struct MatrixState {
    PartPlacement placement;
    bool isBillboard = false;
};

class RecipeReader {
public:
    RecipeReader(const OverlayCode& code, std::vector<base::Diagnostic>& diagnostics)
        : code(code), diagnostics(diagnostics) {}

    [[nodiscard]] std::vector<ItemDraw> read(const cparser::FunctionDefinition& function);

private:
    void readCall(const cparser::Call& call);

    void readScroll(const cparser::Call& segment);

    [[nodiscard]] f32 readNumber(const std::span<const cparser::Token> tokens, const f32 fallback,
                                 const cparser::Call& call) {
        if (const std::optional<f64> value = code.evaluateNumber(tokens)) {
            return static_cast<f32>(*value);
        }
        report(call, "an argument that is not a literal");
        return fallback;
    }

    void report(const cparser::Call& call, const std::string_view what) {
        diagnostics.push_back(base::makeWarning(
            call.location,
            std::format("{} calls {} with {}, which an item's model is drawn without", functionName,
                        call.callee, what)));
    }

    const OverlayCode& code;
    std::vector<base::Diagnostic>& diagnostics;
    std::string_view functionName;
    MatrixState current;
    std::vector<MatrixState> stack;
    std::array<MatrixState, 2> loaded{};
    std::array<u8, 2> setups{};
    std::array<std::vector<TileSize>, 2> scrolls{};
    std::vector<ItemDraw> draws;
};

std::vector<ItemDraw> RecipeReader::read(const cparser::FunctionDefinition& function) {
    functionName = function.name;
    const std::vector<cparser::Call> calls =
        cparser::findCalls(function.body, [](const std::string_view callee) {
            return callee.starts_with("Gfx_") || callee.starts_with("Matrix_") ||
                   callee.starts_with("gSP") || callee.starts_with("gDP") ||
                   callee == "MATRIX_FINALIZE_AND_LOAD" || callee.starts_with("GetItem_");
        });
    for (const cparser::Call& call : calls) {
        readCall(call);
    }
    return std::move(draws);
}

void RecipeReader::readCall(const cparser::Call& call) {
    const std::string_view callee = call.callee;
    const auto passIndex = [&call] {
        return call.arguments.empty() ? usize{0} : static_cast<usize>(findPass(call.arguments[0]));
    };
    if (const std::optional<std::pair<u8, Pass>> named = findNamedSetup(callee)) {
        setups[static_cast<usize>(named->second)] = named->first;
    } else if (callee == "Gfx_SetupDL" && call.arguments.size() == 2) {
        const std::span<const cparser::Token> constant = call.arguments[1];
        const std::optional<u8> setup =
            constant.size() == 1 ? readTrailingNumber(constant.front().text, SetupConstant)
                                 : std::nullopt;
        if (setup) {
            setups[passIndex()] = *setup;
        } else {
            report(call, "a setup display list that is not a SETUPDL_ constant");
        }
    } else if (callee == "MATRIX_FINALIZE_AND_LOAD") {
        loaded[passIndex()] = current;
    } else if (callee == "gSPDisplayList" && call.arguments.size() == 2) {
        const usize pass = passIndex();
        if (const std::optional<usize> list = findListIndex(call.arguments[1])) {
            draws.push_back(ItemDraw{.list = *list,
                                     .pass = static_cast<Pass>(pass),
                                     .setup = setups[pass],
                                     .placement = loaded[pass].placement,
                                     .isBillboard = loaded[pass].isBillboard,
                                     .tiles = scrolls[pass]});
        } else {
            report(call, "a display list that is not one of the table's");
        }
    } else if (callee == "gSPSegment") {
        readScroll(call);
    } else if (callee == "Matrix_Push") {
        stack.push_back(current);
    } else if (callee == "Matrix_Pop" && !stack.empty()) {
        current = stack.back();
        stack.pop_back();
    } else if (callee == "Matrix_Translate" && call.arguments.size() >= 3) {
        for (usize axis = 0; axis < 3; ++axis) {
            current.placement.translation[axis] +=
                readNumber(call.arguments[axis], 0.0F, call) * current.placement.scale[axis];
        }
    } else if (callee == "Matrix_Scale" && call.arguments.size() >= 3) {
        for (usize axis = 0; axis < 3; ++axis) {
            current.placement.scale[axis] *= readNumber(call.arguments[axis], 1.0F, call);
        }
    } else if (callee == "Matrix_ReplaceRotation") {
        current.isBillboard = true;
    } else if (!std::ranges::contains(KnownCalls, callee) && !callee.starts_with("GetItem_")) {
        report(call, "nothing Toki64 knows to draw");
    }
}

void RecipeReader::readScroll(const cparser::Call& segment) {
    constexpr usize ScrollArguments = 11;
    if (segment.arguments.size() != 3) {
        return;
    }
    const std::vector<cparser::Call> scroll =
        cparser::findCalls(segment.arguments[2], [](const std::string_view callee) {
            return callee == "Gfx_TwoTexScroll";
        });
    const std::optional<f64> number = code.evaluateNumber(segment.arguments[1]);
    if (scroll.size() != 1 || scroll.front().arguments.size() != ScrollArguments || !number ||
        *number != ScrolledSegment) {
        report(segment, "a segment that is not segment 8 pointed at a Gfx_TwoTexScroll");
        return;
    }
    const std::vector<std::span<const cparser::Token>>& arguments = scroll.front().arguments;
    std::vector<TileSize> tiles;
    for (const usize first : {usize{1}, usize{6}}) {
        tiles.push_back(TileSize{
            .tile = static_cast<u8>(readNumber(arguments[first], 0.0F, scroll.front())),
            .width = static_cast<u16>(readNumber(arguments[first + 3], 1.0F, scroll.front())),
            .height = static_cast<u16>(readNumber(arguments[first + 4], 1.0F, scroll.front()))});
    }
    scrolls[static_cast<usize>(findPass(segment.arguments[0]))] = std::move(tiles);
}

[[nodiscard]] std::vector<std::string_view>
listIdentifiers(const std::span<const cparser::Token> element) {
    std::vector<std::string_view> names;
    for (const cparser::Token& token : element) {
        if (token.kind == cparser::TokenKind::Identifier && token.text != "NULL") {
            names.push_back(token.text);
        }
    }
    return names;
}

void readDrawTable(const OverlayCode& code, ItemModels& models) {
    const std::vector<ArrayDefinition> arrays = code.listArrays();
    const auto table = std::ranges::find(arrays, DrawTable, &ArrayDefinition::name);
    if (table == arrays.end()) {
        models.diagnostics.push_back(base::makeWarning(
            {}, std::format("src/code/z_draw.c defines no {}, so no item is drawn", DrawTable)));
        return;
    }
    const std::vector<const cparser::FunctionDefinition*> functions = code.listFunctions();
    for (usize index = 0; index < table->elements.size() && index < models.drawIds.size();
         ++index) {
        const std::vector<std::string_view> names = listIdentifiers(table->elements[index]);
        if (names.empty()) {
            continue;
        }
        ItemModel model{.drawFunction = std::string(names.front())};
        for (usize i = 1; i < names.size(); ++i) {
            model.lists.emplace_back(names[i]);
        }
        const auto function =
            std::ranges::find(functions, names.front(), &cparser::FunctionDefinition::name);
        if (function != functions.end()) {
            model.draws = RecipeReader(code, models.diagnostics).read(**function);
        }
        models.models.insert_or_assign(models.drawIds[index], std::move(model));
    }
}

void readObjects(const hackeroot::Workspace& workspace, ItemModels& models) {
    const hackeroot::OverlayFiles player{.directory = workspace.getRoot() / "src" / "overlays" /
                                                      "actors" / "ovl_player_actor",
                                         .main = workspace.getRoot() / "src" / "overlays" /
                                                 "actors" / "ovl_player_actor" / "z_player.c"};
    std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        hackeroot::preprocessOverlay(workspace, player);
    if (!unit) {
        models.diagnostics.push_back(unit.error());
        return;
    }
    models.files.push_back(player.main);
    const OverlayCode code(*unit, player.main);
    const std::vector<ArrayDefinition> arrays = code.listArrays();
    const auto table = std::ranges::find(arrays, GetItemTable, &ArrayDefinition::name);
    if (table == arrays.end()) {
        return;
    }
    const std::vector<hackeroot::ObjectEntry>& objects = workspace.getTables().objects;
    for (const std::span<const cparser::Token> element : table->elements) {
        const std::vector<std::string_view> names = listIdentifiers(element);
        const auto object = std::ranges::find_if(
            names, [](const std::string_view name) { return name.starts_with("OBJECT_"); });
        const auto drawId = std::ranges::find_if(
            names, [](const std::string_view name) { return name.starts_with("GID_"); });
        if (object == names.end() || drawId == names.end()) {
            continue;
        }
        const auto model = models.models.find(*drawId);
        const auto entry = std::ranges::find(objects, *object, &hackeroot::ObjectEntry::enumName);
        if (model != models.models.end() && !model->second.object && entry != objects.end()) {
            model->second.object = entry->name;
        }
    }
}

[[nodiscard]] std::vector<std::string> listDrawIds(const hackeroot::Workspace& workspace) {
    const std::array<std::string, 1> headers{"item.h"};
    const hackeroot::Prelude prelude = hackeroot::readPrelude(workspace, headers);
    const cparser::Enumeration* enumeration = prelude.types.findEnumeration("GetItemDrawID");
    std::vector<std::string> names;
    if (enumeration == nullptr) {
        return names;
    }
    for (const cparser::Enumerator& enumerator : enumeration->constants) {
        if (enumerator.value < 0) {
            continue;
        }
        const auto index = static_cast<usize>(enumerator.value);
        if (names.size() <= index) {
            names.resize(index + 1);
        }
        names[index] = enumerator.name;
    }
    return names;
}

} // namespace

ItemModels readItemModels(const hackeroot::Workspace& workspace) {
    ItemModels models{.drawIds = listDrawIds(workspace)};
    if (models.drawIds.empty()) {
        models.diagnostics.push_back(base::makeWarning(
            {.file = workspace.getRoot() / "include" / "item.h"},
            "include/item.h defines no GetItemDrawID enumeration, so no item is drawn"));
        return models;
    }
    const hackeroot::OverlayFiles draw{.directory = workspace.getRoot() / "src" / "code",
                                       .main = workspace.getRoot() / "src" / "code" / "z_draw.c"};
    std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        hackeroot::preprocessOverlay(workspace, draw);
    if (!unit) {
        models.diagnostics.push_back(unit.error());
        return models;
    }
    models.files.push_back(draw.main);
    readDrawTable(OverlayCode(*unit, draw.main), models);
    readObjects(workspace, models);
    return models;
}

const ItemModel* findItemModel(const ItemModels& models, const std::string_view name) {
    const auto found = models.models.find(name);
    return found == models.models.end() ? nullptr : &found->second;
}

std::optional<std::string> findDrawIdName(const ItemModels& models, const s64 index) {
    if (index < 0 || static_cast<usize>(index) >= models.drawIds.size() ||
        models.drawIds[static_cast<usize>(index)].empty()) {
        return std::nullopt;
    }
    return models.drawIds[static_cast<usize>(index)];
}

} // namespace toki::actor

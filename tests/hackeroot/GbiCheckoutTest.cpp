#include "base/Diagnostic.h"
#include "base/Types.h"
#include "cparser/Dialect.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/ScalarType.h"
#include "hackeroot/AssetSource.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Workspace.h"
#include "n64/CommandDecoder.h"
#include "z64/DecompDialect.h"
#include "z64/DisplayList.h"
#include "z64/DisplayListEncoder.h"
#include "z64/GbiNames.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using base::u32;
using base::u8;
using base::usize;

namespace {

constexpr std::array CodeSymbols = {std::string_view("gIdentityMtx")};

struct Checkout {
    Checkout() {
        const std::filesystem::path repository(TOKI_HACKEROOT);
        if (repository.empty()) {
            SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
        }
        std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
            hackeroot::Workspace::load(repository, {});
        REQUIRE(loaded.has_value());
        workspace.emplace(std::move(*loaded));
        std::expected<hackeroot::Prelude, base::Diagnostic> read = hackeroot::readGbi(*workspace);
        INFO((read ? std::string() : read.error().message));
        REQUIRE(read.has_value());
        gbi.emplace(std::move(*read));
        for (const base::Diagnostic& diagnostic : gbi->diagnostics) {
            INFO(diagnostic.message);
            REQUIRE(diagnostic.severity == base::Severity::Note);
        }
    }

    std::optional<hackeroot::Workspace> workspace;
    std::optional<hackeroot::Prelude> gbi;
};

std::vector<u32> readWords(const z64::DisplayList& list) {
    std::vector<u32> words;
    for (usize i = 0; i + 3 < list.bytes.size(); i += 4) {
        words.push_back((u32{list.bytes[i]} << 24) | (u32{list.bytes[i + 1]} << 16) |
                        (u32{list.bytes[i + 2]} << 8) | u32{list.bytes[i + 3]});
    }
    return words;
}

std::vector<std::filesystem::path> listSources(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> sources;
    std::error_code failure;
    for (std::filesystem::directory_iterator entry(directory, failure);
         !failure && entry != std::filesystem::directory_iterator(); entry.increment(failure)) {
        const std::string name = entry->path().filename().string();
        if (name.ends_with(".c") && !name.ends_with(".inc.c")) {
            sources.push_back(entry->path());
        }
    }
    return sources;
}

usize findElementSize(const std::string_view type) {
    const cparser::Dialect& dialect = z64::getDecompDialect();
    if (const cparser::ScalarType* scalar = cparser::findType(dialect, type)) {
        return scalar->size;
    }
    if (const cparser::ScalarType* aggregate = cparser::findAggregate(dialect, type)) {
        return aggregate->size;
    }
    return 1;
}

} // namespace

TEST_CASE("the checkout's gbi.h encodes F3DEX2's known words", "[hackeroot][gbi][checkout]") {
    const Checkout checkout;
    const cparser::MacroTable empty;
    const cparser::TranslationUnit source = cparser::preprocessText(
        empty, "dl.c",
        "gsDPPipeSync(),\n"
        "gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),\n"
        "gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2),\n"
        "gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),\n"
        "gsSPSetGeometryMode(G_LIGHTING),\n"
        "gsDPLoadTextureBlock(sTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, 0, 0, 0, 0, 0, 0),\n"
        "gsSPVertex(&sVtx[2], 3, 0),\n"
        "gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),\n"
        "gsSPDisplayList(0x09000000),\n"
        "gsSPEndDisplayList(),\n",
        {});
    const auto findSymbol =
        [](const std::string_view name) -> std::optional<cparser::SymbolAddress> {
        if (name == "sTex") {
            return cparser::SymbolAddress{.address = 0x06000800, .elementSize = 8};
        }
        if (name == "sVtx") {
            return cparser::SymbolAddress{.address = 0x06000000, .elementSize = 16};
        }
        return std::nullopt;
    };
    const z64::DisplayList list =
        z64::DisplayListEncoder(
            z64::DisplayListContext{.macros = &checkout.gbi->macros, .findSymbol = findSymbol})
            .encode(std::span(source.tokens).first(source.tokens.size() - 1));

    for (const base::Diagnostic& diagnostic : list.diagnostics) {
        UNSCOPED_INFO(diagnostic.message);
    }
    REQUIRE(list.diagnostics.empty());
    REQUIRE(readWords(list) ==
            std::vector<u32>{
                0xE7000000, 0x00000000, 0xFCFFFFFF, 0xFFFE793C, 0xE200001C, 0x00552078, 0xD7000002,
                0xFFFFFFFF, 0xD9FFFFFF, 0x00020000, 0xFD100000, 0x06000800, 0xF5100000, 0x07000000,
                0xE6000000, 0x00000000, 0xF3000000, 0x073FF100, 0xE7000000, 0x00000000, 0xF5101000,
                0x00000000, 0xF2000000, 0x0007C07C, 0x01003006, 0x06000020, 0x06000204, 0x0006080A,
                0xDE000000, 0x09000000, 0xDF000000, 0x00000000,
            });
}

TEST_CASE("every display list in the checkout's assets encodes without a warning",
          "[hackeroot][gbi][checkout]") {
    const Checkout checkout;
    const hackeroot::Assets& assets = checkout.workspace->getAssets();
    std::vector<const hackeroot::AssetDirectory*> folders;
    folders.reserve(assets.scenes.size() + assets.objects.size());
    for (const hackeroot::AssetDirectory& folder : assets.scenes) {
        folders.push_back(&folder);
    }
    for (const hackeroot::AssetDirectory& folder : assets.objects) {
        folders.push_back(&folder);
    }

    usize lists = 0;
    usize commands = 0;
    std::map<std::string, usize> problems;
    for (const hackeroot::AssetDirectory* folder : folders) {
        for (const std::filesystem::path& file : listSources(folder->directory)) {
            const std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
                hackeroot::preprocessAsset(*checkout.workspace, file);
            REQUIRE(unit.has_value());
            const cparser::ParseResult parsed = cparser::parseDeclarations(
                *unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});
            std::map<std::string_view, usize> elementSizes;
            for (const cparser::Declaration& declaration : parsed.declarations) {
                elementSizes[declaration.name] = findElementSize(declaration.type);
            }
            const auto findSymbol =
                [&elementSizes](
                    const std::string_view name) -> std::optional<cparser::SymbolAddress> {
                const auto found = elementSizes.find(name);
                if (found == elementSizes.end()) {
                    if (std::ranges::find(CodeSymbols, name) != CodeSymbols.end()) {
                        return cparser::SymbolAddress{.address = 0, .elementSize = 64};
                    }
                    return std::nullopt;
                }
                return cparser::SymbolAddress{.address = 0, .elementSize = found->second};
            };
            cparser::MacroTable macros(&checkout.gbi->macros);
            macros.defineFrom(unit->macros);
            z64::DisplayListEncoder encoder(
                z64::DisplayListContext{.macros = &macros, .findSymbol = findSymbol});
            for (const cparser::Declaration& declaration : parsed.declarations) {
                if (declaration.type != "Gfx" || !declaration.hasInitializer) {
                    continue;
                }
                const z64::DisplayList list = encoder.encode(declaration.initializer);
                ++lists;
                commands += list.bytes.size() / 8;
                for (const base::Diagnostic& diagnostic : list.diagnostics) {
                    if (diagnostic.severity != base::Severity::Note) {
                        ++problems[file.filename().string() + ": " + diagnostic.message];
                    }
                }
            }
        }
    }
    WARN(lists << " display lists, " << commands << " commands, " << problems.size()
               << " distinct problems");
    std::string listed;
    for (const auto& [problem, count] : problems) {
        listed += std::to_string(count) + "x " + problem + "\n";
    }
    INFO(listed);
    REQUIRE(lists > 1000);
    REQUIRE(problems.empty());
}

TEST_CASE("every gbi.h macro decodes back under its own name", "[hackeroot][gbi][checkout]") {
    const Checkout checkout;
    constexpr std::array<std::pair<std::string_view, std::string_view>, 40> Macros{{
        {"gsDPNoOp()", "gsDPNoOp"},
        {"gsSPNoOp()", "gsSPNoOp"},
        {"gsDPPipeSync()", "gsDPPipeSync"},
        {"gsDPLoadSync()", "gsDPLoadSync"},
        {"gsDPTileSync()", "gsDPTileSync"},
        {"gsDPFullSync()", "gsDPFullSync"},
        {"gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE)", "gsDPSetCombineMode"},
        {"gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2)", "gsDPSetRenderMode"},
        {"gsDPSetCycleType(G_CYC_2CYCLE)", "gsDPSetCycleType"},
        {"gsDPSetTextureFilter(G_TF_BILERP)", "gsDPSetTextureFilter"},
        {"gsDPSetTextureLUT(G_TT_NONE)", "gsDPSetTextureLUT"},
        {"gsDPSetTexturePersp(G_TP_PERSP)", "gsDPSetTexturePersp"},
        {"gsDPSetTextureLOD(G_TL_TILE)", "gsDPSetTextureLOD"},
        {"gsDPSetAlphaCompare(G_AC_NONE)", "gsDPSetAlphaCompare"},
        {"gsDPSetDepthSource(G_ZS_PIXEL)", "gsDPSetDepthSource"},
        {"gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON)", "gsSPTexture"},
        {"gsSPSetGeometryMode(G_LIGHTING)", "gsSPSetGeometryMode"},
        {"gsSPClearGeometryMode(G_FOG)", "gsSPClearGeometryMode"},
        {"gsSPGeometryMode(G_FOG, G_LIGHTING)", "gsSPGeometryMode"},
        {"gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE)", "gsSPLoadGeometryMode"},
        {"gsSPVertex(&sVtx[2], 3, 0)", "gsSPVertex"},
        {"gsSP1Triangle(0, 1, 2, 0)", "gsSP1Triangle"},
        {"gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0)", "gsSP2Triangles"},
        {"gsSPCullDisplayList(0, 7)", "gsSPCullDisplayList"},
        {"gsSPModifyVertex(0, G_MWO_POINT_RGBA, 0)", "gsSPModifyVertex"},
        {"gsSPDisplayList(0x09000000)", "gsSPDisplayList"},
        {"gsSPBranchList(0x09000000)", "gsSPBranchList"},
        {"gsSPEndDisplayList()", "gsSPEndDisplayList"},
        {"gsSPMatrix(0x01000000, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW)", "gsSPMatrix"},
        {"gsSPPopMatrix(G_MTX_MODELVIEW)", "gsSPPopMatrix"},
        {"gsSPSegment(8, 0x01000000)", "gsSPSegment"},
        {"gsSPNumLights(NUMLIGHTS_1)", "gsSPNumLights"},
        {"gsSPViewport(&sVtx[0])", "gsSPViewport"},
        {"gsDPSetPrimColor(0, 0, 255, 255, 255, 255)", "gsDPSetPrimColor"},
        {"gsDPSetEnvColor(0, 0, 0, 255)", "gsDPSetEnvColor"},
        {"gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, sTex)", "gsDPSetTextureImage"},
        {"gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_4b, 1, 0, G_TX_RENDERTILE, 0, G_TX_NOMIRROR, 5, 0, "
         "G_TX_MIRROR, 5, 0)",
         "gsDPSetTile"},
        {"gsDPSetTileSize(G_TX_RENDERTILE, 0, 0, 124, 124)", "gsDPSetTileSize"},
        {"gsDPLoadTLUTCmd(G_TX_LOADTILE, 15)", "gsDPLoadTLUTCmd"},
        {"gsDPFillRectangle(0, 0, 319, 239)", "gsDPFillRectangle"},
    }};
    const auto findSymbol =
        [](const std::string_view name) -> std::optional<cparser::SymbolAddress> {
        if (name == "sTex") {
            return cparser::SymbolAddress{.address = 0x06000800, .elementSize = 8};
        }
        if (name == "sVtx") {
            return cparser::SymbolAddress{.address = 0x06000000, .elementSize = 16};
        }
        return std::nullopt;
    };
    const cparser::MacroTable empty;
    for (const auto& [macro, name] : Macros) {
        INFO(macro);
        const cparser::TranslationUnit source =
            cparser::preprocessText(empty, "dl.c", std::string(macro) + ",\n", {});
        const z64::DisplayList list =
            z64::DisplayListEncoder(
                z64::DisplayListContext{.macros = &checkout.gbi->macros, .findSymbol = findSymbol})
                .encode(std::span(source.tokens).first(source.tokens.size() - 1));
        REQUIRE(list.diagnostics.empty());
        const std::vector<u32> words = readWords(list);
        REQUIRE(words.size() >= 2);
        CHECK(n64::decodeCommand(words[0], words[1]).name == name);
    }
}

TEST_CASE("decoded commands read with the names of the checkout's gbi.h",
          "[hackeroot][gbi][checkout]") {
    const Checkout checkout;
    const z64::GbiNames names(checkout.gbi->macros);
    constexpr std::array<std::string_view, 10> Macros{
        "gsDPSetCombineMode(G_CC_MODULATEIDECALA, G_CC_PASS2)",
        "gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE)",
        "gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2)",
        "gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2)",
        "gsSPGeometryMode(G_FOG, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_LIGHTING)",
        "gsDPSetCycleType(G_CYC_2CYCLE)",
        "gsDPSetTextureFilter(G_TF_BILERP)",
        "gsDPSetTextureLUT(G_TT_RGBA16)",
        "gsSPMatrix(0x01000000, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION)",
        "gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_4b, 1, 0, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | "
        "G_TX_WRAP, 5, 0, G_TX_MIRROR | G_TX_WRAP, 5, 0)",
    };
    const cparser::MacroTable empty;
    std::string described;
    for (usize i = 0; i < Macros.size(); ++i) {
        const std::string_view macro = Macros[i];
        const cparser::TranslationUnit source =
            cparser::preprocessText(empty, "dl.c", std::string(macro) + ",\n", {});
        const z64::DisplayList list =
            z64::DisplayListEncoder(z64::DisplayListContext{.macros = &checkout.gbi->macros})
                .encode(std::span(source.tokens).first(source.tokens.size() - 1));
        REQUIRE(list.diagnostics.empty());
        const std::vector<u32> words = readWords(list);
        const std::string text = names.makeCommandText(n64::decodeCommand(words[0], words[1]), {});
        described += std::format("\n  {}\n    {}", macro, text);
        if (i < 8) {
            CHECK(text == macro);
        }
    }
    WARN("decoded:" << described);
    CHECK(names.makeCommandText(n64::decodeCommand(0xE300'0A01, 0x0010'0000), {}) ==
          "gsDPSetCycleType(G_CYC_2CYCLE)");
}

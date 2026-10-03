#include "cparser/Preprocessor.h"

#include "Generated.h"
#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/SourceFile.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "cparser/fixtures";
}

[[nodiscard]] bool hasIdentifier(const cparser::TranslationUnit& unit, std::string_view name) {
    return std::ranges::any_of(unit.tokens, [name](const cparser::Token& token) {
        return cparser::isIdentifier(token, name);
    });
}

[[nodiscard]] cparser::usize indexOf(const cparser::TranslationUnit& unit, std::string_view name) {
    const auto found = std::ranges::find_if(unit.tokens, [name](const cparser::Token& token) {
        return cparser::isIdentifier(token, name);
    });
    REQUIRE(found != unit.tokens.end());
    return static_cast<cparser::usize>(found - unit.tokens.begin());
}

[[nodiscard]] cparser::usize lastIndexOf(const cparser::TranslationUnit& unit,
                                         std::string_view name) {
    const auto found = std::ranges::find_if(
        unit.tokens.rbegin(), unit.tokens.rend(),
        [name](const cparser::Token& token) { return cparser::isIdentifier(token, name); });
    REQUIRE(found != unit.tokens.rend());
    return static_cast<cparser::usize>(unit.tokens.rend() - found) - 1;
}

[[nodiscard]] bool reports(const cparser::TranslationUnit& unit, std::string_view text) {
    return std::ranges::any_of(unit.diagnostics, [text](const base::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    });
}

class Harness {
public:
    [[nodiscard]] cparser::TranslationUnit read(const std::filesystem::path& entry,
                                                const cparser::PreprocessOptions& options) const {
        std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
            cparser::preprocess(base, entry, options);
        REQUIRE(unit.has_value());
        return std::move(*unit);
    }

    cparser::MacroTable base;
};

} // namespace

TEST_CASE("includes are followed only inside the allowed roots", "[cparser][preprocessor]") {
    const Harness harness;
    const cparser::TranslationUnit unit =
        harness.read(fixtures() / "include/scene.c",
                     cparser::PreprocessOptions{.allowedRoots = {fixtures() / "include"}});

    SECTION("an in-scope include is spliced in where it appears") {
        REQUIRE(indexOf(unit, "sSharedMarker") < indexOf(unit, "sRoomMarker"));
        REQUIRE(indexOf(unit, "sRoomMarker") < indexOf(unit, "sSceneMarker"));
    }

    SECTION("a system include is never followed") {
        REQUIRE_FALSE(hasIdentifier(unit, "u32"));
        REQUIRE(reports(unit, "<ultra64.h>"));
    }

    SECTION("an include escaping the roots is reported and skipped") {
        REQUIRE_FALSE(hasIdentifier(unit, "sOutsideMarker"));
        REQUIRE(reports(unit, "outside the asset tree"));
    }

    SECTION("a cycle stops at the file that is already open") {
        REQUIRE(reports(unit, "already open"));
    }

    SECTION("an include that does not exist is a diagnostic, not a failure") {
        REQUIRE(reports(unit, "missing.c"));
    }

    SECTION("every token points into a file the unit owns") {
        for (const cparser::Token& token : unit.tokens) {
            REQUIRE(token.file != nullptr);
            REQUIRE(unit.sources.find(token.file->path) == token.file);
            if (token.kind == cparser::TokenKind::EndOfFile) {
                continue;
            }
            const std::string_view contents = token.file->contents;
            REQUIRE(token.text.data() >= contents.data());
            REQUIRE(token.text.data() + token.text.size() <= contents.data() + contents.size());
        }
    }
}

TEST_CASE("an unreadable entry file is the one hard failure", "[cparser][preprocessor]") {
    const cparser::MacroTable base;

    const std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        cparser::preprocess(base, fixtures() / "include/nothing_here.c", {});

    REQUIRE_FALSE(unit.has_value());
    REQUIRE(unit.error().severity == base::Severity::Error);
    REQUIRE(unit.error().location.file.filename() == "nothing_here.c");
}

TEST_CASE("the depth limit stops a runaway include chain", "[cparser][preprocessor]") {
    const Harness harness;
    const cparser::TranslationUnit unit = harness.read(
        fixtures() / "include/scene.c",
        cparser::PreprocessOptions{.allowedRoots = {fixtures() / "include"}, .maxDepth = 1});

    REQUIRE(hasIdentifier(unit, "sRoomMarker"));
    REQUIRE_FALSE(hasIdentifier(unit, "sSharedMarker"));
    REQUIRE(reports(unit, "includes deep"));
}

TEST_CASE("a conditional in an asset file is evaluated, not skipped", "[cparser][preprocessor]") {
    const Harness harness;
    const cparser::TranslationUnit unit =
        harness.read(fixtures() / "asset/room.c",
                     cparser::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    REQUIRE(hasIdentifier(unit, "ACTOR_PLAYER"));
    REQUIRE_FALSE(hasIdentifier(unit, "ACTOR_EN_TEST"));
}

TEST_CASE("a define in an included header is in scope for the file that included it",
          "[cparser][preprocessor]") {
    const Harness harness;
    const cparser::TranslationUnit unit =
        harness.read(fixtures() / "asset/room.c",
                     cparser::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    const cparser::Macro* length = unit.macros.find("LENGTH_sActorList");
    REQUIRE(length != nullptr);
    REQUIRE(length->body.size() == 1);
    REQUIRE(length->body.front().text == "2");

    const cparser::Macro* degrees = unit.macros.find("DEG_TO_BINANG");
    REQUIRE(degrees != nullptr);
    REQUIRE(degrees->functionLike);
    REQUIRE(degrees->parameters == std::vector<std::string>{"degrees"});

    SECTION("and in no other unit read against the same base") {
        REQUIRE_FALSE(harness.base.contains("LENGTH_sActorList"));
        REQUIRE_FALSE(harness.base.contains("ROOM_H"));
    }
}

TEST_CASE("reading a file twice against one base reads it the same way",
          "[cparser][preprocessor]") {
    const Harness harness;
    const cparser::PreprocessOptions options{.allowedRoots = {fixtures() / "asset"},
                                             .missingInclude = embedGenerated};

    const cparser::TranslationUnit first = harness.read(fixtures() / "asset/room.c", options);
    const cparser::TranslationUnit second = harness.read(fixtures() / "asset/room.c", options);

    REQUIRE(second.tokens.size() == first.tokens.size());
    REQUIRE(second.embedded.size() == first.embedded.size());
    REQUIRE(hasIdentifier(second, "sActorList"));
}

TEST_CASE("a unit outlives nothing but its base", "[cparser][preprocessor]") {
    const cparser::MacroTable base;
    std::expected<cparser::TranslationUnit, base::Diagnostic> read =
        cparser::preprocess(base, fixtures() / "asset/room.c",
                            cparser::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});
    REQUIRE(read.has_value());

    const cparser::TranslationUnit moved = std::move(*read);

    REQUIRE(hasIdentifier(moved, "sVertices"));
    REQUIRE(moved.macros.find("LENGTH_sActorList")->body.front().text == "2");
}

TEST_CASE("an include the caller resolves to an embedded file stands for that file",
          "[cparser][preprocessor]") {
    const Harness harness;
    const cparser::TranslationUnit unit =
        harness.read(fixtures() / "asset/room.c",
                     cparser::PreprocessOptions{.allowedRoots = {fixtures() / "asset"},
                                                .missingInclude = embedGenerated});

    REQUIRE(unit.embedded.size() == 2);
    REQUIRE(unit.embedded[0].path.filename() == "sFloorTex.rgba16.inc.c");
    REQUIRE(unit.embedded[0].tag == "generated");
    REQUIRE(unit.embedded[0].location.line > 0);

    SECTION("each embedded file stands in the token stream where its include was") {
        std::vector<cparser::usize> positions;
        for (cparser::usize at = 0; at < unit.tokens.size(); ++at) {
            if (unit.tokens[at].kind == cparser::TokenKind::Embedded) {
                REQUIRE(unit.tokens[at].embedded == positions.size());
                REQUIRE(unit.tokens[at].text.contains(".inc.c"));
                positions.push_back(at);
            }
        }
        REQUIRE(positions.size() == 2);
        REQUIRE(cparser::isPunctuator(unit.tokens[positions[0] - 1], "{"));
        REQUIRE(cparser::isPunctuator(unit.tokens[positions[0] + 1], "}"));
        REQUIRE(lastIndexOf(unit, "sFloorTex") < positions[0]);
        REQUIRE(positions[0] < lastIndexOf(unit, "sPalette"));
        REQUIRE(lastIndexOf(unit, "sPalette") < positions[1]);
    }
}

TEST_CASE("without a rule for missing includes, a missing include is only missing",
          "[cparser][preprocessor]") {
    const Harness harness;
    const cparser::TranslationUnit unit =
        harness.read(fixtures() / "asset/room.c",
                     cparser::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    REQUIRE(unit.embedded.empty());
    REQUIRE(reports(unit, "sFloorTex.rgba16.inc.c"));
}

TEST_CASE("an entry held only in memory resolves includes from where it says it is",
          "[cparser][preprocessor]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = cparser::preprocessText(
        base, fixtures() / "asset/<memory>", "#define FROM_MEMORY 1\n#include \"room.h\"\n",
        cparser::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    REQUIRE(unit.macros.contains("FROM_MEMORY"));
    REQUIRE(unit.macros.contains("LENGTH_sActorList"));
    REQUIRE(unit.sources.count() == 2);
    REQUIRE(unit.sources.getFiles().front()->path.filename() == "<memory>");
}

TEST_CASE("a strict condition guarded by defined() does not warn about what it guards",
          "[cparser][preprocessor]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        cparser::preprocessText(base, fixtures() / "asset/<memory>",
                                "#if defined(NOT_DEFINED) && NOT_DEFINED > 2\nint a;\n#endif\n",
                                cparser::PreprocessOptions{.strictConditionals = true});

    REQUIRE(unit.diagnostics.empty());
    REQUIRE_FALSE(hasIdentifier(unit, "a"));
}

TEST_CASE("a macro body continues past a comment that spans lines", "[cparser][preprocessor]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        cparser::preprocessText(base, fixtures() / "asset/<memory>",
                                "#define A 1 /* spans\n lines */ + 2\nint v = A;\n", {});

    const cparser::Macro* macro = unit.macros.find("A");
    REQUIRE(macro != nullptr);
    REQUIRE(macro->body.size() == 3);
}

TEST_CASE("a directive's diagnostics say where the directive is", "[cparser][preprocessor]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        cparser::preprocessText(base, fixtures() / "asset/<memory>",
                                "#endif\n#else\n#ifdef\n#endif\n#if\n#endif\n#define\n#if 1\n", {});

    REQUIRE(unit.diagnostics.size() == 6);
    for (const base::Diagnostic& diagnostic : unit.diagnostics) {
        INFO(diagnostic.message);
        REQUIRE(diagnostic.location.file.filename() == "<memory>");
        REQUIRE(diagnostic.location.line > 0);
    }
    REQUIRE(unit.diagnostics.back().location.line == 8);
    REQUIRE(unit.diagnostics.back().message.contains("never closed"));
}

TEST_CASE("an include through a file as if it were a directory is a diagnostic",
          "[cparser][preprocessor]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = cparser::preprocessText(
        base, fixtures() / "asset/<memory>", "#include \"room.c/inside.h\"\n", {});

    REQUIRE(reports(unit, "cannot find"));
}

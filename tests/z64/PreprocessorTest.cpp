#include "z64/Preprocessor.h"

#include "z64/Diagnostic.h"
#include "z64/MacroTable.h"
#include "z64/SourceFile.h"
#include "z64/Token.h"
#include "z64/Types.h"

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
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures";
}

[[nodiscard]] bool hasIdentifier(const z64::TranslationUnit& unit, std::string_view name) {
    return std::ranges::any_of(
        unit.tokens, [name](const z64::Token& token) { return z64::isIdentifier(token, name); });
}

[[nodiscard]] z64::usize indexOf(const z64::TranslationUnit& unit, std::string_view name) {
    const auto found = std::ranges::find_if(
        unit.tokens, [name](const z64::Token& token) { return z64::isIdentifier(token, name); });
    REQUIRE(found != unit.tokens.end());
    return static_cast<z64::usize>(found - unit.tokens.begin());
}

[[nodiscard]] bool reports(const z64::TranslationUnit& unit, std::string_view text) {
    return std::ranges::any_of(unit.diagnostics, [text](const z64::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    });
}

class Harness {
public:
    [[nodiscard]] z64::TranslationUnit read(const std::filesystem::path& entry,
                                            const z64::PreprocessOptions& options) const {
        std::expected<z64::TranslationUnit, z64::Diagnostic> unit =
            z64::preprocess(base, entry, options);
        REQUIRE(unit.has_value());
        return std::move(*unit);
    }

    z64::MacroTable base;
};

} // namespace

TEST_CASE("includes are followed only inside the allowed roots", "[z64][preprocessor]") {
    const Harness harness;
    const z64::TranslationUnit unit =
        harness.read(fixtures() / "include/scene.c",
                     z64::PreprocessOptions{.allowedRoots = {fixtures() / "include"}});

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
        for (const z64::Token& token : unit.tokens) {
            REQUIRE(token.file != nullptr);
            REQUIRE(unit.sources.find(token.file->path) == token.file);
            if (token.kind == z64::TokenKind::EndOfFile) {
                continue;
            }
            const std::string_view contents = token.file->contents;
            REQUIRE(token.text.data() >= contents.data());
            REQUIRE(token.text.data() + token.text.size() <= contents.data() + contents.size());
        }
    }
}

TEST_CASE("an unreadable entry file is the one hard failure", "[z64][preprocessor]") {
    const z64::MacroTable base;

    const std::expected<z64::TranslationUnit, z64::Diagnostic> unit =
        z64::preprocess(base, fixtures() / "include/nothing_here.c", {});

    REQUIRE_FALSE(unit.has_value());
    REQUIRE(unit.error().severity == z64::Severity::Error);
    REQUIRE(unit.error().location.file.filename() == "nothing_here.c");
}

TEST_CASE("the depth limit stops a runaway include chain", "[z64][preprocessor]") {
    const Harness harness;
    const z64::TranslationUnit unit = harness.read(
        fixtures() / "include/scene.c",
        z64::PreprocessOptions{.allowedRoots = {fixtures() / "include"}, .maxDepth = 1});

    REQUIRE(hasIdentifier(unit, "sRoomMarker"));
    REQUIRE_FALSE(hasIdentifier(unit, "sSharedMarker"));
    REQUIRE(reports(unit, "includes deep"));
}

TEST_CASE("a conditional in an asset file is evaluated, not skipped", "[z64][preprocessor]") {
    const Harness harness;
    const z64::TranslationUnit unit =
        harness.read(fixtures() / "asset/room.c",
                     z64::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    REQUIRE(hasIdentifier(unit, "ACTOR_PLAYER"));
    REQUIRE_FALSE(hasIdentifier(unit, "ACTOR_EN_TEST"));
}

TEST_CASE("a define in an included header is in scope for the file that included it",
          "[z64][preprocessor]") {
    const Harness harness;
    const z64::TranslationUnit unit =
        harness.read(fixtures() / "asset/room.c",
                     z64::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    const z64::Macro* length = unit.macros.find("LENGTH_sActorList");
    REQUIRE(length != nullptr);
    REQUIRE(length->body.size() == 1);
    REQUIRE(length->body.front().text == "2");

    const z64::Macro* degrees = unit.macros.find("DEG_TO_BINANG");
    REQUIRE(degrees != nullptr);
    REQUIRE(degrees->functionLike);
    REQUIRE(degrees->parameters == std::vector<std::string>{"degrees"});

    SECTION("and in no other unit read against the same base") {
        REQUIRE_FALSE(harness.base.contains("LENGTH_sActorList"));
        REQUIRE_FALSE(harness.base.contains("ROOM_H"));
    }
}

TEST_CASE("reading a file twice against one base reads it the same way", "[z64][preprocessor]") {
    const Harness harness;
    const z64::PreprocessOptions options{.allowedRoots = {fixtures() / "asset"}};

    const z64::TranslationUnit first = harness.read(fixtures() / "asset/room.c", options);
    const z64::TranslationUnit second = harness.read(fixtures() / "asset/room.c", options);

    REQUIRE(second.tokens.size() == first.tokens.size());
    REQUIRE(second.images.size() == first.images.size());
    REQUIRE(hasIdentifier(second, "sActorList"));
}

TEST_CASE("a unit outlives nothing but its base", "[z64][preprocessor]") {
    const z64::MacroTable base;
    std::expected<z64::TranslationUnit, z64::Diagnostic> read =
        z64::preprocess(base, fixtures() / "asset/room.c",
                        z64::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});
    REQUIRE(read.has_value());

    const z64::TranslationUnit moved = std::move(*read);

    REQUIRE(hasIdentifier(moved, "sVertices"));
    REQUIRE(moved.macros.find("LENGTH_sActorList")->body.front().text == "2");
}

TEST_CASE("texture data the build would have generated resolves to its image",
          "[z64][preprocessor]") {
    const Harness harness;
    const z64::TranslationUnit unit =
        harness.read(fixtures() / "asset/room.c",
                     z64::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    REQUIRE(unit.images.size() == 2);
    REQUIRE(unit.images[0].path.filename() == "sFloorTex.rgba16.png");
    REQUIRE(unit.images[0].format == "rgba16");

    SECTION("a paletted image names its palette, so the include is only a prefix") {
        REQUIRE(unit.images[1].path.filename() == "sPalette.ci8.tlut_sRoomTlut.png");
        REQUIRE(unit.images[1].format == "ci8");
    }
}

TEST_CASE("an entry held only in memory resolves includes from where it says it is",
          "[z64][preprocessor]") {
    const z64::MacroTable base;
    const z64::TranslationUnit unit = z64::preprocessText(
        base, fixtures() / "asset/<memory>", "#define FROM_MEMORY 1\n#include \"room.h\"\n",
        z64::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});

    REQUIRE(unit.macros.contains("FROM_MEMORY"));
    REQUIRE(unit.macros.contains("LENGTH_sActorList"));
    REQUIRE(unit.sources.count() == 2);
    REQUIRE(unit.sources.getFiles().front()->path.filename() == "<memory>");
}

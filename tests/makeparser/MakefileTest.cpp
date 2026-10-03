#include "makeparser/Makefile.h"

#include "base/Diagnostic.h"
#include "makeparser/MakeVariables.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] std::filesystem::path getFixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "makeparser/fixtures";
}

[[nodiscard]] makeparser::Makefile read(std::string_view name,
                                        const makeparser::MakeOverrides& overrides = {}) {
    std::expected<makeparser::Makefile, base::Diagnostic> makefile =
        makeparser::readMakefile(getFixtures() / name, overrides);
    REQUIRE(makefile.has_value());
    return std::move(*makefile);
}

[[nodiscard]] makeparser::MakeText expandVariable(makeparser::Makefile& makefile,
                                                  std::string_view name) {
    return makefile.variables.expand("$(" + std::string(name) + ")", makeparser::MakeSite{},
                                     makefile.diagnostics);
}

[[nodiscard]] bool isDefined(const makeparser::Makefile& makefile, std::string_view name) {
    return makefile.variables.find(name) != nullptr;
}

[[nodiscard]] bool reports(const makeparser::Makefile& makefile, base::Severity severity,
                           std::string_view text) {
    return std::ranges::any_of(makefile.diagnostics, [&](const base::Diagnostic& diagnostic) {
        return diagnostic.severity == severity && diagnostic.message.contains(text);
    });
}

} // namespace

TEST_CASE("each assignment flavour binds when make would bind it", "[makeparser]") {
    makeparser::Makefile makefile = read("variables.mk");

    REQUIRE(expandVariable(makefile, "LATE").text == "bound late");
    REQUIRE(expandVariable(makefile, "EARLY").text.empty());
    REQUIRE(expandVariable(makefile, "ONCE").text == "first");
    REQUIRE(expandVariable(makefile, "LIST").text == "a b bound late");
    REQUIRE(expandVariable(makefile, "EXPORTED").text == "yes");
}

TEST_CASE("comments, continuations and define blocks read as make reads them", "[makeparser]") {
    makeparser::Makefile makefile = read("variables.mk");

    REQUIRE(expandVariable(makefile, "WITH_COMMENT").text == "kept");
    REQUIRE(expandVariable(makefile, "CONTINUED").text == "one two");
    REQUIRE(expandVariable(makefile, "BLOCK").text == "line one\nline two");
}

TEST_CASE("the command line beats the Makefile unless it says override", "[makeparser]") {
    makeparser::Makefile makefile =
        read("variables.mk", {{"FROM_COMMAND_LINE", "command"}, {"FORCED", "command"}});

    REQUIRE(expandVariable(makefile, "FROM_COMMAND_LINE").text == "command");
    REQUIRE(expandVariable(makefile, "FORCED").text == "file value");
}

TEST_CASE("a shell command is never run, and what it fed is uncertain", "[makeparser]") {
    makeparser::Makefile makefile = read("variables.mk");

    const makeparser::MakeText shelled = expandVariable(makefile, "SHELLED");
    REQUIRE(shelled.text.empty());
    REQUIRE_FALSE(shelled.certain);
    REQUIRE_FALSE(expandVariable(makefile, "SHELL_ASSIGN").certain);
    REQUIRE(reports(makefile, base::Severity::Note, "$(shell uname)"));
    REQUIRE(expandVariable(makefile, "LATE").certain);
}

TEST_CASE("a variable that refers to itself is an error, not a hang", "[makeparser]") {
    makeparser::Makefile makefile = read("variables.mk");

    REQUIRE_FALSE(expandVariable(makefile, "SELF").certain);
    REQUIRE(reports(makefile, base::Severity::Error, "'SELF' refers to itself"));
}

TEST_CASE("conditionals choose one branch and never evaluate the others", "[makeparser]") {
    makeparser::Makefile makefile = read("conditionals.mk");

    REQUIRE(expandVariable(makefile, "CHOSEN").text == "b");
    REQUIRE(expandVariable(makefile, "NOT_A").text == "yes");
    REQUIRE(expandVariable(makefile, "HAS_VERSION").text == "yes");
    REQUIRE(expandVariable(makefile, "NO_NOTHING").text == "yes");
    REQUIRE(expandVariable(makefile, "NESTED").text == "right");
    REQUIRE_FALSE(reports(makefile, base::Severity::Warning, "unsupported"));
}

TEST_CASE("an error in the branch taken is reported", "[makeparser]") {
    const makeparser::Makefile makefile = read("conditionals.mk", {{"VERSION", "c"}});

    REQUIRE_FALSE(isDefined(makefile, "CHOSEN"));
    REQUIRE(reports(makefile, base::Severity::Warning, "unsupported c"));
}

TEST_CASE("a branch chosen by a shell command is marked uncertain", "[makeparser]") {
    makeparser::Makefile makefile = read("conditionals.mk");

    const makeparser::MakeText guessed = expandVariable(makefile, "GUESSED");
    REQUIRE(guessed.text == "other");
    REQUIRE_FALSE(guessed.certain);
    REQUIRE(expandVariable(makefile, "CHOSEN").certain);
}

TEST_CASE("recipes are never read, but a tab before any rule is", "[makeparser]") {
    const makeparser::Makefile makefile = read("conditionals.mk");

    REQUIRE(isDefined(makefile, "TABBED"));
    REQUIRE_FALSE(isDefined(makefile, "RECIPE_ONLY"));
    REQUIRE_FALSE(isDefined(makefile, "STILL_RECIPE"));
    REQUIRE(isDefined(makefile, "AFTER_RULE"));
}

TEST_CASE("includes are followed, and only a required one may be missing loudly", "[makeparser]") {
    const makeparser::Makefile makefile = read("conditionals.mk");
    REQUIRE(isDefined(makefile, "FROM_INCLUDE"));
    REQUIRE_FALSE(reports(makefile, base::Severity::Warning, "not_there.mk"));

    const makeparser::Makefile missing = read("missing_include.mk");
    REQUIRE(reports(missing, base::Severity::Warning, "nowhere.mk"));
}

TEST_CASE("text functions compute what make computes", "[makeparser]") {
    makeparser::Makefile makefile = read("functions.mk");

    REQUIRE(expandVariable(makefile, "SOURCES").text == "a.c b.c");
    REQUIRE(expandVariable(makefile, "HEADERS").text == "c.h");
    REQUIRE(expandVariable(makefile, "OBJECTS").text == "a.o b.o");
    REQUIRE(expandVariable(makefile, "SUFFIXED").text == "a.o b.o");
    REQUIRE(expandVariable(makefile, "UPPER").text == "bAnAnA");
    REQUIRE(expandVariable(makefile, "STRIPPED").text == "a b");
    REQUIRE(expandVariable(makefile, "PICKED").text == "yes");
    REQUIRE(expandVariable(makefile, "EMPTY_PICK").text == "no");
    REQUIRE(expandVariable(makefile, "CALLED").text == "hello x and y");
    REQUIRE(expandVariable(makefile, "LOOPED").text == "[a.c] [b.c]");
    REQUIRE(expandVariable(makefile, "ORIGIN_FILE").text == "file");
    REQUIRE(expandVariable(makefile, "ORIGIN_NONE").text == "undefined");
    REQUIRE(expandVariable(makefile, "FIRST").text == "a.c");
    REQUIRE(expandVariable(makefile, "COUNT").text == "3");
    REQUIRE(expandVariable(makefile, "DIRS").text == "src/ ./");
    REQUIRE(expandVariable(makefile, "NAMES").text == "a.c b.c");
    REQUIRE(expandVariable(makefile, "SORTED").text == "a b c");
    REQUIRE(expandVariable(makefile, "FOUND").text == "an");
    REQUIRE(expandVariable(makefile, "PREFIXED").text == "-Iinc -Isrc");
    REQUIRE(expandVariable(makefile, "DOLLAR").text == "$HOME");
}

TEST_CASE("a Makefile that is not there is the one hard failure", "[makeparser]") {
    const std::expected<makeparser::Makefile, base::Diagnostic> makefile =
        makeparser::readMakefile(getFixtures() / "absent.mk", {});

    REQUIRE_FALSE(makefile.has_value());
    REQUIRE(makefile.error().location.file.filename() == "absent.mk");
}

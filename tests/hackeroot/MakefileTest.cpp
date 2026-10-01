#include "hackeroot/Makefile.h"

#include "hackeroot/MakeVariables.h"
#include "z64/Diagnostic.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/make";
}

[[nodiscard]] hackeroot::Makefile read(std::string_view name,
                                       const hackeroot::MakeOverrides& overrides = {}) {
    std::expected<hackeroot::Makefile, z64::Diagnostic> makefile =
        hackeroot::readMakefile(fixtures() / name, overrides);
    REQUIRE(makefile.has_value());
    return std::move(*makefile);
}

[[nodiscard]] hackeroot::MakeText valueOf(hackeroot::Makefile& makefile, std::string_view name) {
    return makefile.variables.expand("$(" + std::string(name) + ")", hackeroot::MakeSite{},
                                     makefile.diagnostics);
}

[[nodiscard]] bool isDefined(const hackeroot::Makefile& makefile, std::string_view name) {
    return makefile.variables.find(name) != nullptr;
}

[[nodiscard]] bool reports(const hackeroot::Makefile& makefile, z64::Severity severity,
                           std::string_view text) {
    return std::ranges::any_of(makefile.diagnostics, [&](const z64::Diagnostic& diagnostic) {
        return diagnostic.severity == severity && diagnostic.message.contains(text);
    });
}

} // namespace

TEST_CASE("each assignment flavour binds when make would bind it", "[hackeroot][make]") {
    hackeroot::Makefile makefile = read("variables.mk");

    REQUIRE(valueOf(makefile, "LATE").text == "bound late");
    REQUIRE(valueOf(makefile, "EARLY").text.empty());
    REQUIRE(valueOf(makefile, "ONCE").text == "first");
    REQUIRE(valueOf(makefile, "LIST").text == "a b bound late");
    REQUIRE(valueOf(makefile, "EXPORTED").text == "yes");
}

TEST_CASE("comments, continuations and define blocks read as make reads them",
          "[hackeroot][make]") {
    hackeroot::Makefile makefile = read("variables.mk");

    REQUIRE(valueOf(makefile, "WITH_COMMENT").text == "kept");
    REQUIRE(valueOf(makefile, "CONTINUED").text == "one two");
    REQUIRE(valueOf(makefile, "BLOCK").text == "line one\nline two");
}

TEST_CASE("the command line beats the Makefile unless it says override", "[hackeroot][make]") {
    hackeroot::Makefile makefile =
        read("variables.mk", {{"FROM_COMMAND_LINE", "command"}, {"FORCED", "command"}});

    REQUIRE(valueOf(makefile, "FROM_COMMAND_LINE").text == "command");
    REQUIRE(valueOf(makefile, "FORCED").text == "file value");
}

TEST_CASE("a shell command is never run, and what it fed is uncertain", "[hackeroot][make]") {
    hackeroot::Makefile makefile = read("variables.mk");

    const hackeroot::MakeText shelled = valueOf(makefile, "SHELLED");
    REQUIRE(shelled.text.empty());
    REQUIRE_FALSE(shelled.certain);
    REQUIRE_FALSE(valueOf(makefile, "SHELL_ASSIGN").certain);
    REQUIRE(reports(makefile, z64::Severity::Note, "$(shell uname)"));
    REQUIRE(valueOf(makefile, "LATE").certain);
}

TEST_CASE("a variable that refers to itself is an error, not a hang", "[hackeroot][make]") {
    hackeroot::Makefile makefile = read("variables.mk");

    REQUIRE_FALSE(valueOf(makefile, "SELF").certain);
    REQUIRE(reports(makefile, z64::Severity::Error, "'SELF' refers to itself"));
}

TEST_CASE("conditionals choose one branch and never evaluate the others", "[hackeroot][make]") {
    hackeroot::Makefile makefile = read("conditionals.mk");

    REQUIRE(valueOf(makefile, "CHOSEN").text == "b");
    REQUIRE(valueOf(makefile, "NOT_A").text == "yes");
    REQUIRE(valueOf(makefile, "HAS_VERSION").text == "yes");
    REQUIRE(valueOf(makefile, "NO_NOTHING").text == "yes");
    REQUIRE(valueOf(makefile, "NESTED").text == "right");
    REQUIRE_FALSE(reports(makefile, z64::Severity::Warning, "unsupported"));
}

TEST_CASE("an error in the branch taken is reported", "[hackeroot][make]") {
    const hackeroot::Makefile makefile = read("conditionals.mk", {{"VERSION", "c"}});

    REQUIRE_FALSE(isDefined(makefile, "CHOSEN"));
    REQUIRE(reports(makefile, z64::Severity::Warning, "unsupported c"));
}

TEST_CASE("a branch chosen by a shell command is marked uncertain", "[hackeroot][make]") {
    hackeroot::Makefile makefile = read("conditionals.mk");

    const hackeroot::MakeText guessed = valueOf(makefile, "GUESSED");
    REQUIRE(guessed.text == "other");
    REQUIRE_FALSE(guessed.certain);
    REQUIRE(valueOf(makefile, "CHOSEN").certain);
}

TEST_CASE("recipes are never read, but a tab before any rule is", "[hackeroot][make]") {
    const hackeroot::Makefile makefile = read("conditionals.mk");

    REQUIRE(isDefined(makefile, "TABBED"));
    REQUIRE_FALSE(isDefined(makefile, "RECIPE_ONLY"));
    REQUIRE_FALSE(isDefined(makefile, "STILL_RECIPE"));
    REQUIRE(isDefined(makefile, "AFTER_RULE"));
}

TEST_CASE("includes are followed, and only a required one may be missing loudly",
          "[hackeroot][make]") {
    const hackeroot::Makefile makefile = read("conditionals.mk");
    REQUIRE(isDefined(makefile, "FROM_INCLUDE"));
    REQUIRE_FALSE(reports(makefile, z64::Severity::Warning, "not_there.mk"));

    const hackeroot::Makefile missing = read("missing_include.mk");
    REQUIRE(reports(missing, z64::Severity::Warning, "nowhere.mk"));
}

TEST_CASE("text functions compute what make computes", "[hackeroot][make]") {
    hackeroot::Makefile makefile = read("functions.mk");

    REQUIRE(valueOf(makefile, "SOURCES").text == "a.c b.c");
    REQUIRE(valueOf(makefile, "HEADERS").text == "c.h");
    REQUIRE(valueOf(makefile, "OBJECTS").text == "a.o b.o");
    REQUIRE(valueOf(makefile, "SUFFIXED").text == "a.o b.o");
    REQUIRE(valueOf(makefile, "UPPER").text == "bAnAnA");
    REQUIRE(valueOf(makefile, "STRIPPED").text == "a b");
    REQUIRE(valueOf(makefile, "PICKED").text == "yes");
    REQUIRE(valueOf(makefile, "EMPTY_PICK").text == "no");
    REQUIRE(valueOf(makefile, "CALLED").text == "hello x and y");
    REQUIRE(valueOf(makefile, "LOOPED").text == "[a.c] [b.c]");
    REQUIRE(valueOf(makefile, "ORIGIN_FILE").text == "file");
    REQUIRE(valueOf(makefile, "ORIGIN_NONE").text == "undefined");
    REQUIRE(valueOf(makefile, "FIRST").text == "a.c");
    REQUIRE(valueOf(makefile, "COUNT").text == "3");
    REQUIRE(valueOf(makefile, "DIRS").text == "src/ ./");
    REQUIRE(valueOf(makefile, "NAMES").text == "a.c b.c");
    REQUIRE(valueOf(makefile, "SORTED").text == "a b c");
    REQUIRE(valueOf(makefile, "FOUND").text == "an");
    REQUIRE(valueOf(makefile, "PREFIXED").text == "-Iinc -Isrc");
    REQUIRE(valueOf(makefile, "DOLLAR").text == "$HOME");
}

TEST_CASE("a Makefile that is not there is the one hard failure", "[hackeroot][make]") {
    const std::expected<hackeroot::Makefile, z64::Diagnostic> makefile =
        hackeroot::readMakefile(fixtures() / "absent.mk", {});

    REQUIRE_FALSE(makefile.has_value());
    REQUIRE(makefile.error().location.file.filename() == "absent.mk");
}

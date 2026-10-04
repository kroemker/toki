#include "app/build/domain/BuildRunner.h"

#include "app/build/domain/Process.h"
#include "hackeroot/BuildCommand.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using toki::build::BuildOutcome;
using toki::build::BuildRunner;
using toki::build::BuildStatus;

TEST_CASE("a command line splits back into the same arguments", "[build][process]") {
    const std::vector<std::string> arguments{
        "make", "-j8", "", "a b", R"(say "hi")", R"(C:\dir with space\)", R"(back\slash)"};

    const std::string expected =
        R"(make -j8 "" "a b" "say \"hi\"" "C:\dir with space\\" back\slash)";

    REQUIRE(toki::build::makeCommandLine(arguments) == expected);
}

#ifdef _WIN32

namespace {

BuildOutcome waitForOutcome(BuildRunner& runner) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (const std::optional<BuildOutcome> outcome = runner.update()) {
            return *outcome;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL("the build did not end");
}

} // namespace

TEST_CASE("a build's output and errors reach the log in order", "[build][runner]") {
    BuildRunner runner;
    REQUIRE(runner.startCommand({"cmd.exe", "/c", "echo first & echo second 1>&2 & echo third"}));
    REQUIRE(runner.isRunning());

    const BuildOutcome outcome = waitForOutcome(runner);

    REQUIRE(outcome.status == BuildStatus::Succeeded);
    const auto& lines = runner.getLog().getLines();
    REQUIRE(lines.size() == 5);
    REQUIRE(lines[0].starts_with("$ cmd.exe"));
    REQUIRE(lines[1] == "first ");
    REQUIRE(lines[2] == "second  ");
    REQUIRE(lines[3] == "third");
    REQUIRE(lines[4].starts_with("Build finished"));
}

TEST_CASE("a failing build reports its exit code", "[build][runner]") {
    BuildRunner runner;
    REQUIRE(runner.startCommand({"cmd.exe", "/c", "exit 3"}));

    const BuildOutcome outcome = waitForOutcome(runner);

    REQUIRE(outcome.status == BuildStatus::Failed);
    REQUIRE(outcome.summary.starts_with("make exited with code 3"));
}

TEST_CASE("a stopped build ends, and another can start", "[build][runner]") {
    BuildRunner runner;
    REQUIRE(runner.startCommand({"cmd.exe", "/c", "ping -n 30 127.0.0.1 > nul"}));
    REQUIRE_FALSE(runner.startCommand({"cmd.exe", "/c", "exit 0"}));

    runner.stop();
    const BuildOutcome stopped = waitForOutcome(runner);
    REQUIRE(stopped.status == BuildStatus::Stopped);

    REQUIRE(runner.startCommand({"cmd.exe", "/c", "exit 0"}));
    REQUIRE(waitForOutcome(runner).status == BuildStatus::Succeeded);
}

TEST_CASE("a command that does not exist cannot start", "[build][runner]") {
    BuildRunner runner;

    const auto started = runner.startCommand({"no-such-program-toki.exe"});

    REQUIRE_FALSE(started.has_value());
    REQUIRE(started.error().message.contains("no-such-program-toki.exe"));
    REQUIRE_FALSE(runner.isRunning());
}

TEST_CASE("make runs inside the checkout's distribution", "[build][runner][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::vector<std::string> command =
        hackeroot::makeBuildCommand(repository, {}, hackeroot::BuildTarget::Build, 1);
    command.erase(std::ranges::find(command, "make"), command.end());
    command.insert(command.end(), {"make", "--version"});
    BuildRunner runner;

    REQUIRE(runner.startCommand(command));
    const BuildOutcome outcome = waitForOutcome(runner);

    REQUIRE(outcome.status == BuildStatus::Succeeded);
    REQUIRE(runner.getLog().getLines().at(1).starts_with("GNU Make"));
}

#endif

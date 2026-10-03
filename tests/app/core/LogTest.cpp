#include "app/core/domain/Log.h"

#include "z64/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>
#include <vector>

using toki::core::Log;
using toki::core::LogEntry;
using toki::core::LogLevel;
using toki::core::usize;

namespace {

[[nodiscard]] std::vector<std::string> collectMessages(const Log& log) {
    std::vector<std::string> collected;
    log.visitEntries([&](usize, const LogEntry& entry) { collected.push_back(entry.message); });
    return collected;
}

} // namespace

TEST_CASE("level names round-trip", "[core][log]") {
    for (const LogLevel level :
         {LogLevel::Debug, LogLevel::Info, LogLevel::Ok, LogLevel::Warn, LogLevel::Error}) {
        REQUIRE(toki::core::parseLogLevel(toki::core::getLogLevelName(level)) == level);
    }
}

TEST_CASE("an unknown level name is rejected rather than defaulted", "[core][log]") {
    REQUIRE_FALSE(toki::core::parseLogLevel("verbose").has_value());
    REQUIRE_FALSE(toki::core::parseLogLevel("").has_value());
}

TEST_CASE("pushed entries are visited oldest first", "[core][log]") {
    Log log;
    log.push(LogLevel::Info, "first");
    log.push(LogLevel::Debug, "second");

    REQUIRE(log.countEntries() == 2);
    REQUIRE(collectMessages(log) == std::vector<std::string>{"first", "second"});
    REQUIRE(log.getEntry(1).value().level == LogLevel::Debug);
}

TEST_CASE("an index past the end has no entry", "[core][log]") {
    Log log;
    log.push(LogLevel::Info, "only");

    REQUIRE_FALSE(log.getEntry(1).has_value());
}

TEST_CASE("clearing empties the log", "[core][log]") {
    Log log;
    log.push(LogLevel::Info, "before");

    log.clear();

    REQUIRE(log.countEntries() == 0);
    REQUIRE(collectMessages(log).empty());
}

TEST_CASE("the oldest entries are evicted past the cap", "[core][log]") {
    Log log;
    constexpr usize Cap = 8;
    constexpr usize Overflow = 5;
    log.setMaxEntries(Cap);

    for (usize i = 0; i < Cap + Overflow; ++i) {
        log.push(LogLevel::Debug, std::format("entry {}", i));
    }

    REQUIRE(log.countEntries() == Cap);
    REQUIRE(log.getEntry(0).value().message == std::format("entry {}", Overflow));
    REQUIRE(log.getEntry(Cap - 1).value().message == std::format("entry {}", Cap + Overflow - 1));
}

TEST_CASE("lowering the cap evicts immediately", "[core][log]") {
    Log log;
    for (usize i = 0; i < 10; ++i) {
        log.push(LogLevel::Debug, std::format("entry {}", i));
    }

    log.setMaxEntries(3);

    REQUIRE(log.countEntries() == 3);
    REQUIRE(log.getMaxEntries() == 3);
    REQUIRE(log.getEntry(0).value().message == "entry 7");
}

TEST_CASE("a cap of zero would leave nothing to show, so it is clamped", "[core][log]") {
    Log log;
    log.setMaxEntries(0);
    log.push(LogLevel::Info, "kept");

    REQUIRE(log.getMaxEntries() == 1);
    REQUIRE(log.countEntries() == 1);
}

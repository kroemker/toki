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

[[nodiscard]] std::vector<std::string> messages(const Log& log) {
    std::vector<std::string> collected;
    log.forEach([&](usize, const LogEntry& entry) { collected.push_back(entry.message); });
    return collected;
}

} // namespace

TEST_CASE("level names round-trip", "[core][log]") {
    for (const LogLevel level :
         {LogLevel::Debug, LogLevel::Info, LogLevel::Ok, LogLevel::Warn, LogLevel::Error}) {
        REQUIRE(toki::core::logLevelFromName(toki::core::logLevelName(level)) == level);
    }
}

TEST_CASE("an unknown level name is rejected rather than defaulted", "[core][log]") {
    REQUIRE_FALSE(toki::core::logLevelFromName("verbose").has_value());
    REQUIRE_FALSE(toki::core::logLevelFromName("").has_value());
}

TEST_CASE("pushed entries are visited oldest first", "[core][log]") {
    Log log;
    log.push(LogLevel::Info, "first");
    log.push(LogLevel::Debug, "second");

    REQUIRE(log.size() == 2);
    REQUIRE(messages(log) == std::vector<std::string>{"first", "second"});
    REQUIRE(log.entry(1).value().level == LogLevel::Debug);
}

TEST_CASE("an index past the end has no entry", "[core][log]") {
    Log log;
    log.push(LogLevel::Info, "only");

    REQUIRE_FALSE(log.entry(1).has_value());
}

TEST_CASE("clearing empties the log", "[core][log]") {
    Log log;
    log.push(LogLevel::Info, "before");

    log.clear();

    REQUIRE(log.size() == 0);
    REQUIRE(messages(log).empty());
}

TEST_CASE("the oldest entries are evicted past the cap", "[core][log]") {
    Log log;
    constexpr usize Cap = 8;
    constexpr usize Overflow = 5;
    log.setMaxEntries(Cap);

    for (usize i = 0; i < Cap + Overflow; ++i) {
        log.push(LogLevel::Debug, std::format("entry {}", i));
    }

    REQUIRE(log.size() == Cap);
    REQUIRE(log.entry(0).value().message == std::format("entry {}", Overflow));
    REQUIRE(log.entry(Cap - 1).value().message == std::format("entry {}", Cap + Overflow - 1));
}

TEST_CASE("lowering the cap evicts immediately", "[core][log]") {
    Log log;
    for (usize i = 0; i < 10; ++i) {
        log.push(LogLevel::Debug, std::format("entry {}", i));
    }

    log.setMaxEntries(3);

    REQUIRE(log.size() == 3);
    REQUIRE(log.getMaxEntries() == 3);
    REQUIRE(log.entry(0).value().message == "entry 7");
}

TEST_CASE("a cap of zero would leave nothing to show, so it is clamped", "[core][log]") {
    Log log;
    log.setMaxEntries(0);
    log.push(LogLevel::Info, "kept");

    REQUIRE(log.getMaxEntries() == 1);
    REQUIRE(log.size() == 1);
}

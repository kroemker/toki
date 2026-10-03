#include "Log.h"

#include "app/core/Types.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace toki::core {

namespace {

constexpr auto LevelNames =
    std::to_array<std::string_view>({"debug", "info", "ok", "warn", "error"});

static_assert(LevelNames.size() == LogLevelCount, "a level was added to the enum without a name");

} // namespace

std::string_view getLogLevelName(const LogLevel level) {
    return LevelNames.at(static_cast<usize>(std::to_underlying(level)));
}

std::optional<LogLevel> parseLogLevel(const std::string_view name) {
    for (usize index = 0; index < LevelNames.size(); ++index) {
        if (LevelNames[index] == name) {
            return static_cast<LogLevel>(index);
        }
    }
    return std::nullopt;
}

void Log::push(const LogLevel level, std::string message) {
    if (level >= LogLevel::Warn) {
        std::println(stderr, "[{}] {}", getLogLevelName(level), message);
    }

    const std::scoped_lock lock(mutex);
    entries.push_back(LogEntry{.level = level, .message = std::move(message)});
    while (entries.size() > maxEntries) {
        entries.pop_front();
    }
}

void Log::clear() {
    const std::scoped_lock lock(mutex);
    entries.clear();
}

std::optional<LogEntry> Log::getEntry(const usize index) const {
    const std::scoped_lock lock(mutex);
    if (index >= entries.size()) {
        return std::nullopt;
    }
    return entries[index];
}

usize Log::countEntries() const {
    const std::scoped_lock lock(mutex);
    return entries.size();
}

usize Log::getMaxEntries() const {
    const std::scoped_lock lock(mutex);
    return maxEntries;
}

void Log::setMaxEntries(const usize value) {
    const std::scoped_lock lock(mutex);
    maxEntries = std::max<usize>(value, 1);
    while (entries.size() > maxEntries) {
        entries.pop_front();
    }
}

Log& log() {
    static Log instance;
    return instance;
}

} // namespace toki::core

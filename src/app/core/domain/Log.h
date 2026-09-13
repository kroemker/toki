#pragma once

#include "app/core/Types.h"

#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace toki::core {

enum class LogLevel : u8 {
    Debug,
    Info,
    Ok,
    Warn,
    Error,
};

inline constexpr usize LogLevelCount = static_cast<usize>(LogLevel::Error) + 1;

[[nodiscard]] std::string_view logLevelName(LogLevel level);

[[nodiscard]] std::optional<LogLevel> logLevelFromName(std::string_view name);

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string message;
};

/// Holds the application's log entries. Safe to push to from any thread.
///
/// Readers visit the entries in place through `forEach` and `entry`, both of
/// which take the lock for the duration of the call, so there is exactly one
/// copy of the log in the process.
class Log {
public:
    static constexpr usize DefaultMaxEntries = 10'000;

    void push(LogLevel level, std::string message);

    void clear();

    /// Calls `visit(index, entry)` for every entry, oldest first.
    template <typename Visit>
    void forEach(const Visit& visit) const {
        const std::scoped_lock lock(mutex);
        for (usize index = 0; index < entries.size(); ++index) {
            visit(index, entries[index]);
        }
    }

    /// Empty once `index` has been evicted, which a reader holding indices
    /// from an earlier call has to expect.
    [[nodiscard]] std::optional<LogEntry> entry(usize index) const;

    [[nodiscard]] usize size() const;

    [[nodiscard]] usize getMaxEntries() const;

    /// Drops the oldest entries immediately if the new cap is smaller.
    void setMaxEntries(usize value);

private:
    mutable std::mutex mutex;
    std::deque<LogEntry> entries;
    usize maxEntries = DefaultMaxEntries;
};

/// The process-wide log.
///
/// A singleton rather than a reference threaded through every constructor:
/// every layer logs, and the build runner pushes from a worker thread, so an
/// injected reference would have to reach the same places by a longer route
/// without making the lifetime any clearer.
[[nodiscard]] Log& log();

template <typename... Args>
void logDebug(std::format_string<Args...> fmt, Args&&... args) {
    log().push(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void logInfo(std::format_string<Args...> fmt, Args&&... args) {
    log().push(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void logOk(std::format_string<Args...> fmt, Args&&... args) {
    log().push(LogLevel::Ok, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void logWarn(std::format_string<Args...> fmt, Args&&... args) {
    log().push(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void logError(std::format_string<Args...> fmt, Args&&... args) {
    log().push(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace toki::core

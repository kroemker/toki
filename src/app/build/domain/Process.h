#pragma once

#include "app/build/Types.h"
#include "base/Diagnostic.h"

#include <expected>
#include <optional>
#include <span>
#include <string>

namespace toki::build {

/// A child process whose standard output and error arrive through one pipe, in
/// the order it wrote them. It runs in a job of its own, so stopping it stops
/// everything it started too. Only Windows is implemented; elsewhere `start`
/// fails saying so.
class Process {
public:
    /// Starts `arguments[0]` with the rest as its arguments, in no console
    /// window. Fails naming the command when it cannot be started.
    [[nodiscard]] static std::expected<Process, base::Diagnostic>
    start(std::span<const std::string> arguments);

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    ~Process();

    /// Waits for the next output and returns it as it came, or nothing once
    /// the process and everything it started have closed the pipe.
    [[nodiscard]] std::optional<std::string> readOutput();

    /// Waits for the process to exit and returns its exit code, or nothing if
    /// it cannot be learned.
    [[nodiscard]] std::optional<s32> wait();

    /// Ends the process and everything it started. Safe to call from another
    /// thread while one waits in `readOutput`, which then returns nothing.
    void terminate();

private:
    Process(void* process, void* job, void* output);

    void close();

    void* process = nullptr;
    void* job = nullptr;
    void* output = nullptr;
};

/// `arguments` joined into one Windows command line, quoted so that the
/// Microsoft C runtime's parser, which `wsl.exe` uses too, splits it back into
/// exactly the same arguments.
[[nodiscard]] std::string makeCommandLine(std::span<const std::string> arguments);

} // namespace toki::build

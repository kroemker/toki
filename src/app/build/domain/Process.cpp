#include "Process.h"

#include "app/build/Types.h"
#include "base/Diagnostic.h"

#include <array>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace toki::build {

namespace {

bool needsQuotes(const std::string_view argument) {
    return argument.empty() || argument.find_first_of(" \t\n\v\"") != std::string_view::npos;
}

#ifdef _WIN32

std::wstring widen(const std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<usize>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

base::Diagnostic makeSystemError(const std::string& what) {
    return base::makeError({}, std::format("{} (Windows error {})", what, GetLastError()));
}

void closeHandle(void*& handle) {
    if (handle != nullptr) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

#endif

} // namespace

std::string makeCommandLine(const std::span<const std::string> arguments) {
    std::string line;
    for (const std::string& argument : arguments) {
        if (!line.empty()) {
            line += ' ';
        }
        if (!needsQuotes(argument)) {
            line += argument;
            continue;
        }
        line += '"';
        usize backslashes = 0;
        for (const char c : argument) {
            if (c == '\\') {
                ++backslashes;
                continue;
            }
            line.append(c == '"' ? (backslashes * 2) + 1 : backslashes, '\\');
            line += c;
            backslashes = 0;
        }
        line.append(backslashes * 2, '\\');
        line += '"';
    }
    return line;
}

Process::Process(void* process, void* job, void* output)
    : process(process), job(job), output(output) {}

Process::Process(Process&& other) noexcept
    : process(std::exchange(other.process, nullptr)), job(std::exchange(other.job, nullptr)),
      output(std::exchange(other.output, nullptr)) {}

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        close();
        process = std::exchange(other.process, nullptr);
        job = std::exchange(other.job, nullptr);
        output = std::exchange(other.output, nullptr);
    }
    return *this;
}

Process::~Process() {
    close();
}

#ifdef _WIN32

std::expected<Process, base::Diagnostic>
Process::start(const std::span<const std::string> arguments) {
    const std::string commandLine = makeCommandLine(arguments);
    SECURITY_ATTRIBUTES inheritable{.nLength = sizeof(SECURITY_ATTRIBUTES),
                                    .lpSecurityDescriptor = nullptr,
                                    .bInheritHandle = TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (CreatePipe(&readEnd, &writeEnd, &inheritable, 0) == 0) {
        return std::unexpected(makeSystemError("the output pipe could not be created"));
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &inheritable, OPEN_EXISTING, 0, nullptr);

    SetEnvironmentVariableW(L"WSL_UTF8", L"1");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION information{};
    std::wstring mutableLine = widen(commandLine);
    const BOOL started = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                                        &startup, &information);
    CloseHandle(writeEnd);
    if (input != INVALID_HANDLE_VALUE) {
        CloseHandle(input);
    }
    if (started == 0) {
        const base::Diagnostic error =
            makeSystemError(std::format("`{}` could not be started", commandLine));
        CloseHandle(readEnd);
        return std::unexpected(error);
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, information.hProcess);
    }
    ResumeThread(information.hThread);
    CloseHandle(information.hThread);
    return Process(information.hProcess, job, readEnd);
}

std::optional<std::string> Process::readOutput() {
    std::array<char, 4096> buffer{};
    while (true) {
        DWORD read = 0;
        if (ReadFile(output, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) ==
            0) {
            return std::nullopt;
        }
        if (read > 0) {
            return std::string(buffer.data(), read);
        }
    }
}

std::optional<s32> Process::wait() {
    if (WaitForSingleObject(process, INFINITE) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD exitCode = 0;
    if (GetExitCodeProcess(process, &exitCode) == 0) {
        return std::nullopt;
    }
    return static_cast<s32>(exitCode);
}

void Process::terminate() {
    if (job != nullptr) {
        TerminateJobObject(job, 1);
    } else if (process != nullptr) {
        TerminateProcess(process, 1);
    }
}

void Process::close() {
    closeHandle(output);
    closeHandle(job);
    closeHandle(process);
}

#else

std::expected<Process, base::Diagnostic>
Process::start(const std::span<const std::string> arguments) {
    return std::unexpected(base::makeError(
        {}, std::format("`{}` could not be started: running processes is only implemented on "
                        "Windows so far",
                        makeCommandLine(arguments))));
}

std::optional<std::string> Process::readOutput() {
    return std::nullopt;
}

std::optional<s32> Process::wait() {
    return std::nullopt;
}

void Process::terminate() {}

void Process::close() {}

#endif

} // namespace toki::build

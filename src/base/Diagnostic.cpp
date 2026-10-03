#include "Diagnostic.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace base {

namespace {

[[nodiscard]] std::string_view spell(Severity severity) {
    switch (severity) {
    case Severity::Note:
        return "note";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    return "error";
}

[[nodiscard]] Diagnostic make(Severity severity, SourceLocation location, std::string message) {
    return Diagnostic{
        .severity = severity,
        .location = std::move(location),
        .message = std::move(message),
    };
}

} // namespace

Diagnostic makeNote(SourceLocation location, std::string message) {
    return make(Severity::Note, std::move(location), std::move(message));
}

Diagnostic makeWarning(SourceLocation location, std::string message) {
    return make(Severity::Warning, std::move(location), std::move(message));
}

Diagnostic makeError(SourceLocation location, std::string message) {
    return make(Severity::Error, std::move(location), std::move(message));
}

std::string describe(const Diagnostic& diagnostic) {
    const std::string file = diagnostic.location.file.string();
    if (diagnostic.location.line == 0) {
        return std::format("{}: {}: {}", file, spell(diagnostic.severity), diagnostic.message);
    }
    return std::format("{}:{}:{}: {}: {}", file, diagnostic.location.line,
                       diagnostic.location.column, spell(diagnostic.severity), diagnostic.message);
}

} // namespace base

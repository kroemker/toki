#include "ParamReads.h"

#include "app/actor/Types.h"
#include "app/actor/domain/OverlayCode.h"
#include "cparser/Calls.h"
#include "cparser/Parser.h"
#include "cparser/Token.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::actor {

namespace {

constexpr u8 BitCount = 16;
constexpr std::array<std::string_view, 13> Assignments{
    "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "++", "--"};

[[nodiscard]] bool isLiteralRead(const std::string_view callee) {
    return callee == "PARAMS_GET_U" || callee == "PARAMS_GET_S";
}

[[nodiscard]] std::optional<ParamsRead> readLiteralCall(const cparser::Call& call,
                                                        const OverlayCode& code) {
    if (call.arguments.size() != 3 || call.arguments[0].empty() ||
        !isIdentifier(call.arguments[0].back(), "params")) {
        return std::nullopt;
    }
    const std::optional<f64> shift = code.evaluateNumber(call.arguments[1]);
    const std::optional<f64> width = code.evaluateNumber(call.arguments[2]);
    if (!shift || !width || *width < 1.0 || *shift < 0.0 || *shift + *width > BitCount) {
        return std::nullopt;
    }
    return ParamsRead{.shift = static_cast<u8>(*shift),
                      .width = static_cast<u8>(*width),
                      .isSigned = call.callee == "PARAMS_GET_S",
                      .first = call.arguments[0].data() - 2,
                      .last = &call.arguments[2].back() + 2};
}

[[nodiscard]] std::optional<ParamsRead> readMacroCall(const cparser::Call& call,
                                                      const OverlayCode& code) {
    const std::optional<ParamsMacro> macro = code.findParamsMacro(call.callee);
    if (!macro || call.arguments.empty() || call.arguments.front().empty() ||
        call.arguments.back().empty()) {
        return std::nullopt;
    }
    if (macro->takesParams && !isIdentifier(call.arguments.front().back(), "params")) {
        return std::nullopt;
    }
    return ParamsRead{.shift = macro->shift,
                      .width = macro->width,
                      .isSigned = macro->isSigned,
                      .macro = call.callee,
                      .first = call.arguments.front().data() - 2,
                      .last = &call.arguments.back().back() + 2};
}

} // namespace

u16 makeMask(const ParamsRead& read) {
    return static_cast<u16>(((1U << read.width) - 1U) << read.shift);
}

std::string makeBitsName(const u8 shift, const u8 width) {
    return width == 1 ? std::format("Bit {}", shift)
                      : std::format("Bits {} to {}", shift, shift + width - 1);
}

bool isParamsCall(const OverlayCode& code, const std::string_view callee) {
    return isLiteralRead(callee) || code.findParamsMacro(callee).has_value();
}

std::optional<ParamsRead> readParamsCall(const cparser::Call& call, const OverlayCode& code) {
    return isLiteralRead(call.callee) ? readLiteralCall(call, code) : readMacroCall(call, code);
}

std::optional<ParamsRead> readWholeParamsCall(const OverlayCode& code,
                                              const std::span<const cparser::Token> tokens) {
    const std::vector<cparser::Call> calls = cparser::findCalls(
        tokens, [&code](const std::string_view callee) { return isParamsCall(code, callee); });
    if (calls.empty() || !isWholeCall(tokens, calls.front().callee)) {
        return std::nullopt;
    }
    return readParamsCall(calls.front(), code);
}

const cparser::Token* findAssignedTarget(const std::span<const cparser::Token> body,
                                         const cparser::Token* first) {
    if (first - body.data() < 2 || !isPunctuator(*(first - 1), "=")) {
        return nullptr;
    }
    const cparser::Token* target = first - 2;
    return target->kind == cparser::TokenKind::Identifier ? target : nullptr;
}

bool isMember(const std::span<const cparser::Token> body, const cparser::Token* target) {
    return target != body.data() &&
           (isPunctuator(*(target - 1), ".") || isPunctuator(*(target - 1), "->"));
}

bool isLocalAssignedOnce(const std::span<const cparser::Token> body, const cparser::Token* target) {
    if (isMember(body, target)) {
        return false;
    }
    usize assignments = 0;
    for (usize at = 0; at < body.size(); ++at) {
        if (!isIdentifier(body[at], target->text) || isMember(body, &body[at])) {
            continue;
        }
        const bool isWrittenAfter =
            at + 1 < body.size() &&
            std::ranges::any_of(Assignments, [&](const std::string_view op) {
                return isPunctuator(body[at + 1], op);
            });
        const bool isStepped =
            at > 0 && (isPunctuator(body[at - 1], "++") || isPunctuator(body[at - 1], "--"));
        assignments += isWrittenAfter || isStepped ? 1 : 0;
    }
    return assignments == 1;
}

std::optional<ParamsRead> findLocalRead(const OverlayCode& code,
                                        const std::span<const cparser::Token> body,
                                        const cparser::Token& local) {
    for (usize at = 0; at + 2 < body.size(); ++at) {
        if (!isIdentifier(body[at], local.text) || !isPunctuator(body[at + 1], "=") ||
            isMember(body, &body[at])) {
            continue;
        }
        if (!isLocalAssignedOnce(body, &body[at])) {
            return std::nullopt;
        }
        usize end = at + 2;
        while (end < body.size() && !isPunctuator(body[end], ";")) {
            ++end;
        }
        return readWholeParamsCall(code, body.subspan(at + 2, end - at - 2));
    }
    return std::nullopt;
}

bool isMemberChain(const std::span<const cparser::Token> tokens) {
    if (tokens.size() < 3 || tokens.size() % 2 == 0) {
        return false;
    }
    for (usize at = 0; at < tokens.size(); ++at) {
        const bool isName = at % 2 == 0;
        if (isName ? tokens[at].kind != cparser::TokenKind::Identifier
                   : !isPunctuator(tokens[at], ".") && !isPunctuator(tokens[at], "->")) {
            return false;
        }
    }
    return true;
}

bool isParamsMember(const std::span<const cparser::Token> tokens) {
    return isMemberChain(tokens) && isIdentifier(tokens.back(), "params");
}

bool isNarrowing(const std::span<const cparser::Token> body, const ParamsRead& read) {
    const cparser::Token* target = findAssignedTarget(body, read.first);
    return target != nullptr && isIdentifier(*target, "params") && isMember(body, target);
}

std::optional<ParamsRead> findNarrowing(const OverlayCode& code) {
    for (const cparser::FunctionDefinition* function : code.listFunctions()) {
        const std::vector<cparser::Call> calls =
            cparser::findCalls(function->body, [&code](const std::string_view callee) {
                return isParamsCall(code, callee);
            });
        for (const cparser::Call& call : calls) {
            const std::optional<ParamsRead> read = readParamsCall(call, code);
            if (read && isNarrowing(function->body, *read)) {
                return read;
            }
        }
    }
    return std::nullopt;
}

} // namespace toki::actor

#include "Calls.h"

#include "cparser/Initializer.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace cparser {

namespace {

[[nodiscard]] bool isMemberAccess(const std::span<const Token> body, const usize at) {
    return at > 0 && (isPunctuator(body[at - 1], ".") || isPunctuator(body[at - 1], "->"));
}

[[nodiscard]] usize findClose(const std::span<const Token> body, const usize open) {
    int nesting = 0;
    for (usize cursor = open; cursor < body.size(); ++cursor) {
        if (isPunctuator(body[cursor], "(")) {
            ++nesting;
        } else if (isPunctuator(body[cursor], ")") && --nesting == 0) {
            return cursor;
        }
    }
    return body.size();
}

} // namespace

std::vector<Call> findCalls(const std::span<const Token> body,
                            const std::function<bool(std::string_view)>& isWanted) {
    std::vector<Call> calls;
    for (usize at = 0; at + 1 < body.size(); ++at) {
        const Token& name = body[at];
        if (name.kind != TokenKind::Identifier || !isPunctuator(body[at + 1], "(") ||
            isMemberAccess(body, at) || !isWanted(name.text)) {
            continue;
        }
        const usize close = findClose(body, at + 1);
        if (close == body.size()) {
            continue;
        }
        calls.push_back(Call{
            .callee = name.text,
            .arguments = splitElements(body.subspan(at + 2, close - at - 2)),
            .location = locate(name),
        });
    }
    return calls;
}

} // namespace cparser

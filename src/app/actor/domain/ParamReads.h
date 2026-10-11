#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/OverlayCode.h"
#include "cparser/Calls.h"
#include "cparser/Token.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace toki::actor {

/// A literal read of some of an actor's params, and the tokens of a body it
/// spans, from `first` to one past its last: `PARAMS_GET_U(this->actor.params,
/// 8, 6)`, or a call of a macro that is one (`OverlayCode::findParamsMacro`),
/// `DOORSHUTTER_GET_SWITCH_FLAG(&this->actor)`, which is named.
struct ParamsRead {
    u8 shift = 0;
    u8 width = 0;
    bool isSigned = false;
    std::string_view macro;
    const cparser::Token* first = nullptr;
    const cparser::Token* last = nullptr;
};

/// The mask of the bits `read` reads.
[[nodiscard]] u16 makeMask(const ParamsRead& read);

/// "Bit 4" or "Bits 8 to 13": a param named for its bits alone.
[[nodiscard]] std::string makeBitsName(u8 shift, u8 width);

/// Whether a call to `callee` may be a read: `PARAMS_GET_U`, `PARAMS_GET_S`, or
/// a macro of `code` that is one.
[[nodiscard]] bool isParamsCall(const OverlayCode& code, std::string_view callee);

/// The read `call` is, when its bits are literals and it reads params.
[[nodiscard]] std::optional<ParamsRead> readParamsCall(const cparser::Call& call,
                                                       const OverlayCode& code);

/// The read `tokens` are, when they are one and nothing else.
[[nodiscard]] std::optional<ParamsRead> readWholeParamsCall(const OverlayCode& code,
                                                            std::span<const cparser::Token> tokens);

/// The identifier `first` is assigned to, as `type` is in `type = ...`.
[[nodiscard]] const cparser::Token* findAssignedTarget(std::span<const cparser::Token> body,
                                                       const cparser::Token* first);

/// Whether `target` is a member, `this->type`, rather than a local.
[[nodiscard]] bool isMember(std::span<const cparser::Token> body, const cparser::Token* target);

/// Whether the local `target` is assigned once in `body`.
[[nodiscard]] bool isLocalAssignedOnce(std::span<const cparser::Token> body,
                                       const cparser::Token* target);

/// The read the local `local` is assigned, when it is assigned once in `body`.
[[nodiscard]] std::optional<ParamsRead> findLocalRead(const OverlayCode& code,
                                                      std::span<const cparser::Token> body,
                                                      const cparser::Token& local);

/// Whether `tokens` are a chain of members, `this->actor.params`.
[[nodiscard]] bool isMemberChain(std::span<const cparser::Token> tokens);

/// Whether `tokens` are the whole params, `this->actor.params` or `thisx->params`.
[[nodiscard]] bool isParamsMember(std::span<const cparser::Token> tokens);

/// Whether `read` narrows the actor's params to its bits, as
/// `this->actor.params = PARAMS_GET_U(this->actor.params, 0, 8)` does.
[[nodiscard]] bool isNarrowing(std::span<const cparser::Token> body, const ParamsRead& read);

/// The read the main file's functions narrow the params to, if one does.
[[nodiscard]] std::optional<ParamsRead> findNarrowing(const OverlayCode& code);

} // namespace toki::actor

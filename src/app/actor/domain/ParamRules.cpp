#include "ParamRules.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/OverlayCode.h"
#include "app/actor/domain/ReadableName.h"
#include "app/actor/domain/ValueCollection.h"
#include "cparser/Calls.h"
#include "cparser/Parser.h"
#include "cparser/Token.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr u8 BitCount = 16;

struct KnownArgument {
    std::string_view callee;
    usize argument = 0;
    std::string_view name;
    std::string_view collection;
};

constexpr std::array<KnownArgument, 26> KnownArguments{{
    {.callee = "Flags_GetSwitch", .argument = 1, .name = "Switch Flag"},
    {.callee = "Flags_SetSwitch", .argument = 1, .name = "Switch Flag"},
    {.callee = "Flags_UnsetSwitch", .argument = 1, .name = "Switch Flag"},
    {.callee = "Flags_GetTreasure", .argument = 1, .name = "Treasure Flag"},
    {.callee = "Flags_SetTreasure", .argument = 1, .name = "Treasure Flag"},
    {.callee = "Flags_GetCollectible", .argument = 1, .name = "Collectible Flag"},
    {.callee = "Flags_SetCollectible", .argument = 1, .name = "Collectible Flag"},
    {.callee = "Flags_GetClear", .argument = 1, .name = "Clear Flag"},
    {.callee = "Flags_SetClear", .argument = 1, .name = "Clear Flag"},
    {.callee = "Item_DropCollectible", .argument = 2, .name = "Drop", .collection = "Item00Type"},
    {.callee = "Item_DropCollectible2", .argument = 2, .name = "Drop", .collection = "Item00Type"},
    {.callee = "Actor_OfferGetItem", .argument = 2, .name = "Item", .collection = "GetItemID"},
    {.callee = "Actor_OfferGetItemNearby",
     .argument = 2,
     .name = "Item",
     .collection = "GetItemID"},
    {.callee = "Actor_OfferGetItemFar", .argument = 2, .name = "Item", .collection = "GetItemID"},
    {.callee = "Actor_Spawn", .argument = 2, .name = "Actor", .collection = "ActorID"},
    {.callee = "Actor_SpawnAsChild", .argument = 3, .name = "Actor", .collection = "ActorID"},
    {.callee = "Actor_SpawnPersistent", .argument = 2, .name = "Actor", .collection = "ActorID"},
    {.callee = "Actor_PlaySfx", .argument = 1, .name = "Sound", .collection = "SfxId"},
    {.callee = "Sfx_PlaySfxCentered", .argument = 0, .name = "Sound", .collection = "SfxId"},
    {.callee = "Sfx_PlaySfxCentered2", .argument = 0, .name = "Sound", .collection = "SfxId"},
    {.callee = "Sfx_PlaySfxAtPos", .argument = 1, .name = "Sound", .collection = "SfxId"},
    {.callee = "Audio_PlaySfxGeneral", .argument = 0, .name = "Sound", .collection = "SfxId"},
    {.callee = "Audio_PlayFanfare", .argument = 0, .name = "Music", .collection = "NA_BGM"},
    {.callee = "Audio_PlaySequenceInCutscene",
     .argument = 0,
     .name = "Music",
     .collection = "NA_BGM"},
    {.callee = "Audio_PlaySequenceWithSeqPlayerIO",
     .argument = 1,
     .name = "Music",
     .collection = "NA_BGM"},
    {.callee = "SEQCMD_PLAY_SEQUENCE", .argument = 3, .name = "Music", .collection = "NA_BGM"},
}};

constexpr std::array<std::string_view, 6> VagueNames{"params", "temp", "tmp", "i", "j", "value"};

[[nodiscard]] bool isParamsRead(const std::string_view callee) {
    return callee == "PARAMS_GET_U" || callee == "PARAMS_GET_S";
}

[[nodiscard]] bool isWanted(const std::string_view callee) {
    return isParamsRead(callee) ||
           std::ranges::contains(KnownArguments, callee, &KnownArgument::callee);
}

struct ParamsRead {
    u8 shift = 0;
    u8 width = 0;
    bool isSigned = false;
    const cparser::Token* first = nullptr;
    const cparser::Token* last = nullptr;
};

[[nodiscard]] std::optional<ParamsRead> readParamsCall(const cparser::Call& call,
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
    const cparser::Token* last = &call.arguments[2].back();
    return ParamsRead{.shift = static_cast<u8>(*shift),
                      .width = static_cast<u8>(*width),
                      .isSigned = call.callee == "PARAMS_GET_S",
                      .first = call.arguments[0].data() - 2,
                      .last = last + 2};
}

struct ParamContext {
    std::span<const cparser::Token> body;
    std::span<const cparser::Call> calls;
    std::span<const ValueCollection> tables;
    const OverlayCode& code;
};

struct TableLookup {
    std::string_view table;
    const cparser::Token* first = nullptr;
    const cparser::Token* last = nullptr;
    s64 offset = 0;
};

[[nodiscard]] bool isWholeArgument(const std::span<const cparser::Token> argument,
                                   const cparser::Token* first, const cparser::Token* last) {
    if (argument.empty() || &argument.back() + 1 != last) {
        return false;
    }
    const cparser::Token* begin = argument.data();
    if (begin != first && isPunctuator(*begin, "-")) {
        ++begin;
    }
    return begin == first;
}

[[nodiscard]] const KnownArgument* findKnownArgument(const std::span<const cparser::Call> calls,
                                                     const cparser::Token* first,
                                                     const cparser::Token* last) {
    for (const cparser::Call& call : calls) {
        for (const KnownArgument& known : KnownArguments) {
            if (known.callee == call.callee && known.argument < call.arguments.size() &&
                isWholeArgument(call.arguments[known.argument], first, last)) {
                return &known;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<TableLookup>
findLookup(const ParamContext& context, const cparser::Token* first, const cparser::Token* last) {
    const cparser::Token* begin = context.body.data();
    const cparser::Token* end = begin + context.body.size();
    if (first - begin < 2 || !isPunctuator(*(first - 1), "[")) {
        return std::nullopt;
    }
    const cparser::Token& name = *(first - 2);
    if (name.kind != cparser::TokenKind::Identifier ||
        !std::ranges::contains(context.tables, name.text, &ValueCollection::name)) {
        return std::nullopt;
    }
    const cparser::Token* close = last;
    s64 offset = 0;
    if (close + 2 < end && (isPunctuator(*close, "+") || isPunctuator(*close, "-"))) {
        const std::optional<f64> amount = context.code.evaluateNumber(std::span(close + 1, 1));
        if (!amount) {
            return std::nullopt;
        }
        offset = isPunctuator(*close, "-") ? -static_cast<s64>(*amount) : static_cast<s64>(*amount);
        close += 2;
    }
    if (close >= end || !isPunctuator(*close, "]")) {
        return std::nullopt;
    }
    return TableLookup{.table = name.text, .first = &name, .last = close + 1, .offset = offset};
}

[[nodiscard]] const cparser::Token* findAssignedTarget(std::span<const cparser::Token> body,
                                                       const cparser::Token* first) {
    if (first - body.data() < 2 || !isPunctuator(*(first - 1), "=")) {
        return nullptr;
    }
    const cparser::Token* target = first - 2;
    return target->kind == cparser::TokenKind::Identifier ? target : nullptr;
}

[[nodiscard]] std::optional<std::string> findAssignedName(std::span<const cparser::Token> body,
                                                          const cparser::Token* first) {
    const cparser::Token* target = findAssignedTarget(body, first);
    if (target == nullptr || std::ranges::contains(VagueNames, target->text)) {
        return std::nullopt;
    }
    return makeReadableName(target->text);
}

[[nodiscard]] bool isLocalAssignedOnce(std::span<const cparser::Token> body,
                                       const cparser::Token* target) {
    if (target != body.data() &&
        (isPunctuator(*(target - 1), ".") || isPunctuator(*(target - 1), "->"))) {
        return false;
    }
    usize assignments = 0;
    for (usize at = 0; at + 1 < body.size(); ++at) {
        if (isIdentifier(body[at], target->text) && isPunctuator(body[at + 1], "=")) {
            ++assignments;
        }
    }
    return assignments == 1;
}

[[nodiscard]] std::optional<TableLookup> findLocalLookup(const ParamContext& context,
                                                         const cparser::Token* first) {
    const cparser::Token* target = findAssignedTarget(context.body, first);
    if (target == nullptr || !isLocalAssignedOnce(context.body, target)) {
        return std::nullopt;
    }
    std::optional<TableLookup> chosen;
    for (const cparser::Token& token : context.body) {
        if (&token == target || !isIdentifier(token, target->text)) {
            continue;
        }
        const std::optional<TableLookup> lookup = findLookup(context, &token, &token + 1);
        if (!lookup) {
            continue;
        }
        if (findKnownArgument(context.calls, lookup->first, lookup->last) != nullptr) {
            return lookup;
        }
        if (!chosen) {
            chosen = lookup;
        }
    }
    return chosen;
}

[[nodiscard]] std::string makeBitsName(const ParamsRead& read) {
    return read.width == 1 ? std::format("Bit {}", read.shift)
                           : std::format("Bits {} to {}", read.shift, read.shift + read.width - 1);
}

[[nodiscard]] u16 makeMask(const ParamsRead& read) {
    return static_cast<u16>(((1U << read.width) - 1U) << read.shift);
}

[[nodiscard]] ActorParam makeParam(const ParamContext& context, const ParamsRead& read) {
    ActorParam param{.mask = makeMask(read), .isSigned = read.isSigned};
    const std::optional<TableLookup> direct = findLookup(context, read.first, read.last);
    const std::optional<TableLookup> lookup =
        direct ? direct : findLocalLookup(context, read.first);
    const bool isThroughLocal = !direct && lookup;
    const cparser::Token* first = direct ? direct->first : read.first;
    const cparser::Token* last = direct ? direct->last : read.last;
    const KnownArgument* known = findKnownArgument(context.calls, first, last);
    if (known == nullptr && lookup) {
        known = findKnownArgument(context.calls, lookup->first, lookup->last);
    }
    const std::optional<std::string> assigned = findAssignedName(context.body, first);
    if (lookup) {
        param.kind = ParamKind::Choice;
        param.collection = std::string(lookup->table);
        param.offset = lookup->offset;
    } else if (known != nullptr && !known->collection.empty()) {
        param.kind = ParamKind::Choice;
        param.collection = std::string(known->collection);
    }
    if (assigned && (isThroughLocal || known == nullptr)) {
        param.name = *assigned;
    } else if (known != nullptr) {
        param.name = std::string(known->name);
    } else if (lookup) {
        param.name = makeReadableName(lookup->table);
    } else {
        param.name = makeBitsName(read);
        param.kind = read.width == 1 ? ParamKind::Flag : ParamKind::Number;
    }
    return param;
}

[[nodiscard]] bool isParamsMember(const std::span<const cparser::Token> index) {
    if (index.size() < 3 || !isIdentifier(index.back(), "params")) {
        return false;
    }
    for (usize at = 0; at < index.size(); ++at) {
        const bool isName = at % 2 == 0;
        if (isName ? index[at].kind != cparser::TokenKind::Identifier
                   : !isPunctuator(index[at], ".") && !isPunctuator(index[at], "->")) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool isNarrowing(std::span<const cparser::Token> body, const ParamsRead& read) {
    const cparser::Token* target = findAssignedTarget(body, read.first);
    return target != nullptr && target != body.data() && isIdentifier(*target, "params") &&
           (isPunctuator(*(target - 1), ".") || isPunctuator(*(target - 1), "->"));
}

[[nodiscard]] std::vector<ActorParam> findWholeLookups(const ParamContext& context,
                                                       const std::optional<ParamsRead>& narrowed) {
    std::vector<ActorParam> params;
    const std::span<const cparser::Token> body = context.body;
    for (usize open = 0; open < body.size(); ++open) {
        if (!isPunctuator(body[open], "[")) {
            continue;
        }
        usize close = open + 1;
        while (close < body.size() && !isPunctuator(body[close], "]") &&
               !isPunctuator(body[close], "[") && !isPunctuator(body[close], ";")) {
            ++close;
        }
        const std::span<const cparser::Token> index = body.subspan(open + 1, close - open - 1);
        if (close >= body.size() || !isParamsMember(index)) {
            continue;
        }
        const ParamsRead read{.shift = narrowed ? narrowed->shift : u8{0},
                              .width = narrowed ? narrowed->width : BitCount,
                              .isSigned = narrowed && narrowed->isSigned,
                              .first = index.data(),
                              .last = &index.back() + 1};
        if (findLookup(context, read.first, read.last)) {
            params.push_back(makeParam(context, read));
        }
    }
    return params;
}

[[nodiscard]] std::optional<ParamsRead>
readWholeParamsCall(const OverlayCode& code, const std::span<const cparser::Token> tokens) {
    const std::vector<cparser::Call> calls = cparser::findCalls(tokens, isParamsRead);
    if (calls.empty() || !isWholeCall(tokens, calls.front().callee)) {
        return std::nullopt;
    }
    return readParamsCall(calls.front(), code);
}

[[nodiscard]] std::optional<ParamsRead> findNarrowing(const OverlayCode& code) {
    for (const cparser::FunctionDefinition* function : code.listFunctions()) {
        for (const cparser::Call& call : cparser::findCalls(function->body, isParamsRead)) {
            const std::optional<ParamsRead> read = readParamsCall(call, code);
            if (read && isNarrowing(function->body, *read)) {
                return read;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ParamsRead> findLocalRead(const OverlayCode& code,
                                                      const std::span<const cparser::Token> body,
                                                      const cparser::Token& local) {
    for (usize at = 0; at + 2 < body.size(); ++at) {
        if (!isIdentifier(body[at], local.text) || !isPunctuator(body[at + 1], "=")) {
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

[[nodiscard]] IndexRead makeIndexRead(const ParamsRead& read, const s64 offset) {
    return IndexRead{.mask = makeMask(read), .isSigned = read.isSigned, .offset = offset};
}

[[nodiscard]] bool isVague(const ActorParam& param) {
    return param.name == makeBitsName(ParamsRead{.shift = findMaskShift(param.mask),
                                                 .width = findMaskWidth(param.mask)});
}

void addParam(std::vector<ActorParam>& params, ActorParam param) {
    const auto same = std::ranges::find(params, param.mask, &ActorParam::mask);
    if (same != params.end()) {
        if (isVague(*same) && !isVague(param)) {
            *same = std::move(param);
        } else if (!same->collection && param.collection) {
            same->kind = ParamKind::Choice;
            same->collection = std::move(param.collection);
            same->offset = param.offset;
        }
        return;
    }
    const bool isOverlapping = std::ranges::any_of(
        params, [&param](const ActorParam& known) { return (known.mask & param.mask) != 0; });
    if (isOverlapping) {
        return;
    }
    if (std::ranges::contains(params, param.name, &ActorParam::name)) {
        param.name = std::format("{} ({})", param.name,
                                 makeBitsName(ParamsRead{.shift = findMaskShift(param.mask),
                                                         .width = findMaskWidth(param.mask)}));
    }
    params.push_back(std::move(param));
}

} // namespace

std::vector<ActorParam> findActorParams(const OverlayCode& code) {
    const std::vector<ValueCollection> tables = listTableCollections(code);
    const std::vector<const cparser::FunctionDefinition*> functions = code.listFunctions();
    std::vector<std::vector<cparser::Call>> calls;
    calls.reserve(functions.size());
    std::vector<ActorParam> params;
    std::optional<ParamsRead> narrowed;
    for (const cparser::FunctionDefinition* function : functions) {
        calls.push_back(cparser::findCalls(function->body, isWanted));
        const ParamContext context{
            .body = function->body, .calls = calls.back(), .tables = tables, .code = code};
        for (const cparser::Call& call : calls.back()) {
            if (!isParamsRead(call.callee)) {
                continue;
            }
            if (const std::optional<ParamsRead> read = readParamsCall(call, code)) {
                addParam(params, makeParam(context, *read));
                if (isNarrowing(function->body, *read)) {
                    narrowed = read;
                }
            }
        }
    }
    for (usize i = 0; i < functions.size(); ++i) {
        const ParamContext context{
            .body = functions[i]->body, .calls = calls[i], .tables = tables, .code = code};
        for (ActorParam& param : findWholeLookups(context, narrowed)) {
            addParam(params, std::move(param));
        }
    }
    std::ranges::stable_sort(params, {},
                             [](const ActorParam& param) { return findMaskShift(param.mask); });
    return params;
}

std::optional<IndexRead> findIndexRead(const OverlayCode& code,
                                       const std::span<const cparser::Token> body,
                                       std::span<const cparser::Token> index) {
    s64 offset = 0;
    if (index.size() >= 3 && index.back().kind == cparser::TokenKind::Integer &&
        (isPunctuator(index[index.size() - 2], "+") ||
         isPunctuator(index[index.size() - 2], "-"))) {
        const std::optional<f64> amount = code.evaluateNumber(index.last(1));
        if (!amount) {
            return std::nullopt;
        }
        offset = isPunctuator(index[index.size() - 2], "-") ? -static_cast<s64>(*amount)
                                                            : static_cast<s64>(*amount);
        index = index.first(index.size() - 2);
    }
    if (const std::optional<ParamsRead> read = readWholeParamsCall(code, index)) {
        return makeIndexRead(*read, offset);
    }
    if (isParamsMember(index)) {
        const std::optional<ParamsRead> narrowed = findNarrowing(code);
        return narrowed ? makeIndexRead(*narrowed, offset)
                        : IndexRead{.mask = 0xFFFF, .offset = offset};
    }
    if (index.size() == 1 && index.front().kind == cparser::TokenKind::Identifier) {
        if (const std::optional<ParamsRead> read = findLocalRead(code, body, index.front())) {
            return makeIndexRead(*read, offset);
        }
    }
    return std::nullopt;
}

} // namespace toki::actor

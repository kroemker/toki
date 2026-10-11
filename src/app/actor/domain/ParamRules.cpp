#include "ParamRules.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/OverlayCode.h"
#include "app/actor/domain/ParamReads.h"
#include "app/actor/domain/ReadableName.h"
#include "app/actor/domain/ValueCollection.h"
#include "cparser/Calls.h"
#include "cparser/Parser.h"
#include "cparser/Token.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <map>
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
    std::optional<SceneFlag> flag;
    FlagUse use = FlagUse::Reads;
};

[[nodiscard]] constexpr KnownArgument makeFlagArgument(const std::string_view callee,
                                                       const std::string_view name,
                                                       const SceneFlag flag, const FlagUse use) {
    return KnownArgument{.callee = callee, .argument = 1, .name = name, .flag = flag, .use = use};
}

constexpr std::array<KnownArgument, 26> KnownArguments{{
    makeFlagArgument("Flags_GetSwitch", "Switch Flag", SceneFlag::Switch, FlagUse::Reads),
    makeFlagArgument("Flags_SetSwitch", "Switch Flag", SceneFlag::Switch, FlagUse::Sets),
    makeFlagArgument("Flags_UnsetSwitch", "Switch Flag", SceneFlag::Switch, FlagUse::Sets),
    makeFlagArgument("Flags_GetTreasure", "Treasure Flag", SceneFlag::Treasure, FlagUse::Reads),
    makeFlagArgument("Flags_SetTreasure", "Treasure Flag", SceneFlag::Treasure, FlagUse::Sets),
    makeFlagArgument("Flags_GetCollectible", "Collectible Flag", SceneFlag::Collectible,
                     FlagUse::Reads),
    makeFlagArgument("Flags_SetCollectible", "Collectible Flag", SceneFlag::Collectible,
                     FlagUse::Sets),
    makeFlagArgument("Flags_GetClear", "Clear Flag", SceneFlag::Clear, FlagUse::Reads),
    makeFlagArgument("Flags_SetClear", "Clear Flag", SceneFlag::Clear, FlagUse::Sets),
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

[[nodiscard]] bool isKnownCallee(const std::string_view callee) {
    return std::ranges::contains(KnownArguments, callee, &KnownArgument::callee);
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

[[nodiscard]] std::optional<std::string> findAssignedName(std::span<const cparser::Token> body,
                                                          const cparser::Token* first) {
    const cparser::Token* target = findAssignedTarget(body, first);
    if (target == nullptr || std::ranges::contains(VagueNames, target->text)) {
        return std::nullopt;
    }
    return makeReadableName(target->text);
}

[[nodiscard]] std::string makeMacroName(const std::string_view macro) {
    const usize get = macro.find("_GET_");
    const usize underscore = macro.find('_');
    std::string_view rest = macro;
    if (get != std::string_view::npos) {
        rest = macro.substr(get + 5);
    } else if (underscore != std::string_view::npos && underscore + 1 < macro.size()) {
        rest = macro.substr(underscore + 1);
    }
    std::string lowered(rest);
    std::ranges::transform(lowered, lowered.begin(), [](const char letter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    });
    return makeReadableName(lowered);
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
    } else if (read.width == 1) {
        param.kind = ParamKind::Flag;
    }
    if (assigned && isThroughLocal) {
        param.name = *assigned;
    } else if (known != nullptr) {
        param.name = std::string(known->name);
    } else if (!read.macro.empty()) {
        param.name = makeMacroName(read.macro);
    } else if (assigned) {
        param.name = *assigned;
    } else if (lookup) {
        param.name = makeReadableName(lookup->table);
    } else {
        param.name = makeBitsName(read.shift, read.width);
    }
    return param;
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

[[nodiscard]] IndexRead makeIndexRead(const ParamsRead& read, const s64 offset) {
    return IndexRead{.mask = makeMask(read), .isSigned = read.isSigned, .offset = offset};
}

[[nodiscard]] std::string makeMaskName(const u16 mask) {
    return makeBitsName(findMaskShift(mask), findMaskWidth(mask));
}

[[nodiscard]] bool isVague(const ActorParam& param) {
    return param.name == makeMaskName(param.mask);
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
        param.name = std::format("{} ({})", param.name, makeMaskName(param.mask));
    }
    params.push_back(std::move(param));
}

/// What the params read into the members a file assigns once, by member name.
using MemberReads = std::map<std::string_view, ParamsRead>;

[[nodiscard]] MemberReads
findMemberReads(const std::span<const cparser::FunctionDefinition* const> functions,
                const std::span<const std::vector<cparser::Call>> calls, const OverlayCode& code) {
    std::map<std::string_view, usize> assignments;
    for (const cparser::FunctionDefinition* function : functions) {
        const std::span<const cparser::Token> body = function->body;
        for (usize at = 0; at + 1 < body.size(); ++at) {
            if (body[at].kind == cparser::TokenKind::Identifier && isMember(body, &body[at]) &&
                isPunctuator(body[at + 1], "=")) {
                ++assignments[body[at].text];
            }
        }
    }
    MemberReads reads;
    for (usize i = 0; i < functions.size(); ++i) {
        for (const cparser::Call& call : calls[i]) {
            const std::optional<ParamsRead> read =
                isParamsCall(code, call.callee) ? readParamsCall(call, code) : std::nullopt;
            if (!read) {
                continue;
            }
            const cparser::Token* target = findAssignedTarget(functions[i]->body, read->first);
            if (target != nullptr && isMember(functions[i]->body, target) &&
                target->text != "params" && assignments[target->text] == 1) {
                reads.emplace(target->text, *read);
            }
        }
    }
    return reads;
}

struct ArgumentContext {
    std::span<const cparser::Token> body;
    const OverlayCode& code;
    const MemberReads& members;
    const std::optional<ParamsRead>& narrowed;
};

[[nodiscard]] std::optional<ParamsRead> resolveArgument(const ArgumentContext& context,
                                                        std::span<const cparser::Token> argument) {
    argument = stripParentheses(argument);
    if (std::optional<ParamsRead> read = readWholeParamsCall(context.code, argument)) {
        return read;
    }
    if (isParamsMember(argument)) {
        return context.narrowed ? context.narrowed : std::optional(ParamsRead{.width = BitCount});
    }
    if (isMemberChain(argument)) {
        const auto found = context.members.find(argument.back().text);
        return found == context.members.end() ? std::nullopt : std::optional(found->second);
    }
    if (argument.size() == 1 && argument.front().kind == cparser::TokenKind::Identifier) {
        return findLocalRead(context.code, context.body, argument.front());
    }
    return std::nullopt;
}

[[nodiscard]] FlagUse mergeUse(const FlagUse first, const FlagUse second) {
    return first == second ? first : FlagUse::Both;
}

void applyKnownArgument(ActorParam& param, const KnownArgument& known,
                        const std::vector<ActorParam>& params) {
    if (isVague(param) && !std::ranges::contains(params, known.name, &ActorParam::name)) {
        param.name = std::string(known.name);
    }
    if (known.flag) {
        if (!param.sceneFlag) {
            param.kind = ParamKind::Number;
            param.sceneFlag = known.flag;
            param.flagUse = known.use;
        } else if (param.sceneFlag == known.flag) {
            param.flagUse = mergeUse(param.flagUse, known.use);
        }
    } else if (!known.collection.empty() && !param.collection && param.kind != ParamKind::Choice) {
        param.kind = ParamKind::Choice;
        param.collection = std::string(known.collection);
    }
}

void linkKnownArguments(std::vector<ActorParam>& params,
                        const std::span<const cparser::FunctionDefinition* const> functions,
                        const std::span<const std::vector<cparser::Call>> calls,
                        const OverlayCode& code, const std::optional<ParamsRead>& narrowed) {
    const MemberReads members = findMemberReads(functions, calls, code);
    for (usize i = 0; i < functions.size(); ++i) {
        const ArgumentContext context{
            .body = functions[i]->body, .code = code, .members = members, .narrowed = narrowed};
        for (const cparser::Call& call : calls[i]) {
            for (const KnownArgument& known : KnownArguments) {
                if (known.callee != call.callee || known.argument >= call.arguments.size()) {
                    continue;
                }
                const std::optional<ParamsRead> read =
                    resolveArgument(context, call.arguments[known.argument]);
                if (!read) {
                    continue;
                }
                const u16 mask = makeMask(*read);
                if (!std::ranges::contains(params, mask, &ActorParam::mask)) {
                    addParam(params, ActorParam{.name = std::string(known.name),
                                                .mask = mask,
                                                .isSigned = read->isSigned});
                }
                const auto param = std::ranges::find(params, mask, &ActorParam::mask);
                if (param != params.end()) {
                    applyKnownArgument(*param, known, params);
                }
            }
        }
    }
}

} // namespace

std::vector<ActorParam> findActorParams(const OverlayCode& code) {
    const std::vector<ValueCollection> tables = listTableCollections(code);
    const std::vector<const cparser::FunctionDefinition*> functions = code.listFunctions();
    const auto isWanted = [&code](const std::string_view callee) {
        return isKnownCallee(callee) || isParamsCall(code, callee);
    };
    std::vector<std::vector<cparser::Call>> calls;
    calls.reserve(functions.size());
    std::vector<ActorParam> params;
    std::optional<ParamsRead> narrowed;
    for (const cparser::FunctionDefinition* function : functions) {
        calls.push_back(cparser::findCalls(function->body, isWanted));
        const ParamContext context{
            .body = function->body, .calls = calls.back(), .tables = tables, .code = code};
        for (const cparser::Call& call : calls.back()) {
            if (isKnownCallee(call.callee) || !isParamsCall(code, call.callee)) {
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
    linkKnownArguments(params, functions, calls, code, narrowed);
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

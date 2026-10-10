#include "ParamRules.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/OverlayCode.h"
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
                      .last = last + 1};
}

[[nodiscard]] bool isWholeArgument(const std::span<const cparser::Token> argument,
                                   const ParamsRead& read) {
    if (argument.empty() || &argument.back() != read.last) {
        return false;
    }
    const cparser::Token* first = argument.data();
    if (first != read.first && isPunctuator(*first, "-")) {
        ++first;
    }
    return first == read.first;
}

[[nodiscard]] std::string makeWords(const std::string_view identifier) {
    std::string words;
    for (usize i = 0; i < identifier.size(); ++i) {
        const char letter = identifier[i];
        if (letter == '_') {
            words += ' ';
            continue;
        }
        const bool startsWord = i == 0 || identifier[i - 1] == '_' ||
                                (std::isupper(static_cast<unsigned char>(letter)) != 0 &&
                                 std::islower(static_cast<unsigned char>(identifier[i - 1])) != 0);
        if (startsWord && i > 0 && identifier[i - 1] != '_') {
            words += ' ';
        }
        words += startsWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(letter)))
                            : letter;
    }
    return words;
}

[[nodiscard]] std::optional<std::string> findAssignedName(std::span<const cparser::Token> body,
                                                          const ParamsRead& read) {
    if (read.first - body.data() < 2 || !isPunctuator(*(read.first - 1), "=")) {
        return std::nullopt;
    }
    const cparser::Token& target = *(read.first - 2);
    if (target.kind != cparser::TokenKind::Identifier ||
        std::ranges::contains(VagueNames, target.text)) {
        return std::nullopt;
    }
    return makeWords(target.text);
}

[[nodiscard]] std::string makeBitsName(const ParamsRead& read) {
    return read.width == 1 ? std::format("Bit {}", read.shift)
                           : std::format("Bits {} to {}", read.shift, read.shift + read.width - 1);
}

[[nodiscard]] u16 makeMask(const ParamsRead& read) {
    return static_cast<u16>(((1U << read.width) - 1U) << read.shift);
}

[[nodiscard]] ActorParam makeParam(std::span<const cparser::Token> body,
                                   std::span<const cparser::Call> calls, const ParamsRead& read) {
    ActorParam param{.mask = makeMask(read), .isSigned = read.isSigned};
    for (const cparser::Call& call : calls) {
        for (const KnownArgument& known : KnownArguments) {
            if (known.callee != call.callee || known.argument >= call.arguments.size() ||
                !isWholeArgument(call.arguments[known.argument], read)) {
                continue;
            }
            param.name = std::string(known.name);
            if (!known.collection.empty()) {
                param.kind = ParamKind::Choice;
                param.collection = std::string(known.collection);
            }
            return param;
        }
    }
    if (std::optional<std::string> assigned = findAssignedName(body, read)) {
        param.name = std::move(*assigned);
    } else {
        param.name = makeBitsName(read);
        param.kind = read.width == 1 ? ParamKind::Flag : ParamKind::Number;
    }
    return param;
}

void addParam(std::vector<ActorParam>& params, ActorParam param) {
    const auto same = std::ranges::find(params, param.mask, &ActorParam::mask);
    if (same != params.end()) {
        const std::string vague = makeBitsName(
            ParamsRead{.shift = findMaskShift(same->mask), .width = findMaskWidth(same->mask)});
        if (same->name == vague && param.name != vague) {
            *same = std::move(param);
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
    std::vector<ActorParam> params;
    for (const cparser::FunctionDefinition* function : code.listFunctions()) {
        const std::vector<cparser::Call> calls = cparser::findCalls(function->body, isWanted);
        for (const cparser::Call& call : calls) {
            if (!isParamsRead(call.callee)) {
                continue;
            }
            if (const std::optional<ParamsRead> read = readParamsCall(call, code)) {
                addParam(params, makeParam(function->body, calls, *read));
            }
        }
    }
    std::ranges::stable_sort(params, {},
                             [](const ActorParam& param) { return findMaskShift(param.mask); });
    return params;
}

} // namespace toki::actor

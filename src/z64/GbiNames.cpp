#include "GbiNames.h"

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Token.h"
#include "cparser/Value.h"
#include "n64/CombineMode.h"
#include "n64/CommandDecoder.h"
#include "z64/DecompDialect.h"
#include "z64/Types.h"

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

struct ModeFamily {
    bool isHigh;
    u8 shift;
    std::string_view prefix;
};

constexpr std::array<ModeFamily, 13> ModeFamilies{{
    {true, 4, "G_AD_"},
    {true, 6, "G_CD_"},
    {true, 8, "G_CK_"},
    {true, 9, "G_TC_"},
    {true, 12, "G_TF_"},
    {true, 14, "G_TT_"},
    {true, 16, "G_TL_"},
    {true, 17, "G_TD_"},
    {true, 19, "G_TP_"},
    {true, 20, "G_CYC_"},
    {true, 23, "G_PM_"},
    {false, 0, "G_AC_"},
    {false, 2, "G_ZS_"},
}};

constexpr std::array<std::string_view, 8> PlainFamilies{"G_IM_FMT_", "G_IM_SIZ_", "G_MW_", "G_MV_",
                                                        "G_RM_",     "G_MTX_",    "G_TX_", "G_CC_"};

constexpr std::array<std::string_view, 11> GeometryNames{
    "G_ZBUFFER",  "G_SHADE",       "G_CULL_FRONT",         "G_CULL_BACK",      "G_FOG",
    "G_LIGHTING", "G_TEXTURE_GEN", "G_TEXTURE_GEN_LINEAR", "G_SHADING_SMOOTH", "G_CLIPPING",
    "G_LOD"};

struct BitNames {
    std::string_view on;
    std::string_view off;
};

constexpr std::array<BitNames, 3> MatrixNames{{
    {"G_MTX_PROJECTION", "G_MTX_MODELVIEW"},
    {"G_MTX_LOAD", "G_MTX_MUL"},
    {"G_MTX_PUSH", "G_MTX_NOPUSH"},
}};

constexpr std::array<BitNames, 2> WrapNames{{
    {"G_TX_MIRROR", "G_TX_NOMIRROR"},
    {"G_TX_CLAMP", "G_TX_WRAP"},
}};

[[nodiscard]] std::optional<u32> evaluateMacro(const cparser::MacroTable& gbi,
                                               const cparser::Macro& macro) {
    if (macro.functionLike || macro.body.empty()) {
        return std::nullopt;
    }
    std::vector<base::Diagnostic> diagnostics;
    const std::expected<cparser::Value, base::Diagnostic> value = cparser::evaluate(
        macro.body,
        cparser::EvalContext{
            .macros = &gbi, .dialect = &getDecompDialect(), .unknownIsZero = false},
        diagnostics);
    const bool isKnown = std::ranges::none_of(diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.severity != base::Severity::Note;
    });
    if (!value || !isKnown || cparser::isReal(*value)) {
        return std::nullopt;
    }
    return static_cast<u32>(cparser::getInteger(*value));
}

[[nodiscard]] std::vector<std::string> splitPreset(const cparser::Macro& macro) {
    std::vector<std::string> inputs;
    std::string current;
    for (const cparser::Token& token : macro.body) {
        if (cparser::isPunctuator(token, ",")) {
            inputs.push_back(std::move(current));
            current.clear();
        } else {
            current += token.text;
        }
    }
    inputs.push_back(std::move(current));
    return inputs;
}

[[nodiscard]] std::string makeFixedText(const u32 value) {
    return std::format("{:g}", static_cast<f64>(value) / 4.0);
}

} // namespace

GbiNames::GbiNames(const cparser::MacroTable& gbi) {
    for (const std::string_view name : gbi.getDefinedNames()) {
        if (!name.starts_with("G_")) {
            continue;
        }
        const cparser::Macro* macro = gbi.find(name);
        if (macro == nullptr) {
            continue;
        }
        if (name.starts_with("G_CC_")) {
            const cparser::Macro* preset = macro;
            if (preset->body.size() == 1 && preset->body.front().text.starts_with("G_CC_")) {
                preset = gbi.find(preset->body.front().text);
            }
            std::vector<std::string> inputs =
                preset != nullptr ? splitPreset(*preset) : std::vector<std::string>{};
            if (inputs.size() == 8) {
                combinePresets.emplace_back(std::string(name), std::move(inputs));
            }
            continue;
        }
        if (const std::optional<u32> value = evaluateMacro(gbi, *macro)) {
            values.emplace(std::string(name), *value);
        }
    }
    std::ranges::sort(combinePresets, {}, [](const auto& preset) { return preset.first.size(); });
    for (const std::string_view prefix : PlainFamilies) {
        readFamily(prefix);
    }
    for (const ModeFamily& family : ModeFamilies) {
        readFamily(family.prefix);
    }
}

void GbiNames::readFamily(const std::string_view prefix) {
    std::vector<Named>& family = families[std::string(prefix)];
    for (const auto& [name, value] : values) {
        if (name.starts_with(prefix)) {
            family.push_back(Named{.name = name, .value = value});
        }
    }
    std::ranges::stable_sort(family, {}, [](const Named& named) { return named.name.size(); });
}

std::optional<std::string_view> GbiNames::findName(const std::string_view family,
                                                   const u32 value) const {
    const auto found = families.find(family);
    if (found == families.end()) {
        return std::nullopt;
    }
    const auto named = std::ranges::find(found->second, value, &Named::value);
    return named == found->second.end() ? std::nullopt
                                        : std::optional<std::string_view>(named->name);
}

std::string GbiNames::makeBitsText(const std::vector<std::string_view>& names, u32 value) const {
    std::string text;
    for (const std::string_view name : names) {
        const auto bit = values.find(name);
        if (bit == values.end() || bit->second == 0 || (value & bit->second) != bit->second) {
            continue;
        }
        text += std::format("{}{}", text.empty() ? "" : " | ", name);
        value &= ~bit->second;
    }
    if (value != 0) {
        text += std::format("{}0x{:X}", text.empty() ? "" : " | ", value);
    }
    return text.empty() ? std::string("0") : text;
}

std::string GbiNames::makeRenderModeText(const u32 value) const {
    const auto found = families.find(std::string_view("G_RM_"));
    if (found == families.end()) {
        return std::format("0x{:08X}", value);
    }
    const std::vector<Named>& modes = found->second;
    const auto findPair = [&](const bool isMatched) -> std::optional<std::string> {
        for (const Named& first : modes) {
            if (first.name.ends_with('2')) {
                continue;
            }
            for (const Named& second : modes) {
                const bool isPair = second.name == first.name + "2";
                if (second.name.ends_with('2') && (!isMatched || isPair) &&
                    (first.value | second.value) == value) {
                    return std::format("{}, {}", first.name, second.name);
                }
            }
        }
        return std::nullopt;
    };
    if (std::optional<std::string> matched = findPair(true)) {
        return std::move(*matched);
    }
    return findPair(false).value_or(std::format("0x{:08X}", value));
}

std::string GbiNames::makeOtherModeText(const n64::CommandField& field) const {
    const bool isHigh = field.kind == n64::FieldKind::OtherModeHigh;
    if (!isHigh && field.shift == 3) {
        return makeRenderModeText(field.value);
    }
    for (const ModeFamily& family : ModeFamilies) {
        if (family.isHigh == isHigh && family.shift == field.shift) {
            if (const std::optional<std::string_view> name = findName(family.prefix, field.value)) {
                return std::string(*name);
            }
        }
    }
    return std::format("0x{:08X}", field.value);
}

std::optional<std::string_view>
GbiNames::findCombinePreset(const std::vector<std::string_view>& inputs) const {
    for (const auto& [name, preset] : combinePresets) {
        if (std::ranges::equal(preset, inputs)) {
            return name;
        }
    }
    return std::nullopt;
}

std::string GbiNames::makeCombineText(const n64::DecodedCommand& command) const {
    std::array<std::vector<std::string_view>, 2> cycles;
    for (const n64::CommandField& field : command.fields) {
        cycles[field.shift & 1U].push_back(n64::getCombinerInputName(
            static_cast<n64::CombinerInput>(field.value), field.name.starts_with("alpha")));
    }
    std::string text;
    for (const std::vector<std::string_view>& cycle : cycles) {
        std::string inputs;
        if (const std::optional<std::string_view> preset = findCombinePreset(cycle)) {
            inputs = *preset;
        } else {
            for (const std::string_view input : cycle) {
                inputs += std::format("{}{}", inputs.empty() ? "" : ", ", input);
            }
        }
        text += std::format("{}{}", text.empty() ? "" : ", ", inputs);
    }
    return std::format("{}({})", command.name, text);
}

std::string GbiNames::makeValueText(const n64::CommandField& field,
                                    const FindAddressName& findAddressName) const {
    switch (field.kind) {
    case n64::FieldKind::Count:
        return std::to_string(field.value);
    case n64::FieldKind::Index:
        if (field.name == "tile") {
            if (const std::optional<std::string_view> name =
                    field.value == 7   ? std::optional<std::string_view>("G_TX_LOADTILE")
                    : field.value == 0 ? std::optional<std::string_view>("G_TX_RENDERTILE")
                                       : std::nullopt;
                name && values.contains(*name)) {
                return std::string(*name);
            }
        }
        return std::to_string(field.value);
    case n64::FieldKind::Hex:
    case n64::FieldKind::Fraction:
        return std::format("0x{:X}", field.value);
    case n64::FieldKind::Address:
        if (findAddressName) {
            if (std::optional<std::string> name = findAddressName(field.value)) {
                return std::move(*name);
            }
        }
        return std::format("0x{:08X}", field.value);
    case n64::FieldKind::FixedPoint:
        return makeFixedText(field.value);
    case n64::FieldKind::Color:
        return std::format("{}, {}, {}, {}", field.value >> 24, (field.value >> 16) & 0xFF,
                           (field.value >> 8) & 0xFF, field.value & 0xFF);
    case n64::FieldKind::GeometryMode:
        return makeBitsText({GeometryNames.begin(), GeometryNames.end()}, field.value);
    case n64::FieldKind::OtherModeHigh:
    case n64::FieldKind::OtherModeLow:
        return makeOtherModeText(field);
    case n64::FieldKind::MatrixFlags:
    case n64::FieldKind::TileWrap: {
        const std::span<const BitNames> names = field.kind == n64::FieldKind::MatrixFlags
                                                    ? std::span<const BitNames>(MatrixNames)
                                                    : std::span<const BitNames>(WrapNames);
        std::string text;
        for (const BitNames& bit : names) {
            const auto on = values.find(bit.on);
            const bool isOn = on != values.end() && (field.value & on->second) != 0;
            text += std::format("{}{}", text.empty() ? "" : " | ", isOn ? bit.on : bit.off);
        }
        return text;
    }
    case n64::FieldKind::MoveWordIndex:
    case n64::FieldKind::MoveMemoryIndex:
    case n64::FieldKind::ImageFormat:
    case n64::FieldKind::TexelSize: {
        const std::string_view family = field.kind == n64::FieldKind::MoveWordIndex     ? "G_MW_"
                                        : field.kind == n64::FieldKind::MoveMemoryIndex ? "G_MV_"
                                        : field.kind == n64::FieldKind::ImageFormat ? "G_IM_FMT_"
                                                                                    : "G_IM_SIZ_";
        const std::optional<std::string_view> name = findName(family, field.value);
        return name ? std::string(*name) : std::to_string(field.value);
    }
    case n64::FieldKind::CombinerInput:
        return std::string(n64::getCombinerInputName(static_cast<n64::CombinerInput>(field.value),
                                                     field.name.starts_with("alpha")));
    case n64::FieldKind::Flag:
        return field.value != 0 ? "G_ON" : "G_OFF";
    }
    return std::to_string(field.value);
}

std::string GbiNames::makeCommandText(const n64::DecodedCommand& command,
                                      const FindAddressName& findAddressName) const {
    if (command.name == "gsDPSetCombineMode") {
        return makeCombineText(command);
    }
    std::string arguments;
    for (const n64::CommandField& field : command.fields) {
        if (field.name == "bits") {
            continue;
        }
        arguments += std::format("{}{}", arguments.empty() ? "" : ", ",
                                 makeValueText(field, findAddressName));
    }
    return std::format("{}({})", command.name, arguments);
}

} // namespace z64

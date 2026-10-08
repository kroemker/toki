#include "DatabaseReader.h"
#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"

#include <toml++/toml.hpp>

#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array StepKinds = {std::string_view("setup"),        std::string_view("colors"),
                                  std::string_view("segment"),      std::string_view("transform"),
                                  std::string_view("display_list"), std::string_view("skeleton"),
                                  std::string_view("shadow")};
constexpr std::array StepKeys = {std::string_view("setup"),
                                 std::string_view("colors"),
                                 std::string_view("segment"),
                                 std::string_view("symbol"),
                                 std::string_view("object"),
                                 std::string_view("transform"),
                                 std::string_view("display_list"),
                                 std::string_view("pass"),
                                 std::string_view("skeleton"),
                                 std::string_view("animation"),
                                 std::string_view("animation_object"),
                                 std::string_view("frame"),
                                 std::string_view("limbs"),
                                 std::string_view("shadow")};
constexpr std::array TransformKeys = {std::string_view("translation"), std::string_view("rotation"),
                                      std::string_view("scale"), std::string_view("billboard"),
                                      std::string_view("reset")};
constexpr s64 LastSetup = 70;
constexpr s64 FirstSegment = 8;
constexpr s64 LastSegment = 12;

[[nodiscard]] std::optional<u8> parseLimb(const std::string_view key) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), value);
    if (error != std::errc() || end != key.data() + key.size() || value > 0xFF) {
        return std::nullopt;
    }
    return static_cast<u8>(value);
}

} // namespace

std::vector<DrawStep> DatabaseReader::readSteps(const toml::array& steps,
                                                const std::string_view path, const bool isInLimb) {
    std::vector<DrawStep> read;
    for (usize i = 0; i < steps.size(); ++i) {
        const std::string at = std::format("{}[{}]", path, i);
        const toml::table* table = steps[i].as_table();
        if (table == nullptr) {
            report(steps[i], at, "must be a table");
            continue;
        }
        if (std::optional<DrawStep> step = readStep(*table, at, isInLimb)) {
            read.push_back(std::move(*step));
        }
    }
    return read;
}

std::optional<DrawStep> DatabaseReader::readStep(const toml::table& table,
                                                 const std::string_view path, const bool isInLimb) {
    checkKeys(table, path, StepKeys);
    std::vector<std::string_view> kinds;
    for (const std::string_view kind : StepKinds) {
        if (table.contains(kind)) {
            kinds.push_back(kind);
        }
    }
    if (kinds.size() != 1) {
        report(table, path,
               "must be exactly one of setup, colors, segment, transform, display_list, skeleton "
               "or shadow");
        return std::nullopt;
    }
    const base::SourceLocation location{.file = file,
                                        .line = static_cast<u32>(table.source().begin.line)};
    const std::string_view kind = kinds.front();
    if (isInLimb && (kind == "skeleton" || kind == "shadow")) {
        report(table, path, std::format("a limb's steps cannot draw a {}", kind));
        return std::nullopt;
    }
    if (kind == "setup") {
        const s64 index = readInteger(table, "setup", path).value_or(-1);
        if (index < 0 || index > LastSetup) {
            report(table, makePath(path, "setup"), "must be an index of sSetupDL, from 0 to 70");
            return std::nullopt;
        }
        return DrawStep{.step = SetupStep{.index = static_cast<u8>(index)}, .location = location};
    }
    if (kind == "colors") {
        const toml::table* colors = table.get_as<toml::table>("colors");
        if (colors == nullptr) {
            report(table, makePath(path, "colors"), "must be a table");
            return std::nullopt;
        }
        return DrawStep{.step = ColorsStep{.colors = readColors(*colors, makePath(path, "colors"))},
                        .location = location};
    }
    if (kind == "segment") {
        const s64 segment = readInteger(table, "segment", path).value_or(0);
        const std::optional<std::string> symbol = readString(table, "symbol", path);
        if (segment < FirstSegment || segment > LastSegment || !symbol) {
            report(table, path, "must point a segment from 8 to 12 at a symbol");
            return std::nullopt;
        }
        return DrawStep{.step = SegmentStep{.segment = static_cast<u8>(segment),
                                            .symbol = *symbol,
                                            .object = readString(table, "object", path)},
                        .location = location};
    }
    if (kind == "transform") {
        const toml::table* transform = table.get_as<toml::table>("transform");
        if (transform == nullptr) {
            report(table, makePath(path, "transform"), "must be a table");
            return std::nullopt;
        }
        return DrawStep{.step = readTransform(*transform, makePath(path, "transform")),
                        .location = location};
    }
    if (kind == "display_list") {
        const std::optional<std::string> name = readString(table, "display_list", path);
        if (!name) {
            return std::nullopt;
        }
        return DrawStep{.step = DisplayListStep{.name = *name,
                                                .object = readString(table, "object", path),
                                                .pass = readName(table, "pass", path, PassNames)
                                                            .value_or(Pass::Opaque)},
                        .location = location};
    }
    if (kind == "shadow") {
        const toml::table* shadow = table.get_as<toml::table>("shadow");
        if (shadow == nullptr) {
            report(table, makePath(path, "shadow"), "must be a table");
            return std::nullopt;
        }
        return DrawStep{
            .step =
                ShadowStep{.shadow =
                               readShadow(*shadow, makePath(path, "shadow")).value_or(Shadow{})},
            .location = location};
    }
    return readSkeletonStep(table, path);
}

std::optional<DrawStep> DatabaseReader::readSkeletonStep(const toml::table& table,
                                                         const std::string_view path) {
    const std::optional<std::string> name = readString(table, "skeleton", path);
    if (!name) {
        return std::nullopt;
    }
    SkeletonStep skeleton{.skeleton = *name,
                          .object = readString(table, "object", path),
                          .animation = readString(table, "animation", path),
                          .animationObject = readString(table, "animation_object", path),
                          .pass = readName(table, "pass", path, PassNames).value_or(Pass::Opaque)};
    if (const std::optional<s64> frame = readInteger(table, "frame", path)) {
        skeleton.frame = static_cast<u32>(std::max<s64>(*frame, 0));
    }
    if (const toml::table* limbs = table.get_as<toml::table>("limbs")) {
        for (const auto& [key, node] : *limbs) {
            const std::string at = makePath(makePath(path, "limbs"), key.str());
            const std::optional<u8> limb = parseLimb(key.str());
            const toml::array* steps = node.as_array();
            if (!limb || steps == nullptr) {
                report(node, at, "must be a limb's index set to a list of steps");
                continue;
            }
            skeleton.limbs.push_back(
                LimbSteps{.limb = *limb, .steps = readSteps(*steps, at, true)});
        }
    }
    return DrawStep{
        .step = std::move(skeleton),
        .location = {.file = file, .line = static_cast<u32>(table.source().begin.line)}};
}

TransformStep DatabaseReader::readTransform(const toml::table& table, const std::string_view path) {
    checkKeys(table, path, TransformKeys);
    return TransformStep{.placement = readPlacement(table, path),
                         .isBillboard = readBool(table, "billboard", path).value_or(false),
                         .resets = readBool(table, "reset", path).value_or(false)};
}

} // namespace toki::actor

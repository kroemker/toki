#include "app/tools/configure/domain/MemoryLayout.h"

#include "app/tools/configure/domain/ConfigSettings.h"
#include "base/Types.h"
#include "hackeroot/ConfigDefines.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using base::u32;
using base::usize;

using toki::configure::MemoryLayout;
using toki::configure::MemoryRegion;

[[nodiscard]] hackeroot::ConfigDefine makeDefine(std::string name, std::string written,
                                                 std::string comment = {}) {
    return {.name = std::move(name),
            .file = "include/config/config_memory.h",
            .written = std::move(written),
            .comment = std::move(comment),
            .effective = std::nullopt};
}

[[nodiscard]] std::vector<hackeroot::ConfigDefine> makeCheckoutDefines() {
    std::vector<hackeroot::ConfigDefine> defines{
        makeDefine("GAMESTATE_ALLOC_SIZE", "0x100000"),
        makeDefine("PLAY_ALLOC_SIZE", "0x4D4790", "The vanilla value is `0x1D4790`, ~1.83MB."),
        makeDefine("AUDIO_HEAP_SIZE", "0x38000", "Audio heap size, vanilla value is `0x38000`."),
        makeDefine("OBJECT_BANK_SIZE", "0x200000", "Vanilla is about `0xFA000`, ~0.98MB"),
        makeDefine("GI_ALLOC_SIZE", "0xB000", "Vanilla's size is very tiny, `0x3008`, ~12KB."),
        makeDefine("POLY_OPA_BUFFER_SIZE", "0x1A00", "Vanilla value is `0x17E0`."),
        makeDefine("POLY_XLU_BUFFER_SIZE", "0xC00", "Vanilla value is `0x800`."),
        makeDefine("OVERLAY_BUFFER_SIZE", "0x400", "Vanilla value is `0x400`."),
        makeDefine("WORK_BUFFER_SIZE", "0x80", "Vanilla value is `0x80`."),
        makeDefine("DEBUG_BUFFER_SIZE", "0xC00"),
        makeDefine("SYS_CFB_END", "0x80800000", "Vanilla value is 0x8044BE80."),
    };
    for (hackeroot::ConfigDefine& define : defines) {
        define.effective = define.written;
    }
    return defines;
}

[[nodiscard]] std::map<std::string, u32, std::less<>> makeCheckoutSymbols() {
    return {{"_buffersSegmentStart", 0x80167DC0},
            {"gZBuffer", 0x80167DC0},
            {"gGfxPools", 0x8018D5C0},
            {"gGfxSPTaskStack", 0x801C3DE0},
            {"gAudioHeap", 0x801DCDE0}};
}

[[nodiscard]] MemoryLayout
makeLayout(const std::vector<hackeroot::ConfigDefine>& defines,
           const std::map<std::string, std::string, std::less<>>& values = {},
           const std::map<std::string, u32, std::less<>>& symbols = makeCheckoutSymbols()) {
    const std::vector<toki::configure::ConfigGroup> groups =
        toki::configure::makeConfigGroups(defines);
    std::expected<MemoryLayout, std::string> layout =
        toki::configure::makeMemoryLayout(groups.front().settings, values, symbols);
    REQUIRE(layout.has_value());
    return std::move(*layout);
}

[[nodiscard]] const MemoryRegion& findRegion(const MemoryLayout& layout, std::string_view label) {
    const auto found = std::ranges::find(layout.regions, label, &MemoryRegion::label);
    REQUIRE(found != layout.regions.end());
    return *found;
}

} // namespace

TEST_CASE("the layout places the static buffers as the build's map does",
          "[tools][configure][memory]") {
    const MemoryLayout layout = makeLayout(makeCheckoutDefines());

    REQUIRE(findRegion(layout, "Code and data").end == 0x80167DC0);
    REQUIRE(findRegion(layout, "Gfx pools ×2").end == 0x801C3DE0);
    REQUIRE(findRegion(layout, "RSP task buffers").end == 0x801DCDE0);
    REQUIRE(findRegion(layout, "Audio heap").end == 0x80214DE0);
    REQUIRE(findRegion(layout, "Framebuffers ×2").start == 0x807B5000);
    REQUIRE(layout.systemHeapSize == 0x5A0220);
    REQUIRE(layout.playSize == 0x4D4790);
    REQUIRE(layout.actorSize == 0x4D4790 - 0x200000 - 0xB000);
}

TEST_CASE("each region follows the one it lies in, in address order",
          "[tools][configure][memory]") {
    const MemoryLayout layout = makeLayout(makeCheckoutDefines());

    for (usize i = 1; i < layout.regions.size(); ++i) {
        REQUIRE(layout.regions[i].start >= layout.regions[i - 1].start);
    }
    const auto object = std::ranges::find(layout.regions, "Object bank", &MemoryRegion::label);
    const std::optional<usize> parent = toki::configure::findParentRegion(
        layout.regions, static_cast<usize>(object - layout.regions.begin()));
    REQUIRE(parent.has_value());
    REQUIRE(layout.regions[*parent].label == "Play state");
}

TEST_CASE("a Gfx buffer costs its commands in both pools, and vanilla is read from its comment",
          "[tools][configure][memory]") {
    const MemoryLayout layout = makeLayout(makeCheckoutDefines());

    const MemoryRegion& opaque = findRegion(layout, "Opaque buffer");
    REQUIRE(opaque.size == 0x1A00 * 16);
    REQUIRE(opaque.vanillaSize == 0x17E0 * 16);
    REQUIRE_FALSE(findRegion(layout, "Debug buffer").vanillaSize.has_value());
    REQUIRE(findRegion(layout, "Object bank").vanillaSize == 0xFA000);
    REQUIRE(findRegion(layout, "Expanded RAM end").vanillaSize == 0x44BE80);
}

TEST_CASE("edited values are drawn, unless the safeguards override them",
          "[tools][configure][memory]") {
    std::vector<hackeroot::ConfigDefine> defines = makeCheckoutDefines();
    const auto play = std::ranges::find(defines, "PLAY_ALLOC_SIZE", &hackeroot::ConfigDefine::name);
    play->effective = "0x1D4790";

    const MemoryLayout layout =
        makeLayout(defines, {{"PLAY_ALLOC_SIZE", "0x300000"}, {"OBJECT_BANK_SIZE", "0x100000"}});

    REQUIRE(layout.playSize == 0x1D4790);
    REQUIRE(findRegion(layout, "Object bank").size == 0x100000);
}

TEST_CASE("Play larger than the heap is clamped, and a bank larger than Play is a problem",
          "[tools][configure][memory]") {
    const MemoryLayout clamped =
        makeLayout(makeCheckoutDefines(), {{"PLAY_ALLOC_SIZE", "0x700000"}});
    REQUIRE(clamped.playSize == clamped.systemHeapSize);
    REQUIRE(findRegion(clamped, "Play state").isProblem);

    const MemoryLayout overflowing =
        makeLayout(makeCheckoutDefines(), {{"PLAY_ALLOC_SIZE", "0x100000"}});
    REQUIRE(overflowing.actorSize < 0);
    REQUIRE(findRegion(overflowing, "Object bank").isProblem);
    REQUIRE(findRegion(overflowing, "Object bank").end ==
            findRegion(overflowing, "Play state").end);
}

TEST_CASE("a layout that cannot be drawn says what is missing", "[tools][configure][memory]") {
    const std::vector<toki::configure::ConfigGroup> groups =
        toki::configure::makeConfigGroups(makeCheckoutDefines());
    std::map<std::string, u32, std::less<>> symbols = makeCheckoutSymbols();
    symbols.erase("gAudioHeap");

    const auto withoutSymbol =
        toki::configure::makeMemoryLayout(groups.front().settings, {}, symbols);
    REQUIRE_FALSE(withoutSymbol.has_value());
    REQUIRE(withoutSymbol.error().contains("gAudioHeap"));

    const auto withText = toki::configure::makeMemoryLayout(
        groups.front().settings, {{"AUDIO_HEAP_SIZE", "LARGE"}}, makeCheckoutSymbols());
    REQUIRE_FALSE(withText.has_value());
    REQUIRE(withText.error().contains("AUDIO_HEAP_SIZE"));
}

TEST_CASE("the vanilla value is the first number after the word", "[tools][configure][memory]") {
    REQUIRE(toki::configure::findVanillaValue("Vanilla is about `0xFA000`, ~0.98MB") == 0xFA000);
    REQUIRE(toki::configure::findVanillaValue("Vanilla value is 0x8044BE80. Make it") ==
            0x8044BE80);
    REQUIRE_FALSE(toki::configure::findVanillaValue("Space for the debug buffer.").has_value());
}

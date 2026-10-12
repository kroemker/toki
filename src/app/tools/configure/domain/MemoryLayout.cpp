#include "MemoryLayout.h"

#include "app/tools/configure/domain/ConfigSettings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::configure {

namespace {

constexpr u32 FramebufferSize = 320 * 240 * 2;
constexpr u32 GfxSize = 8;
constexpr u32 GfxPoolMagics = 0x10;
constexpr u32 GfxPoolHead = 0x8;
constexpr u32 FramebufferAlignment = 0x40;

constexpr std::array MapSymbols = {
    std::string_view("_buffersSegmentStart"),
    std::string_view("gZBuffer"),
    std::string_view("gGfxPools"),
    std::string_view("gGfxSPTaskStack"),
    std::string_view("gAudioHeap"),
};

struct GfxBuffer {
    std::string_view setting;
    std::string_view label;
};

constexpr std::array GfxBuffers = {
    GfxBuffer{"POLY_OPA_BUFFER_SIZE", "Opaque buffer"},
    GfxBuffer{"POLY_XLU_BUFFER_SIZE", "Translucent buffer"},
    GfxBuffer{"OVERLAY_BUFFER_SIZE", "Overlay buffer"},
    GfxBuffer{"WORK_BUFFER_SIZE", "Work buffer"},
    GfxBuffer{"DEBUG_BUFFER_SIZE", "Debug buffer"},
};

struct SettingValue {
    u32 value = 0;
    std::optional<u32> vanilla;
};

using SettingValues = std::map<std::string, SettingValue, std::less<>>;
using Symbols = std::map<std::string, u32, std::less<>>;

[[nodiscard]] std::optional<u32> parseNumber(std::string_view text) {
    while (!text.empty() &&
           (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L')) {
        text.remove_suffix(1);
    }
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::string>
findBuiltText(const ConfigSetting& setting,
              const std::map<std::string, std::string, std::less<>>& values) {
    if (isOverridden(setting.define)) {
        return setting.define.effective;
    }
    const auto edited = values.find(setting.define.name);
    return edited != values.end() ? edited->second : setting.define.written;
}

[[nodiscard]] std::expected<SettingValues, std::string>
readSettingValues(const std::span<const ConfigSetting> settings,
                  const std::map<std::string, std::string, std::less<>>& values) {
    std::vector<std::string_view> wanted{"SYS_CFB_END", "PLAY_ALLOC_SIZE", "OBJECT_BANK_SIZE",
                                         "GI_ALLOC_SIZE", "AUDIO_HEAP_SIZE"};
    for (const GfxBuffer& buffer : GfxBuffers) {
        wanted.push_back(buffer.setting);
    }
    SettingValues read;
    for (const std::string_view name : wanted) {
        const auto setting = std::ranges::find(settings, name, [](const ConfigSetting& candidate) {
            return std::string_view(candidate.define.name);
        });
        if (setting == settings.end()) {
            return std::unexpected(std::format("config_memory.h defines no {}", name));
        }
        const std::optional<std::string> text = findBuiltText(*setting, values);
        const std::optional<u32> value = text ? parseNumber(*text) : std::nullopt;
        if (!value) {
            return std::unexpected(
                std::format("{} is {}, not a number the layout can be drawn with", name,
                            text.value_or("undefined by the safeguards")));
        }
        read.emplace(name, SettingValue{.value = *value,
                                        .vanilla = findVanillaValue(setting->define.comment)});
    }
    return read;
}

[[nodiscard]] std::expected<Symbols, std::string> checkSymbols(const Symbols& symbols) {
    for (const std::string_view name : MapSymbols) {
        const auto found = symbols.find(name);
        if (found == symbols.end()) {
            return std::unexpected(std::format("the build's map lists no {}", name));
        }
        if (found->second < RamStart || found->second >= RamEnd) {
            return std::unexpected(std::format("the build's map puts {} at 0x{:08X}, outside RDRAM",
                                               name, found->second));
        }
    }
    return symbols;
}

[[nodiscard]] std::optional<s64> scaleVanilla(const std::optional<u32> vanilla, const s64 scale) {
    return vanilla ? std::optional<s64>(static_cast<s64>(*vanilla) * scale) : std::nullopt;
}

struct Addresses {
    u32 codeEnd = 0;
    u32 gfxStart = 0;
    u32 gfxEnd = 0;
    u32 taskEnd = 0;
    u32 buffersEnd = 0;
    u32 framebufferStart = 0;
    u32 framebufferEnd = 0;
    u32 poolSize = 0;
};

[[nodiscard]] Addresses placeStatic(const SettingValues& settings, const Symbols& symbols) {
    Addresses at;
    at.codeEnd = symbols.find("_buffersSegmentStart")->second;
    at.gfxStart = symbols.find("gGfxPools")->second;
    at.poolSize = GfxPoolMagics;
    for (const GfxBuffer& buffer : GfxBuffers) {
        at.poolSize += settings.find(buffer.setting)->second.value * GfxSize;
    }
    at.gfxEnd = at.gfxStart + 2 * at.poolSize;
    const u32 taskSize =
        symbols.find("gAudioHeap")->second - symbols.find("gGfxSPTaskStack")->second;
    at.taskEnd = at.gfxEnd + taskSize;
    at.buffersEnd = at.taskEnd + settings.find("AUDIO_HEAP_SIZE")->second.value;
    const u32 cfbEnd = std::min(settings.find("SYS_CFB_END")->second.value, RamEnd);
    at.framebufferEnd = cfbEnd - cfbEnd % FramebufferAlignment;
    at.framebufferStart = at.framebufferEnd - 2 * FramebufferSize;
    return at;
}

void addBuffers(std::vector<MemoryRegion>& regions, const SettingValues& settings,
                const Symbols& symbols, const Addresses& at) {
    const u32 zBuffer = symbols.find("gZBuffer")->second;
    regions.push_back({.label = "Code and data",
                       .source = "boot, code, debug and usb, from the build's map",
                       .reason = "Everything the game links in ahead of its static buffers",
                       .start = RamStart,
                       .end = at.codeEnd,
                       .kind = RegionKind::Code,
                       .placement = StripPlacement::Full});
    regions.push_back({.label = "Z-buffer",
                       .source = "gZBuffer",
                       .reason = "The depth buffer, 320×240 at 16 bits",
                       .start = zBuffer,
                       .end = at.gfxStart,
                       .kind = RegionKind::Fixed,
                       .placement = StripPlacement::Full});
    regions.push_back({.label = "Gfx pools ×2",
                       .source = "gGfxPools",
                       .reason = "The five buffers below, twice, so one frame is built while "
                                 "the other is drawn",
                       .start = at.gfxStart,
                       .end = at.gfxEnd,
                       .kind = RegionKind::Gfx,
                       .placement = StripPlacement::Full});
    u32 offset = GfxPoolHead;
    for (const GfxBuffer& buffer : GfxBuffers) {
        const SettingValue& value = settings.find(buffer.setting)->second;
        const u32 bytes = value.value * GfxSize;
        regions.push_back(
            {.label = std::string(buffer.label),
             .setting = std::string(buffer.setting),
             .source = "Gfx commands of 8 bytes, in each pool",
             .reason = std::format("0x{:X} display list commands of 8 bytes, in each of the two "
                                   "pools",
                                   value.value),
             .start = at.gfxStart + offset,
             .end = at.gfxStart + offset + bytes,
             .size = static_cast<s64>(bytes) * 2,
             .vanillaSize = scaleVanilla(value.vanilla, 2 * GfxSize),
             .depth = 1,
             .kind = RegionKind::Gfx});
        offset += bytes;
    }
    regions.push_back({.label = "RSP task buffers",
                       .source = "gGfxSPTaskStack and on",
                       .reason = "The stack, yield and output buffers of the graphics task",
                       .start = at.gfxEnd,
                       .end = at.taskEnd,
                       .kind = RegionKind::Fixed,
                       .placement = StripPlacement::Full});
    const SettingValue& audio = settings.find("AUDIO_HEAP_SIZE")->second;
    regions.push_back({.label = "Audio heap",
                       .setting = "AUDIO_HEAP_SIZE",
                       .source = "gAudioHeap",
                       .reason = "A static buffer: growing it shrinks the system heap",
                       .start = at.taskEnd,
                       .end = at.buffersEnd,
                       .vanillaSize = scaleVanilla(audio.vanilla, 1),
                       .kind = RegionKind::Audio,
                       .placement = StripPlacement::Full});
}

void addSystemHeap(std::vector<MemoryRegion>& regions, MemoryLayout& layout,
                   const SettingValues& settings, const Addresses& at) {
    const s64 heap = static_cast<s64>(at.framebufferStart) - static_cast<s64>(at.buffersEnd);
    const SettingValue& play = settings.find("PLAY_ALLOC_SIZE")->second;
    const SettingValue& object = settings.find("OBJECT_BANK_SIZE")->second;
    const SettingValue& getItem = settings.find("GI_ALLOC_SIZE")->second;
    const s64 playSize = std::clamp<s64>(play.value, 0, std::max<s64>(heap, 0));
    const s64 actorSize = playSize - object.value - getItem.value;
    const u32 heapEnd = heap > 0 ? at.framebufferStart : at.buffersEnd;
    const u32 playEnd = at.buffersEnd + static_cast<u32>(playSize);
    const u32 objectEnd = at.buffersEnd + object.value;
    const u32 getItemEnd = objectEnd + getItem.value;
    layout.systemHeapSize = heap;
    layout.playSize = playSize;
    layout.actorSize = actorSize;

    regions.push_back(
        {.label = "System heap",
         .source = "from the static buffers to the framebuffers",
         .reason = heap > 0 ? "Everything between the static buffers and the framebuffers"
                            : "The static buffers reach past the framebuffers: lower the audio "
                              "heap or the Gfx buffers, or raise SYS_CFB_END",
         .start = at.buffersEnd,
         .end = heapEnd,
         .size = heap,
         .kind = RegionKind::Heap,
         .placement = StripPlacement::Full,
         .isRemainder = true,
         .isProblem = heap <= 0});
    regions.push_back(
        {.label = "Play state",
         .setting = "PLAY_ALLOC_SIZE",
         .source = "GameState_Realloc",
         .reason = play.value > heap
                       ? std::format("Asks for more than the heap has; GameState_Realloc gives "
                                     "it the 0x{:X} bytes that are left",
                                     playSize)
                       : "The arena gameplay allocates from",
         .start = at.buffersEnd,
         .end = playEnd,
         .size = play.value,
         .vanillaSize = scaleVanilla(play.vanilla, 1),
         .depth = 1,
         .kind = RegionKind::Play,
         .placement = StripPlacement::Over,
         .isProblem = play.value > heap});
    regions.push_back({.label = "Object bank",
                       .setting = "OBJECT_BANK_SIZE",
                       .source = "Play's arena",
                       .reason = actorSize < 0 ? "Larger than what Play is given, with the GI "
                                                 "buffer; its place inside Play is illustrative"
                                               : "Where actors' objects are loaded; its place "
                                                 "inside Play is illustrative",
                       .start = at.buffersEnd,
                       .end = std::min(objectEnd, playEnd),
                       .size = object.value,
                       .vanillaSize = scaleVanilla(object.vanilla, 1),
                       .depth = 2,
                       .kind = RegionKind::Object,
                       .placement = StripPlacement::Inset,
                       .isProblem = actorSize < 0});
    regions.push_back({.label = "GI buffer",
                       .setting = "GI_ALLOC_SIZE",
                       .source = "Player's allocation",
                       .reason = "The model Link holds up when he gets an item; its place inside "
                                 "Play is illustrative",
                       .start = std::min(objectEnd, playEnd),
                       .end = std::min(getItemEnd, playEnd),
                       .size = getItem.value,
                       .vanillaSize = scaleVanilla(getItem.vanilla, 1),
                       .depth = 2,
                       .kind = RegionKind::GetItem,
                       .placement = StripPlacement::Inset});
    regions.push_back({.label = "Actors, rooms, effects",
                       .source = "Play less the object bank and GI buffer",
                       .reason = "What Play keeps for everything else",
                       .start = std::min(getItemEnd, playEnd),
                       .end = playEnd,
                       .size = actorSize,
                       .depth = 2,
                       .kind = RegionKind::Unused,
                       .isRemainder = true,
                       .isProblem = actorSize < 0});
    regions.push_back({.label = "Other heap users",
                       .source = "the system heap less Play",
                       .reason = "Game state structs and overlays, not known without running "
                                 "the game",
                       .start = playEnd,
                       .end = heapEnd,
                       .depth = 1,
                       .kind = RegionKind::Heap,
                       .isRemainder = true});
}

void addFramebuffers(std::vector<MemoryRegion>& regions, const SettingValues& settings,
                     const Addresses& at) {
    const std::optional<u32> vanillaEnd = settings.find("SYS_CFB_END")->second.vanilla;
    regions.push_back({.label = "Framebuffers ×2",
                       .source = "below SYS_CFB_END",
                       .reason = "Two 320×240 framebuffers at 16 bits, ending at SYS_CFB_END",
                       .start = at.framebufferStart,
                       .end = at.framebufferEnd,
                       .kind = RegionKind::Framebuffer,
                       .placement = StripPlacement::Full});
    regions.push_back(
        {.label = "Expanded RAM end",
         .setting = "SYS_CFB_END",
         .source = "an address",
         .reason = "The last address the system uses; the framebuffers end here",
         .start = at.framebufferEnd,
         .end = at.framebufferEnd,
         .size = static_cast<s64>(at.framebufferEnd) - RamStart,
         .vanillaSize = vanillaEnd ? std::optional<s64>(static_cast<s64>(*vanillaEnd) - RamStart)
                                   : std::nullopt,
         .kind = RegionKind::Framebuffer,
         .isBoundary = true});
    regions.push_back({.label = "Past SYS_CFB_END",
                       .source = "SYS_CFB_END to 8 MiB",
                       .reason = "Unused, unless the debug heap is enabled; the camera debugger "
                                 "allocates there",
                       .start = at.framebufferEnd,
                       .end = RamEnd,
                       .kind = RegionKind::Unused,
                       .placement = StripPlacement::Full,
                       .isRemainder = true});
}

} // namespace

std::span<const std::string_view> getMemoryMapSymbols() {
    return MapSymbols;
}

std::optional<u32> findVanillaValue(const std::string_view comment) {
    const usize vanilla = comment.find("anilla");
    if (vanilla == std::string_view::npos) {
        return std::nullopt;
    }
    const usize hex = comment.find("0x", vanilla);
    if (hex == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view digits = comment.substr(hex + 2);
    u32 value = 0;
    const auto [end, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    return error == std::errc{} ? std::optional<u32>(value) : std::nullopt;
}

std::expected<MemoryLayout, std::string>
makeMemoryLayout(const std::span<const ConfigSetting> settings,
                 const std::map<std::string, std::string, std::less<>>& values,
                 const std::map<std::string, u32, std::less<>>& symbols) {
    const std::expected<SettingValues, std::string> read = readSettingValues(settings, values);
    if (!read) {
        return std::unexpected(read.error());
    }
    const std::expected<Symbols, std::string> checked = checkSymbols(symbols);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    const Addresses at = placeStatic(*read, *checked);
    MemoryLayout layout;
    addBuffers(layout.regions, *read, *checked, at);
    addSystemHeap(layout.regions, layout, *read, at);
    addFramebuffers(layout.regions, *read, at);
    for (MemoryRegion& region : layout.regions) {
        if (region.size == 0 && !region.isBoundary) {
            region.size = static_cast<s64>(region.end) - static_cast<s64>(region.start);
        }
    }
    return layout;
}

std::optional<usize> findParentRegion(const std::span<const MemoryRegion> regions,
                                      const usize index) {
    for (usize candidate = index; candidate-- > 0;) {
        if (regions[candidate].depth < regions[index].depth) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace toki::configure

#include "FrameInspection.h"

#include "app/displayList/Types.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/CommandDecoder.h"
#include "n64/Opcode.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "z64/GbiNames.h"
#include "z64/MemoryImage.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toki::displayList {

namespace {

constexpr u32 PhysicalMask = 0x00FF'FFFF;
constexpr u32 SegmentIndex = 0x06;
constexpr f32 NearestW = 1.0e-4F;
constexpr std::array<std::string_view, 5> FormatNames{"RGBA", "YUV", "CI", "IA", "I"};
constexpr std::array<std::string_view, 4> SizeNames{"4", "8", "16", "32"};

[[nodiscard]] u32 findField(const n64::DecodedCommand& command, const std::string_view name) {
    const auto found = std::ranges::find(command.fields, name, &n64::CommandField::name);
    return found == command.fields.end() ? 0 : found->value;
}

[[nodiscard]] bool isCall(const n64::DecodedCommand& command) {
    return command.name == "gsSPDisplayList";
}

[[nodiscard]] bool isTriangles(const n64::DecodedCommand& command) {
    return command.name == "gsSP1Triangle" || command.name == "gsSP2Triangles" ||
           command.name == "gsSP1Quadrangle";
}

[[nodiscard]] bool isLoadStep(const std::string_view name) {
    return name == "gsDPLoadSync" || name == "gsDPTileSync" || name == "gsDPPipeSync" ||
           name == "gsDPSetTile" || name == "gsDPLoadBlock" || name == "gsDPLoadTile" ||
           name == "gsDPLoadTLUTCmd" || name == "gsDPSetTileSize";
}

[[nodiscard]] std::string makePlural(const usize count, const std::string_view one,
                                     const std::string_view many) {
    return std::format("{} {}", count, count == 1 ? one : many);
}

} // namespace

FrameInspection::FrameInspection(const viewport::Frame& frame,
                                 const viewport::ViewportContent& content)
    : frame(frame) {
    indexSymbols(content);
    readRows();
    findLoadGroups();
    usize draw = 0;
    for (const n64::RspOutput& output : frame.rspResult.outputs) {
        const auto* command = std::get_if<n64::RdpCommand>(&output);
        const bool isDraw = command == nullptr || static_cast<n64::Opcode>(command->word0 >> 24) ==
                                                      n64::Opcode::FillRectangle;
        outputDraws.push_back(isDraw ? std::optional<usize>(draw++) : std::nullopt);
    }
    if (draw != frame.rdpResult.draws.size()) {
        outputDraws.assign(frame.rspResult.outputs.size(), std::nullopt);
    }
}

void FrameInspection::indexSymbols(const viewport::ViewportContent& content) {
    for (usize i = 0; i < content.images.size() && i < frame.mappedImages.size(); ++i) {
        const z64::MappedImage& mapped = frame.mappedImages[i];
        for (const auto& [name, symbol] : content.images[i]->symbols) {
            const u32 base = mapped.segments[symbol.segment & 0x0F];
            const auto begin = static_cast<u32>(base + symbol.offset);
            symbols.push_back(
                NamedRange{.begin = begin,
                           .end = begin + static_cast<u32>(std::max<usize>(symbol.size, 1)),
                           .name = name});
        }
    }
    symbols.push_back(NamedRange{.begin = frame.frameListAddress,
                                 .end = frame.frameListAddress +
                                        static_cast<u32>(std::max<usize>(frame.frameListSize, 1)),
                                 .name = "Toki's frame list"});
    std::ranges::sort(symbols, {}, &NamedRange::begin);
}

void FrameInspection::readRows() {
    std::array<u32, n64::SegmentCount> segments{};
    segments[viewport::FrameSegment] = frame.frameListAddress;
    segmentTables.push_back(segments);
    std::vector<usize> calls;
    const std::vector<n64::TraceEntry>& trace = frame.rspResult.trace;
    for (usize i = 0; i < trace.size(); ++i) {
        const n64::TraceEntry& entry = trace[i];
        while (!calls.empty() && entry.depth <= rows[calls.back()].depth) {
            rows[calls.back()].end = i;
            calls.pop_back();
        }
        rows.push_back(InspectedCommand{.entry = i,
                                        .command = n64::decodeCommand(entry.word0, entry.word1),
                                        .depth = entry.depth,
                                        .end = i + 1,
                                        .segments = segmentTables.size() - 1});
        const n64::DecodedCommand& command = rows.back().command;
        if (isCall(command)) {
            calls.push_back(i);
        }
        if (command.opcode == static_cast<u8>(n64::Opcode::MoveWord) &&
            ((entry.word0 >> 16) & 0xFF) == SegmentIndex) {
            segments[((entry.word0 & 0xFFFF) / 4) & 0x0F] = entry.word1;
            segmentTables.push_back(segments);
        }
    }
    for (const usize call : calls) {
        rows[call].end = rows.size();
    }
}

std::string FrameInspection::makeLoadLabel(const usize first, const usize end) const {
    const InspectedCommand* tile = nullptr;
    const InspectedCommand* size = nullptr;
    for (usize row = first; row < end; ++row) {
        const n64::DecodedCommand& command = rows[row].command;
        if (command.name == "gsDPLoadTLUTCmd") {
            return std::format("Loads a palette of {} colours", findField(command, "colors"));
        }
        if (command.name == "gsDPSetTile") {
            tile = &rows[row];
        } else if (command.name == "gsDPSetTileSize") {
            size = &rows[row];
        }
    }
    if (tile == nullptr || size == nullptr) {
        return "Loads texels into texture memory";
    }
    const u32 format = findField(tile->command, "format");
    const u32 texel = findField(tile->command, "size");
    const u32 width =
        ((findField(size->command, "last s") - findField(size->command, "first s")) / 4) + 1;
    const u32 height =
        ((findField(size->command, "last t") - findField(size->command, "first t")) / 4) + 1;
    return std::format("Loads a {}×{} {}{} texture", width, height,
                       format < FormatNames.size() ? FormatNames[format] : "?",
                       texel < SizeNames.size() ? SizeNames[texel] : "?");
}

void FrameInspection::findLoadGroups() {
    for (usize row = 0; row < rows.size(); ++row) {
        if (rows[row].command.name != "gsDPSetTextureImage") {
            continue;
        }
        usize end = row + 1;
        while (end < rows.size() && rows[end].depth == rows[row].depth &&
               isLoadStep(rows[end].command.name)) {
            ++end;
        }
        if (end - row < 3) {
            continue;
        }
        loadGroups.push_back(LoadGroup{.first = row, .end = end, .label = makeLoadLabel(row, end)});
        row = end - 1;
    }
}

std::optional<std::string> FrameInspection::findPhysicalName(const u32 physical) const {
    const auto after = std::ranges::upper_bound(symbols, physical, {}, &NamedRange::begin);
    if (after == symbols.begin()) {
        return std::nullopt;
    }
    const NamedRange& range = *(after - 1);
    if (physical >= range.end) {
        return std::nullopt;
    }
    return physical == range.begin ? range.name
                                   : std::format("{} + 0x{:X}", range.name, physical - range.begin);
}

std::optional<std::string> FrameInspection::findAddressName(const usize row,
                                                            const u32 segmented) const {
    if (row >= rows.size()) {
        return std::nullopt;
    }
    const std::array<u32, n64::SegmentCount>& segments = segmentTables[rows[row].segments];
    const u32 segment = (segmented >> 24) & 0x0F;
    if (segment != 0 && segments[segment] == 0) {
        return std::nullopt;
    }
    return findPhysicalName((segments[segment] + (segmented & PhysicalMask)) & PhysicalMask);
}

std::string FrameInspection::describeEffect(const usize row, const z64::GbiNames& names) const {
    const InspectedCommand& inspected = rows[row];
    const n64::TraceEntry& entry = frame.rspResult.trace[inspected.entry];
    const n64::DecodedCommand& command = inspected.command;
    const auto nameAddress = [&](const u32 segmented) {
        return findAddressName(row, segmented).value_or(std::format("0x{:08X}", segmented));
    };
    const usize made = entry.endOutput - entry.firstOutput;
    if (command.name == "gsSPVertex") {
        const u32 count = findField(command, "count");
        const u32 first = findField(command, "first slot");
        return std::format("Loaded {} into slots {} to {} from {}.",
                           makePlural(count, "vertex", "vertices"), first, first + count - 1,
                           nameAddress(findField(command, "address")));
    }
    if (isTriangles(command)) {
        if (made == 0 && entry.culled == 0) {
            return "Drew nothing: a vertex it names was never loaded.";
        }
        std::string text = std::format("Drew {}", makePlural(made, "triangle", "triangles"));
        if (entry.culled > 0) {
            text += std::format("; {} culled, facing the way culling drops",
                                entry.culled == 1 ? "1 was" : std::format("{} were", entry.culled));
        }
        return text + ".";
    }
    if (isCall(command)) {
        return std::format("Called {}, which ran {} before returning here.",
                           nameAddress(findField(command, "address")),
                           makePlural(inspected.end - row - 1, "command", "commands"));
    }
    if (command.name == "gsSPBranchList") {
        return std::format("Continued with {} and did not come back.",
                           nameAddress(findField(command, "address")));
    }
    if (command.name == "gsSPEndDisplayList") {
        return inspected.depth == 0 ? "Ended the frame's drawing." : "Returned to the caller.";
    }
    if (command.name == "gsSPCullDisplayList") {
        const bool isEnded = row + 1 < rows.size() && rows[row + 1].depth < inspected.depth;
        return isEnded ? "Ended the list: all of the range was off the screen."
                       : "Went on: some of the range is on the screen.";
    }
    if (command.name == "gsSPSegment") {
        return std::format("Pointed segment {} at {}.", findField(command, "segment"),
                           findPhysicalName(findField(command, "address") & PhysicalMask)
                               .value_or(std::format("0x{:08X}", findField(command, "address"))));
    }
    if (command.name == "gsSPMatrix") {
        const u32 flags = findField(command, "flags");
        return std::format("{} the {} matrix{}.",
                           (flags & 0x02) != 0 ? "Loaded" : "Multiplied onto",
                           (flags & 0x04) != 0 ? "projection" : "modelview",
                           (flags & 0x01) != 0 ? ", pushing the one before" : "");
    }
    if (command.name.contains("GeometryMode")) {
        std::string text;
        for (const n64::CommandField& field : command.fields) {
            if (field.value == 0) {
                continue;
            }
            text +=
                std::format("{}Turned {} {}", text.empty() ? "" : "; ",
                            field.name == "clear" ? "off" : "on", names.makeValueText(field, {}));
        }
        return text.empty() ? std::string() : text + ".";
    }
    if (command.name == "gsSPTexture") {
        return findField(command, "on") != 0
                   ? std::format("Turned texturing on, from tile {}.", findField(command, "tile"))
                   : std::string("Turned texturing off.");
    }
    if (made > 0 && command.kind == n64::CommandKind::Geometry) {
        return "Drew a rectangle.";
    }
    return {};
}

std::vector<usize> FrameInspection::listDraws(const usize row) const {
    std::vector<usize> draws;
    const n64::TraceEntry& entry = frame.rspResult.trace[rows[row].entry];
    for (usize output = entry.firstOutput; output < entry.endOutput && output < outputDraws.size();
         ++output) {
        if (outputDraws[output]) {
            draws.push_back(*outputDraws[output]);
        }
    }
    return draws;
}

std::vector<ScreenTriangle> FrameInspection::listScreenTriangles(const usize row) const {
    std::vector<ScreenTriangle> triangles;
    const n64::RdpResult& rdp = frame.rdpResult;
    for (usize ran = row; ran < rows[row].end; ++ran) {
        for (const usize draw : listDraws(ran)) {
            appendScreenTriangles(rdp.draws[draw].primitive, triangles);
        }
    }
    return triangles;
}

void FrameInspection::appendScreenTriangles(const n64::RdpPrimitive& primitive,
                                            std::vector<ScreenTriangle>& triangles) const {
    const auto appendRectangle = [&triangles](const f32 left, const f32 top, const f32 right,
                                              const f32 bottom) {
        triangles.push_back(
            {glm::vec2(left, top), glm::vec2(right, top), glm::vec2(right, bottom)});
        triangles.push_back(
            {glm::vec2(left, top), glm::vec2(right, bottom), glm::vec2(left, bottom)});
    };
    if (const auto* triangle = std::get_if<n64::Triangle>(&primitive)) {
        if (triangle->viewport >= frame.rspResult.viewports.size()) {
            return;
        }
        const n64::Viewport& viewport = frame.rspResult.viewports[triangle->viewport];
        ScreenTriangle corners{};
        for (usize i = 0; i < corners.size(); ++i) {
            const std::array<f32, 4>& position = triangle->vertices[i].position;
            if (position[3] <= NearestW) {
                return;
            }
            corners[i] =
                glm::vec2((position[0] / position[3] * viewport.scale[0]) + viewport.translate[0],
                          (-position[1] / position[3] * viewport.scale[1]) + viewport.translate[1]);
        }
        triangles.push_back(corners);
    } else if (const auto* fill = std::get_if<n64::FillRectangle>(&primitive)) {
        appendRectangle(fill->left / 4.0F, fill->top / 4.0F, (fill->right / 4.0F) + 1.0F,
                        (fill->bottom / 4.0F) + 1.0F);
    } else if (const auto* rectangle = std::get_if<n64::TextureRectangle>(&primitive)) {
        const std::array<u32, 4>& words = rectangle->words;
        appendRectangle(static_cast<f32>((words[1] >> 12) & 0xFFF) / 4.0F,
                        static_cast<f32>(words[1] & 0xFFF) / 4.0F,
                        static_cast<f32>((words[0] >> 12) & 0xFFF) / 4.0F,
                        static_cast<f32>(words[0] & 0xFFF) / 4.0F);
    }
}

const n64::RdpState* FrameInspection::findDrawState(const usize row) const {
    const std::vector<usize> draws = listDraws(row);
    if (draws.empty()) {
        return nullptr;
    }
    const usize state = frame.rdpResult.draws[draws.front()].state;
    return state < frame.rdpResult.states.size() ? &frame.rdpResult.states[state] : nullptr;
}

std::optional<usize> FrameInspection::findFirstRun(const u32 physical) const {
    for (usize row = 0; row < rows.size(); ++row) {
        if (frame.rspResult.trace[rows[row].entry].address == (physical & PhysicalMask)) {
            return row;
        }
    }
    return std::nullopt;
}

} // namespace toki::displayList

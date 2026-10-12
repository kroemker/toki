#include "Rsp.h"

#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "n64/FixedMatrix.h"
#include "n64/Lighting.h"
#include "n64/Memory.h"
#include "n64/Opcode.h"
#include "n64/RspResult.h"
#include "n64/Types.h"
#include "n64/Viewport.h"
#include "n64/detail/RspInterpreter.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace n64 {

RspResult RspInterpreter::run() {
    while (true) {
        const usize traced = result.trace.size();
        const usize culledBefore = culled;
        const bool isRunning = step();
        if (result.trace.size() > traced) {
            result.trace.back().endOutput = result.outputs.size();
            result.trace.back().culled = culled - culledBefore;
        }
        if (!isRunning) {
            break;
        }
    }
    return std::move(result);
}

bool RspInterpreter::step() {
    commandAddress = pc;
    if (++commandCount > CommandLimit) {
        report(base::Severity::Error,
               std::format("the task ran {} commands without ending, so it was stopped",
                           CommandLimit));
        return false;
    }
    const std::optional<std::span<const u8>> bytes = memory.findBytes(pc, 8);
    if (!bytes) {
        report(base::Severity::Error, "no display list is mapped here");
        return false;
    }
    const u32 word0 = readU32(*bytes, 0);
    const u32 word1 = readU32(*bytes, 4);
    pc = (pc + 8) & PhysicalMask;
    if (recordsTrace) {
        result.trace.push_back(TraceEntry{.address = commandAddress,
                                          .word0 = word0,
                                          .word1 = word1,
                                          .depth = returnStack.size(),
                                          .firstOutput = result.outputs.size()});
    }

    const auto opcode = static_cast<Opcode>(word0 >> 24);
    switch (opcode) {
    case Opcode::NoOp:
    case Opcode::SpNoOp:
    case Opcode::CullDisplayList:
        break;
    case Opcode::Vertex:
        runVertex(word0, word1);
        break;
    case Opcode::ModifyVertex:
        runModifyVertex(word0, word1);
        break;
    case Opcode::BranchZ:
        runBranchZ(word0, word1);
        break;
    case Opcode::Triangle1:
        drawTriangle(word0);
        break;
    case Opcode::Triangle2:
    case Opcode::Quad:
        drawTriangle(word0);
        drawTriangle(word1);
        break;
    case Opcode::Special1:
    case Opcode::Special2:
    case Opcode::Special3:
    case Opcode::DmaIo:
        report(base::Severity::Note,
               std::format("command 0x{:02X} has no effect outside the game", word0 >> 24));
        break;
    case Opcode::Texture:
        runTexture(word0, word1);
        break;
    case Opcode::PopMatrix:
        runPopMatrix(word1);
        break;
    case Opcode::GeometryMode:
        geometryMode = (geometryMode & (word0 & 0x00FF'FFFF)) | word1;
        break;
    case Opcode::Matrix:
        runMatrix(word0, word1);
        break;
    case Opcode::MoveWord:
        runMoveWord(word0, word1);
        break;
    case Opcode::MoveMemory:
        runMoveMemory(word0, word1);
        break;
    case Opcode::LoadMicrocode:
        report(base::Severity::Error,
               "the display list loads another microcode, which is not supported");
        return false;
    case Opcode::DisplayList:
        return runDisplayList(word0, word1);
    case Opcode::EndDisplayList:
        if (returnStack.empty()) {
            return false;
        }
        pc = returnStack.back();
        returnStack.pop_back();
        break;
    case Opcode::RdpHalf1:
        rdpHalf1 = word1;
        break;
    case Opcode::RdpHalf2:
        break;
    case Opcode::SetOtherModeLow:
    case Opcode::SetOtherModeHigh:
        runOtherMode(word0, word1, opcode == Opcode::SetOtherModeHigh);
        break;
    case Opcode::SetOtherMode:
        otherModeHigh = word0 & OtherModeHighMask;
        otherModeLow = word1;
        emitRdp(word0, word1);
        break;
    case Opcode::TextureRectangle:
    case Opcode::TextureRectangleFlip:
        runTextureRectangle(word0, word1);
        break;
    case Opcode::SetTextureImage:
    case Opcode::SetDepthImage:
    case Opcode::SetColorImage:
        emitRdp(word0, resolve(word1));
        break;
    case Opcode::LoadSync:
    case Opcode::PipeSync:
    case Opcode::TileSync:
    case Opcode::FullSync:
    case Opcode::SetKeyGreenBlue:
    case Opcode::SetKeyRed:
    case Opcode::SetConvert:
    case Opcode::SetScissor:
    case Opcode::SetPrimitiveDepth:
    case Opcode::LoadTlut:
    case Opcode::SetTileSize:
    case Opcode::LoadBlock:
    case Opcode::LoadTile:
    case Opcode::SetTile:
    case Opcode::FillRectangle:
    case Opcode::SetFillColor:
    case Opcode::SetFogColor:
    case Opcode::SetBlendColor:
    case Opcode::SetPrimitiveColor:
    case Opcode::SetEnvironmentColor:
    case Opcode::SetCombine:
        emitRdp(word0, word1);
        break;
    default:
        report(base::Severity::Warning,
               std::format("unknown F3DEX2 command 0x{:02X} (0x{:08X} 0x{:08X})", word0 >> 24,
                           word0, word1));
        break;
    }
    return true;
}

void RspInterpreter::runTexture(const u32 word0, const u32 word1) {
    texture = TextureState{
        .tile = static_cast<u8>((word0 >> 8) & 0x07),
        .levels = static_cast<u8>(((word0 >> 11) & 0x07) + 1),
        .enabled = ((word0 >> 1) & 0x7F) != 0,
        .scale = {static_cast<u16>(word1 >> 16), static_cast<u16>(word1)},
    };
}

void RspInterpreter::runPopMatrix(const u32 word1) {
    for (u32 count = word1 / FixedMatrixSize; count > 0 && !matrixStack.empty(); --count) {
        modelview = matrixStack.back();
        matrixStack.pop_back();
    }
    modelviewProjectionStale = true;
}

void RspInterpreter::runMatrix(const u32 word0, const u32 word1) {
    const u32 parameters = (word0 & 0xFF) ^ 0x01;
    const std::optional<std::span<const u8>> bytes = readDma(word1, FixedMatrixSize, "a matrix");
    if (!bytes) {
        return;
    }
    const FixedMatrix matrix = readFixedMatrix(*bytes);
    const bool load = (parameters & 0x02) != 0;
    if ((parameters & 0x04) != 0) {
        projection = load ? matrix : multiply(matrix, projection);
    } else {
        if ((parameters & 0x01) != 0) {
            if (matrixStack.size() < MatrixStackDepth) {
                matrixStack.push_back(modelview);
            } else {
                report(base::Severity::Warning,
                       std::format("the matrix stack is full at {} matrices", MatrixStackDepth));
            }
        }
        modelview = load ? matrix : multiply(matrix, modelview);
    }
    modelviewProjectionStale = true;
}

void RspInterpreter::runMoveWord(const u32 word0, const u32 word1) {
    const u32 index = (word0 >> 16) & 0xFF;
    const u32 offset = word0 & 0xFFFF;
    switch (index) {
    case 0x00:
        if (offset < FixedMatrixSize) {
            modelviewProjection = insertMatrixWord(modelviewProjection, offset, word1);
        }
        break;
    case 0x02:
        lightCount = std::min<usize>(word1 / LightSize, MaxLights);
        lightSetup.reset();
        break;
    case 0x04:
    case 0x0E:
        break;
    case 0x06:
        segments[(offset >> 2) & 0x0F] = word1;
        break;
    case 0x08:
        fogMultiplier = static_cast<s16>(word1 >> 16);
        fogOffset = static_cast<s16>(word1);
        break;
    case 0x0A:
        if (LightBase + offset + 4 <= lightMemory.size()) {
            const std::array<u8, 4> color{static_cast<u8>(word1 >> 24),
                                          static_cast<u8>(word1 >> 16), static_cast<u8>(word1 >> 8),
                                          static_cast<u8>(word1)};
            writeLightMemory(LightBase + offset, color);
        }
        break;
    case 0x0C:
        modelviewProjectionStale = word1 == 0;
        break;
    default:
        report(base::Severity::Warning,
               std::format("G_MOVEWORD index 0x{:02X} is not supported", index));
        break;
    }
}

void RspInterpreter::runMoveMemory(const u32 word0, const u32 word1) {
    const u32 index = word0 & 0xFF;
    const usize offset = usize{(word0 >> 8) & 0xFF} * 8;
    const usize size = (usize{(word0 >> 19) & 0x1F} + 1) * 8;
    switch (index) {
    case 8:
        if (const auto bytes = readDma(word1, ViewportSize, "a viewport")) {
            viewport = readViewport(*bytes);
            viewportIndex.reset();
        }
        break;
    case 10:
        if (offset + size > lightMemory.size()) {
            report(base::Severity::Warning,
                   std::format(
                       "G_MOVEMEM writes {} bytes of lights at offset {}, past the light buffer",
                       size, offset));
        } else if (const auto bytes = readDma(word1, size, "a light")) {
            writeLightMemory(offset, *bytes);
        }
        break;
    case 14:
        if (const auto bytes = readDma(word1, FixedMatrixSize, "a matrix")) {
            modelviewProjection = readFixedMatrix(*bytes);
            modelviewProjectionStale = false;
        }
        break;
    default:
        report(base::Severity::Warning, std::format("G_MOVEMEM index {} is not supported", index));
        break;
    }
}

bool RspInterpreter::runDisplayList(const u32 word0, const u32 word1) {
    if (((word0 >> 16) & 0xFF) == 0) {
        if (returnStack.size() >= ReturnStackDepth) {
            report(base::Severity::Error,
                   std::format("display list calls nest deeper than {}", ReturnStackDepth));
            return false;
        }
        returnStack.push_back(pc);
    }
    pc = resolve(word1) & DmaMask;
    return true;
}

void RspInterpreter::runOtherMode(const u32 word0, const u32 word1, const bool high) {
    const u32 length = (word0 & 0xFF) + 1;
    const u32 field = (word0 >> 8) & 0xFF;
    if (field + length > 32) {
        report(base::Severity::Warning,
               std::format("G_SETOTHERMODE sets {} bits from position {}, past the 32-bit word",
                           length, 32 - std::min<u32>(field, 32)));
        return;
    }
    const u32 shift = 32 - field - length;
    const auto mask = static_cast<u32>(((u64{1} << length) - 1) << shift);
    u32& mode = high ? otherModeHigh : otherModeLow;
    mode = (mode & ~mask) | word1;
    emitRdp((u32{static_cast<u8>(Opcode::SetOtherMode)} << 24) |
                (otherModeHigh & OtherModeHighMask),
            otherModeLow);
}

void RspInterpreter::runTextureRectangle(const u32 word0, const u32 word1) {
    const std::optional<std::span<const u8>> bytes = memory.findBytes(pc, 16);
    if (!bytes || static_cast<Opcode>((*bytes)[0]) != Opcode::RdpHalf1 ||
        static_cast<Opcode>((*bytes)[8]) != Opcode::RdpHalf2) {
        report(base::Severity::Warning,
               "a texture rectangle is not followed by G_RDPHALF_1 and G_RDPHALF_2");
        return;
    }
    result.outputs.emplace_back(TextureRectangle{
        .words = {word0, word1, readU32(*bytes, 4), readU32(*bytes, 12)},
        .address = commandAddress,
    });
    pc = (pc + 16) & PhysicalMask;
}

std::optional<std::span<const u8>> RspInterpreter::readDma(const u32 segmented, const usize size,
                                                           const std::string_view what) {
    const u32 physical = resolve(segmented) & DmaMask;
    std::optional<std::span<const u8>> bytes = memory.findBytes(physical, size);
    if (!bytes) {
        report(base::Severity::Warning,
               std::format("cannot read {} bytes of {} at 0x{:08X} (physical 0x{:06X})", size, what,
                           segmented, physical));
    }
    return bytes;
}

void RspInterpreter::emitRdp(const u32 word0, const u32 word1) {
    result.outputs.emplace_back(
        RdpCommand{.word0 = word0, .word1 = word1, .address = commandAddress});
}

void RspInterpreter::report(const base::Severity severity, const std::string& message) {
    result.diagnostics.push_back(base::Diagnostic{
        .severity = severity,
        .location = {},
        .message = std::format("display list command at 0x{:06X}: {}", commandAddress, message),
    });
}

RspResult runTask(const Memory& memory, const RspTask& task) {
    return RspInterpreter(memory, task).run();
}

} // namespace n64

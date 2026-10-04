#include "Rdp.h"

#include "base/Diagnostic.h"
#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/Memory.h"
#include "n64/Opcode.h"
#include "n64/OtherMode.h"
#include "n64/RspResult.h"
#include "n64/TextureMemory.h"
#include "n64/Types.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <variant>

namespace n64 {

namespace {

Color makeColor(const u32 word) {
    return {static_cast<u8>(word >> 24), static_cast<u8>(word >> 16), static_cast<u8>(word >> 8),
            static_cast<u8>(word)};
}

class RdpInterpreter {
public:
    explicit RdpInterpreter(const Memory& memory) : memory(memory) {}

    [[nodiscard]] RdpResult run(const RspResult& rspResult) {
        for (const RspOutput& output : rspResult.outputs) {
            std::visit([this](const auto& item) { apply(item); }, output);
        }
        return std::move(result);
    }

private:
    void apply(const Triangle& triangle) {
        addDraw(triangle);
    }

    void apply(const TextureRectangle& rectangle) {
        addDraw(rectangle);
    }

    void apply(const RdpCommand& command);
    void applyTexture(const RdpCommand& command, bool load);
    void setKey(u32 word0, u32 word1, bool red);
    void setConvert(u32 word0, u32 word1);
    void addDraw(const RdpPrimitive& primitive);

    const Memory& memory;
    RdpResult result;
    TextureMemory textureMemory;
    RdpState state;
    bool textureMemoryChanged = true;
};

void RdpInterpreter::apply(const RdpCommand& command) {
    const u32 word0 = command.word0;
    const u32 word1 = command.word1;
    switch (static_cast<Opcode>(word0 >> 24)) {
    case Opcode::SetOtherMode:
        state.otherMode = OtherMode{.high = word0 & 0x00FF'FFFF, .low = word1};
        break;
    case Opcode::SetCombine:
        state.combineMode = makeCombineMode(word0 & 0x00FF'FFFF, word1);
        break;
    case Opcode::SetPrimitiveColor:
        state.primitiveMinLevel = static_cast<u8>((word0 >> 8) & 0x1F);
        state.primitiveLodFraction = static_cast<u8>(word0);
        state.primitive = makeColor(word1);
        break;
    case Opcode::SetEnvironmentColor:
        state.environment = makeColor(word1);
        break;
    case Opcode::SetBlendColor:
        state.blend = makeColor(word1);
        break;
    case Opcode::SetFogColor:
        state.fog = makeColor(word1);
        break;
    case Opcode::SetFillColor:
        state.fill = word1;
        break;
    case Opcode::SetPrimitiveDepth:
        state.primitiveDepth = static_cast<u16>(word1 >> 16);
        state.primitiveDeltaDepth = static_cast<u16>(word1);
        break;
    case Opcode::SetScissor:
        state.scissor = Scissor{
            .left = static_cast<u16>((word0 >> 12) & 0x0FFF),
            .top = static_cast<u16>(word0 & 0x0FFF),
            .right = static_cast<u16>((word1 >> 12) & 0x0FFF),
            .bottom = static_cast<u16>(word1 & 0x0FFF),
        };
        break;
    case Opcode::SetKeyRed:
    case Opcode::SetKeyGreenBlue:
        setKey(word0, word1, static_cast<Opcode>(word0 >> 24) == Opcode::SetKeyRed);
        break;
    case Opcode::SetConvert:
        setConvert(word0, word1);
        break;
    case Opcode::SetTextureImage:
    case Opcode::SetTile:
    case Opcode::SetTileSize:
        applyTexture(command, false);
        break;
    case Opcode::LoadTile:
    case Opcode::LoadBlock:
    case Opcode::LoadTlut:
        applyTexture(command, true);
        break;
    case Opcode::FillRectangle:
        addDraw(FillRectangle{
            .left = static_cast<u16>((word1 >> 12) & 0x0FFF),
            .top = static_cast<u16>(word1 & 0x0FFF),
            .right = static_cast<u16>((word0 >> 12) & 0x0FFF),
            .bottom = static_cast<u16>(word0 & 0x0FFF),
            .address = command.address,
        });
        break;
    default:
        break;
    }
}

void RdpInterpreter::applyTexture(const RdpCommand& command, const bool load) {
    if (std::optional<base::Diagnostic> diagnostic = textureMemory.apply(command, memory)) {
        result.diagnostics.push_back(std::move(*diagnostic));
    }
    textureMemoryChanged = textureMemoryChanged || load;
    for (usize i = 0; i < TileCount; ++i) {
        state.tiles[i] = textureMemory.getTile(i);
    }
}

void RdpInterpreter::setKey(const u32 word0, const u32 word1, const bool red) {
    if (red) {
        state.keyWidth[0] = static_cast<u16>((word1 >> 16) & 0x0FFF);
        state.keyCenter[0] = static_cast<u8>(word1 >> 8);
        state.keyScale[0] = static_cast<u8>(word1);
        return;
    }
    state.keyWidth[1] = static_cast<u16>((word0 >> 12) & 0x0FFF);
    state.keyWidth[2] = static_cast<u16>(word0 & 0x0FFF);
    state.keyCenter[1] = static_cast<u8>(word1 >> 24);
    state.keyScale[1] = static_cast<u8>(word1 >> 16);
    state.keyCenter[2] = static_cast<u8>(word1 >> 8);
    state.keyScale[2] = static_cast<u8>(word1);
}

void RdpInterpreter::setConvert(const u32 word0, const u32 word1) {
    state.convert = {
        static_cast<u16>((word0 >> 13) & 0x01FF),
        static_cast<u16>((word0 >> 4) & 0x01FF),
        static_cast<u16>(((word0 & 0x0F) << 5) | (word1 >> 27)),
        static_cast<u16>((word1 >> 18) & 0x01FF),
        static_cast<u16>((word1 >> 9) & 0x01FF),
        static_cast<u16>(word1 & 0x01FF),
    };
}

void RdpInterpreter::addDraw(const RdpPrimitive& primitive) {
    if (textureMemoryChanged) {
        state.textureMemory = result.textureMemories.size();
        const auto bytes = textureMemory.getBytes();
        result.textureMemories.emplace_back();
        std::ranges::copy(bytes, result.textureMemories.back().begin());
        textureMemoryChanged = false;
    }
    if (result.states.empty() || result.states.back() != state) {
        result.states.push_back(state);
    }
    result.draws.push_back(Draw{.primitive = primitive, .state = result.states.size() - 1});
}

} // namespace

RdpResult runRdp(const RspResult& rspResult, const Memory& memory) {
    return RdpInterpreter(memory).run(rspResult);
}

} // namespace n64

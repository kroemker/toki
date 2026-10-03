#pragma once

#include "base/Diagnostic.h"
#include "n64/FixedMatrix.h"
#include "n64/Lighting.h"
#include "n64/Memory.h"
#include "n64/Rsp.h"
#include "n64/RspResult.h"
#include "n64/Segment.h"
#include "n64/Types.h"
#include "n64/Viewport.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace n64 {

/// F3DEX2's state and command handlers for one task. Private to `runTask`:
/// the handlers for vertices and triangles are in RspGeometry.cpp, the rest in
/// Rsp.cpp.
class RspInterpreter {
public:
    RspInterpreter(const Memory& memory, const RspTask& task)
        : memory(memory), segments(task.segments), pc(resolve(task.displayList) & DmaMask) {}

    [[nodiscard]] RspResult run();

private:
    static constexpr usize VertexBufferSize = 32;
    static constexpr usize VertexSize = 16;
    static constexpr usize ReturnStackDepth = 18;
    static constexpr usize MatrixStackDepth = 16;
    static constexpr usize MaxLights = 7;
    static constexpr usize LightBase = 2 * LightSize;
    static constexpr usize LightMemorySize = LightBase + ((MaxLights + 1) * LightSize);
    static constexpr u64 CommandLimit = u64{1} << 20;
    static constexpr u32 PhysicalMask = 0x00FF'FFFF;
    static constexpr u32 DmaMask = 0x00FF'FFF8;
    static constexpr u32 OtherModeHighMask = 0x00FF'FFFF;
    static constexpr f32 FixedOne = 65536.0F;
    static constexpr f32 TexelUnits = 32.0F;

    struct VertexSlot {
        ShadedVertex vertex;
        usize viewport = 0;
    };

    struct LightSetup {
        std::vector<DirectionalLight> lights;
        std::array<u8, 3> ambient{};
        std::array<s8, 3> lookAtX{};
        std::array<s8, 3> lookAtY{};
    };

    struct TextureState {
        u8 tile = 0;
        u8 levels = 1;
        bool enabled = false;
        std::array<u16, 2> scale{};
    };

    bool step();
    void runVertex(u32 word0, u32 word1);
    void runModifyVertex(u32 word0, u32 word1);
    void runBranchZ(u32 word0, u32 word1);
    void drawTriangle(u32 indices);
    void runTexture(u32 word0, u32 word1);
    void runPopMatrix(u32 word1);
    void runMatrix(u32 word0, u32 word1);
    void runMoveWord(u32 word0, u32 word1);
    void runMoveMemory(u32 word0, u32 word1);
    bool runDisplayList(u32 word0, u32 word1);
    void runOtherMode(u32 word0, u32 word1, bool high);
    void runTextureRectangle(u32 word0, u32 word1);

    [[nodiscard]] ShadedVertex shadeVertex(std::span<const u8> bytes,
                                           const LightSetup& setup) const;
    [[nodiscard]] LightSetup makeLightSetup() const;
    [[nodiscard]] usize findViewportIndex();

    [[nodiscard]] const Viewport& getViewport(const VertexSlot& slot) const {
        return result.viewports.empty() ? viewport : result.viewports[slot.viewport];
    }

    [[nodiscard]] std::optional<usize> findVertexIndex(u32 index);

    [[nodiscard]] u32 resolve(const u32 segmented) const {
        return (segments[(segmented >> 24) & 0x0F] + (segmented & 0x00FF'FFFF)) & PhysicalMask;
    }

    [[nodiscard]] std::optional<std::span<const u8>> readDma(u32 segmented, usize size,
                                                             std::string_view what);
    void emitRdp(u32 word0, u32 word1);
    void report(base::Severity severity, const std::string& message);

    const Memory& memory;
    RspResult result;
    std::array<u32, SegmentCount> segments;
    u32 pc;
    u32 commandAddress = 0;
    u64 commandCount = 0;
    std::vector<u32> returnStack;
    std::vector<FixedMatrix> matrixStack;
    FixedMatrix modelview = makeIdentityMatrix();
    FixedMatrix projection = makeIdentityMatrix();
    FixedMatrix modelviewProjection = makeIdentityMatrix();
    bool modelviewProjectionStale = false;
    std::array<VertexSlot, VertexBufferSize> vertices{};
    std::array<u8, LightMemorySize> lightMemory{};
    usize lightCount = 1;
    Viewport viewport = makeDefaultViewport();
    std::optional<usize> viewportIndex;
    s16 fogMultiplier = 0;
    s16 fogOffset = 0;
    TextureState texture;
    u32 geometryMode = 0;
    u32 otherModeHigh = 0;
    u32 otherModeLow = 0;
    u32 rdpHalf1 = 0;
};

} // namespace n64

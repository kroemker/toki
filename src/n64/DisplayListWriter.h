#pragma once

#include "n64/FixedMatrix.h"
#include "n64/Types.h"
#include "n64/Viewport.h"

#include <array>
#include <vector>

namespace n64 {

/// One `Vtx`: a model-space position, texture coordinates in s10.5, and a
/// colour, or a normal and alpha when lighting is on.
struct VertexData {
    std::array<s16, 3> position{};
    std::array<s16, 2> texture{};
    std::array<u8, 4> color{};
};

inline constexpr usize VertexDataSize = 16;

/// One `Light`: a colour and the direction it shines from. The ambient light
/// is written as one too, with no direction.
struct LightData {
    std::array<u8, 3> color{};
    std::array<s8, 3> direction{};
};

inline constexpr usize LightDataSize = 16;

/// What `G_MTX` does with the matrix it reads.
enum class MatrixLoad : u8 {
    /// Replaces the projection matrix.
    Projection,
    /// Replaces the top of the modelview stack.
    ModelView,
};

/// Writes one segment of F3DEX2 display list commands and the data they point
/// at, big-endian, as the game's own code builds a display list at run time.
/// Everything written is 8-byte aligned, so commands, matrices and vertices may
/// be interleaved freely. Addresses returned are segmented, in the segment the
/// writer was made for.
class DisplayListWriter {
public:
    explicit DisplayListWriter(u8 segment) : segment(segment) {}

    /// The segmented address the next byte will have.
    [[nodiscard]] u32 getAddress() const;

    [[nodiscard]] const std::vector<u8>& getBytes() const {
        return bytes;
    }

    [[nodiscard]] u32 addMatrix(const FixedMatrix& matrix);

    [[nodiscard]] u32 addViewport(const Viewport& viewport);

    [[nodiscard]] u32 addVertex(const VertexData& vertex);

    [[nodiscard]] u32 addLight(const LightData& light);

    u32 addCommand(u32 word0, u32 word1);

    u32 setViewport(u32 address);

    u32 loadMatrix(u32 address, MatrixLoad load);

    /// `G_VTX`: loads `count` vertices from `address` into the vertex buffer,
    /// starting at slot `first`.
    u32 loadVertices(u32 address, u32 count, u32 first);

    u32 drawTriangle(const std::array<u32, 3>& triangle);

    u32 drawTriangles(const std::array<u32, 3>& first, const std::array<u32, 3>& second);

    /// `G_GEOMETRYMODE`: clears the bits of `clear`, then sets those of `set`.
    u32 setGeometryMode(u32 clear, u32 set);

    /// `G_SETOTHERMODE`: the high word's 24 bits and the whole low word.
    u32 setOtherMode(u32 high, u32 low);

    /// `G_SETCOMBINE` with both of its words already packed.
    u32 setCombine(u32 word0, u32 word1);

    /// `gSPNumLights`: how many directional lights shine, besides the ambient.
    u32 setLightCount(u32 count);

    /// `gSPLight`: loads the light at `address` as light `index`, counting from
    /// one; the ambient light is the one after the last directional light.
    u32 setLight(u32 address, u32 index);

    u32 setFogColor(const std::array<u8, 4>& color);

    /// `gSPFogFactor`: the RSP's fog multiplier and offset.
    u32 setFogFactor(s16 multiplier, s16 offset);

    /// `gSPSegment`: points segment `number` at `physicalAddress`.
    u32 setSegment(u8 number, u32 physicalAddress);

    u32 syncPipe();

    u32 callDisplayList(u32 address);

    u32 endDisplayList();

private:
    u8 segment;
    std::vector<u8> bytes;
};

} // namespace n64

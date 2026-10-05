#include "DisplayListWriter.h"

#include "n64/BigEndian.h"
#include "n64/FixedMatrix.h"
#include "n64/Opcode.h"
#include "n64/Segment.h"
#include "n64/Types.h"
#include "n64/Viewport.h"

#include <algorithm>
#include <array>
#include <span>

namespace n64 {

namespace {

constexpr u32 MatrixPush = 0x01;
constexpr u32 MatrixLoadFlag = 0x02;
constexpr u32 MatrixProjection = 0x04;
constexpr u32 MoveMemoryViewport = 0x08;
constexpr u32 MoveWordSegment = 0x06;
constexpr u32 MoveWordLightCount = 0x02;
constexpr u32 MoveWordFog = 0x08;
constexpr u32 MoveMemoryLight = 0x0A;
constexpr u32 LightStride = 24;
constexpr u32 VertexBufferSize = 32;

u32 makeOpcode(const Opcode opcode) {
    return static_cast<u32>(opcode) << 24;
}

u32 packTriangle(const std::array<u32, 3>& triangle) {
    return ((triangle[0] * 2) << 16) | ((triangle[1] * 2) << 8) | (triangle[2] * 2);
}

} // namespace

u32 DisplayListWriter::getAddress() const {
    return pack(SegmentAddress{.segment = segment, .offset = static_cast<u32>(bytes.size())});
}

u32 DisplayListWriter::addMatrix(const FixedMatrix& matrix) {
    const u32 address = getAddress();
    const std::array<u8, FixedMatrixSize> written = writeFixedMatrix(matrix);
    bytes.insert(bytes.end(), written.begin(), written.end());
    return address;
}

u32 DisplayListWriter::addViewport(const Viewport& viewport) {
    const u32 address = getAddress();
    const std::array<u8, ViewportSize> written = writeViewport(viewport);
    bytes.insert(bytes.end(), written.begin(), written.end());
    return address;
}

u32 DisplayListWriter::addVertex(const VertexData& vertex) {
    const u32 address = getAddress();
    std::array<u8, VertexDataSize> written{};
    for (usize axis = 0; axis < vertex.position.size(); ++axis) {
        writeU16(written, axis * 2, static_cast<u16>(vertex.position[axis]));
    }
    writeU16(written, 8, static_cast<u16>(vertex.texture[0]));
    writeU16(written, 10, static_cast<u16>(vertex.texture[1]));
    for (usize component = 0; component < vertex.color.size(); ++component) {
        written[12 + component] = vertex.color[component];
    }
    bytes.insert(bytes.end(), written.begin(), written.end());
    return address;
}

u32 DisplayListWriter::addLight(const LightData& light) {
    const u32 address = getAddress();
    std::array<u8, LightDataSize> written{};
    for (usize i = 0; i < 3; ++i) {
        written[i] = light.color[i];
        written[4 + i] = light.color[i];
        written[8 + i] = static_cast<u8>(light.direction[i]);
    }
    bytes.insert(bytes.end(), written.begin(), written.end());
    return address;
}

u32 DisplayListWriter::addCommand(const u32 word0, const u32 word1) {
    const u32 address = getAddress();
    std::array<u8, 8> written{};
    writeU32(written, 0, word0);
    writeU32(written, 4, word1);
    bytes.insert(bytes.end(), written.begin(), written.end());
    return address;
}

u32 DisplayListWriter::setViewport(const u32 address) {
    constexpr u32 SizeField = ((ViewportSize - 1) / 8) << 19;
    return addCommand(makeOpcode(Opcode::MoveMemory) | SizeField | MoveMemoryViewport, address);
}

u32 DisplayListWriter::loadMatrix(const u32 address, const MatrixLoad load) {
    constexpr u32 SizeField = ((FixedMatrixSize - 1) / 8) << 19;
    const u32 parameters =
        MatrixLoadFlag | (load == MatrixLoad::Projection ? MatrixProjection : 0U);
    return addCommand(makeOpcode(Opcode::Matrix) | SizeField | (parameters ^ MatrixPush), address);
}

u32 DisplayListWriter::loadVertices(const u32 address, const u32 count, const u32 first) {
    const u32 end = std::min(first + count, VertexBufferSize);
    return addCommand(makeOpcode(Opcode::Vertex) | (count << 12) | (end << 1), address);
}

u32 DisplayListWriter::drawTriangle(const std::array<u32, 3>& triangle) {
    return addCommand(makeOpcode(Opcode::Triangle1) | packTriangle(triangle), 0);
}

u32 DisplayListWriter::drawTriangles(const std::array<u32, 3>& first,
                                     const std::array<u32, 3>& second) {
    return addCommand(makeOpcode(Opcode::Triangle2) | packTriangle(first), packTriangle(second));
}

u32 DisplayListWriter::setGeometryMode(const u32 clear, const u32 set) {
    return addCommand(makeOpcode(Opcode::GeometryMode) | (~clear & 0x00FF'FFFFU), set);
}

u32 DisplayListWriter::setOtherMode(const u32 high, const u32 low) {
    return addCommand(makeOpcode(Opcode::SetOtherMode) | (high & 0x00FF'FFFFU), low);
}

u32 DisplayListWriter::setCombine(const u32 word0, const u32 word1) {
    return addCommand(makeOpcode(Opcode::SetCombine) | (word0 & 0x00FF'FFFFU), word1);
}

u32 DisplayListWriter::setLightCount(const u32 count) {
    return addCommand(makeOpcode(Opcode::MoveWord) | (MoveWordLightCount << 16),
                      count * LightStride);
}

u32 DisplayListWriter::setLight(const u32 address, const u32 index) {
    constexpr u32 SizeField = ((LightDataSize - 1) / 8) << 19;
    const u32 offset = ((index * LightStride) + LightStride) / 8;
    return addCommand(makeOpcode(Opcode::MoveMemory) | SizeField | (offset << 8) | MoveMemoryLight,
                      address);
}

u32 DisplayListWriter::setFogColor(const std::array<u8, 4>& color) {
    return addCommand(makeOpcode(Opcode::SetFogColor), (u32{color[0]} << 24) |
                                                           (u32{color[1]} << 16) |
                                                           (u32{color[2]} << 8) | color[3]);
}

u32 DisplayListWriter::setFogFactor(const s16 multiplier, const s16 offset) {
    return addCommand(makeOpcode(Opcode::MoveWord) | (MoveWordFog << 16),
                      (u32{static_cast<u16>(multiplier)} << 16) | static_cast<u16>(offset));
}

u32 DisplayListWriter::setSegment(const u8 number, const u32 physicalAddress) {
    return addCommand(makeOpcode(Opcode::MoveWord) | (MoveWordSegment << 16) | (u32{number} * 4),
                      physicalAddress);
}

u32 DisplayListWriter::syncPipe() {
    return addCommand(makeOpcode(Opcode::PipeSync), 0);
}

u32 DisplayListWriter::callDisplayList(const u32 address) {
    return addCommand(makeOpcode(Opcode::DisplayList), address);
}

u32 DisplayListWriter::endDisplayList() {
    return addCommand(makeOpcode(Opcode::EndDisplayList), 0);
}

} // namespace n64

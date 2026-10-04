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

#pragma once

#include "n64/Types.h"

namespace n64 {

/// The first byte of a display list command under F3DEX2: the microcode's own
/// commands, and the RDP commands it forwards.
enum class Opcode : u8 {
    NoOp = 0x00,
    Vertex = 0x01,
    ModifyVertex = 0x02,
    CullDisplayList = 0x03,
    BranchZ = 0x04,
    Triangle1 = 0x05,
    Triangle2 = 0x06,
    Quad = 0x07,
    Special3 = 0xD3,
    Special2 = 0xD4,
    Special1 = 0xD5,
    DmaIo = 0xD6,
    Texture = 0xD7,
    PopMatrix = 0xD8,
    GeometryMode = 0xD9,
    Matrix = 0xDA,
    MoveWord = 0xDB,
    MoveMemory = 0xDC,
    LoadMicrocode = 0xDD,
    DisplayList = 0xDE,
    EndDisplayList = 0xDF,
    SpNoOp = 0xE0,
    RdpHalf1 = 0xE1,
    SetOtherModeLow = 0xE2,
    SetOtherModeHigh = 0xE3,
    TextureRectangle = 0xE4,
    TextureRectangleFlip = 0xE5,
    LoadSync = 0xE6,
    PipeSync = 0xE7,
    TileSync = 0xE8,
    FullSync = 0xE9,
    SetKeyGreenBlue = 0xEA,
    SetKeyRed = 0xEB,
    SetConvert = 0xEC,
    SetScissor = 0xED,
    SetPrimitiveDepth = 0xEE,
    SetOtherMode = 0xEF,
    LoadTlut = 0xF0,
    RdpHalf2 = 0xF1,
    SetTileSize = 0xF2,
    LoadBlock = 0xF3,
    LoadTile = 0xF4,
    SetTile = 0xF5,
    FillRectangle = 0xF6,
    SetFillColor = 0xF7,
    SetFogColor = 0xF8,
    SetBlendColor = 0xF9,
    SetPrimitiveColor = 0xFA,
    SetEnvironmentColor = 0xFB,
    SetCombine = 0xFC,
    SetTextureImage = 0xFD,
    SetDepthImage = 0xFE,
    SetColorImage = 0xFF,
};

/// Geometry mode bits as F3DEX2 lays them out.
namespace geometry {

inline constexpr u32 ZBuffer = 0x0000'0001;
inline constexpr u32 Shade = 0x0000'0004;
inline constexpr u32 CullFront = 0x0000'0200;
inline constexpr u32 CullBack = 0x0000'0400;
inline constexpr u32 Fog = 0x0001'0000;
inline constexpr u32 Lighting = 0x0002'0000;
inline constexpr u32 TextureGen = 0x0004'0000;
inline constexpr u32 TextureGenLinear = 0x0008'0000;
inline constexpr u32 SmoothShading = 0x0020'0000;

} // namespace geometry

} // namespace n64

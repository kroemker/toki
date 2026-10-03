#pragma once

#include "n64/Types.h"

#include <array>
#include <initializer_list>
#include <vector>

namespace n64::test {

/// Builds the bytes of one segment by hand: display list commands and the data
/// they point at, big-endian, as F3DEX2 words. Tests use it in place of
/// `gbi.h`, so what a test feeds the RSP is spelled out next to what it expects.
class DisplayListBuilder {
public:
    static constexpr u32 Segment = 0x06;

    /// The segmented address the next byte will have.
    [[nodiscard]] u32 getAddress() const {
        return (Segment << 24) | static_cast<u32>(bytes.size());
    }

    [[nodiscard]] const std::vector<u8>& getBytes() const {
        return bytes;
    }

    u32 addWords(const u32 word0, const u32 word1) {
        const u32 address = getAddress();
        addWord(word0);
        addWord(word1);
        return address;
    }

    u32 addVertex(const std::array<s16, 3> position, const std::array<s16, 2> texture,
                  const std::array<u8, 4> color) {
        const u32 address = getAddress();
        addHalf(static_cast<u16>(position[0]));
        addHalf(static_cast<u16>(position[1]));
        addHalf(static_cast<u16>(position[2]));
        addHalf(0);
        addHalf(static_cast<u16>(texture[0]));
        addHalf(static_cast<u16>(texture[1]));
        for (const u8 component : color) {
            bytes.push_back(component);
        }
        return address;
    }

    /// An `Mtx` from sixteen values in s15.16, row-major.
    u32 addMatrix(const std::array<s32, 16>& elements) {
        const u32 address = getAddress();
        for (const s32 element : elements) {
            addHalf(static_cast<u16>(static_cast<u32>(element) >> 16));
        }
        for (const s32 element : elements) {
            addHalf(static_cast<u16>(element));
        }
        return address;
    }

    u32 addBytes(const std::initializer_list<u8> values) {
        const u32 address = getAddress();
        bytes.insert(bytes.end(), values);
        return address;
    }

    u32 addHalves(const std::initializer_list<s16> values) {
        const u32 address = getAddress();
        for (const s16 value : values) {
            addHalf(static_cast<u16>(value));
        }
        return address;
    }

    u32 loadVertices(const u32 address, const u32 count, const u32 first) {
        return addWords(0x0100'0000U | (count << 12) | ((first + count) << 1), address);
    }

    u32 drawTriangle(const u32 a, const u32 b, const u32 c) {
        return addWords(0x0500'0000U | packTriangle(a, b, c), 0);
    }

    u32 drawTriangles(const std::array<u32, 3> first, const std::array<u32, 3> second) {
        return addWords(0x0600'0000U | packTriangle(first[0], first[1], first[2]),
                        packTriangle(second[0], second[1], second[2]));
    }

    u32 applyMatrix(const u32 address, const u32 parameters) {
        return addWords(0xDA38'0000U | (parameters ^ 0x01U), address);
    }

    u32 setGeometryMode(const u32 clear, const u32 set) {
        return addWords(0xD900'0000U | (~clear & 0x00FF'FFFFU), set);
    }

    u32 setTexture(const u16 scaleS, const u16 scaleT, const u32 tile, const bool on) {
        return addWords(0xD700'0000U | (tile << 8) | (on ? 0x02U : 0x00U),
                        (u32{scaleS} << 16) | scaleT);
    }

    u32 moveWord(const u32 index, const u32 offset, const u32 data) {
        return addWords(0xDB00'0000U | (index << 16) | offset, data);
    }

    u32 moveMemory(const u32 address, const u32 size, const u32 index, const u32 offset) {
        return addWords(0xDC00'0000U | (((size - 1) / 8) << 19) | ((offset / 8) << 8) | index,
                        address);
    }

    u32 callDisplayList(const u32 address) {
        return addWords(0xDE00'0000U, address);
    }

    u32 branchDisplayList(const u32 address) {
        return addWords(0xDE01'0000U, address);
    }

    u32 endDisplayList() {
        return addWords(0xDF00'0000U, 0);
    }

private:
    static u32 packTriangle(const u32 a, const u32 b, const u32 c) {
        return ((a * 2) << 16) | ((b * 2) << 8) | (c * 2);
    }

    void addHalf(const u16 value) {
        bytes.push_back(static_cast<u8>(value >> 8));
        bytes.push_back(static_cast<u8>(value));
    }

    void addWord(const u32 value) {
        addHalf(static_cast<u16>(value >> 16));
        addHalf(static_cast<u16>(value));
    }

    std::vector<u8> bytes;
};

} // namespace n64::test

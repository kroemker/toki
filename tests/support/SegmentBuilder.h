#pragma once

#include "base/Types.h"
#include "z64/MemoryImage.h"

#include <initializer_list>
#include <vector>

namespace testing {

/// One segment's bytes written by hand, big-endian, the way the build lays out
/// a scene or room header and the data it points at.
class SegmentBuilder {
public:
    explicit SegmentBuilder(const base::u8 number) : number(number) {}

    [[nodiscard]] base::u32 getAddress() const {
        return (base::u32{number} << 24) | static_cast<base::u32>(bytes.size());
    }

    void addBytes(const std::initializer_list<base::u8> values) {
        bytes.insert(bytes.end(), values);
    }

    void addHalf(const base::s16 value) {
        bytes.push_back(static_cast<base::u8>(static_cast<base::u16>(value) >> 8));
        bytes.push_back(static_cast<base::u8>(value));
    }

    void addWord(const base::u32 value) {
        for (const int shift : {24, 16, 8, 0}) {
            bytes.push_back(static_cast<base::u8>(value >> shift));
        }
    }

    void addCommand(const base::u8 code, const base::u8 data1, const base::u32 data2) {
        addBytes({code, data1, 0, 0});
        addWord(data2);
    }

    void setWord(const base::usize offset, const base::u32 value) {
        for (base::usize i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<base::u8>(value >> (24 - (i * 8)));
        }
    }

    [[nodiscard]] z64::MemoryImage makeImage() const {
        z64::MemoryImage image;
        image.segments.push_back(z64::ImageSegment{.number = number, .bytes = bytes});
        return image;
    }

private:
    base::u8 number;
    std::vector<base::u8> bytes;
};

} // namespace testing

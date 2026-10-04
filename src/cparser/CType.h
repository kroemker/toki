#pragma once

#include "cparser/ScalarType.h"
#include "cparser/Types.h"

#include <optional>
#include <string>
#include <vector>

namespace cparser {

enum class TypeKind : u8 {
    Void,
    Scalar,
    Pointer,
    Array,
    Struct,
    Union,
    Function,
};

struct CType;

/// One member of a struct or union, where layout put it. A bitfield occupies
/// `bitWidth` bits of the storage unit of its type at `offset`, `bitOffset`
/// bits from that unit's most significant bit, as big-endian MIPS GCC packs
/// them. An anonymous struct or union member has no name; its own members are
/// reached through it.
struct Field {
    std::string name;
    const CType* type = nullptr;
    usize offset = 0;
    u8 bitWidth = 0;
    u8 bitOffset = 0;
};

/// A C type with its layout on the target. An enumeration is a scalar `int`.
/// An array whose length was left out has no `count` and no size, and an
/// incomplete struct, declared but never defined, is not `complete`.
struct CType {
    TypeKind kind = TypeKind::Void;
    std::string name;
    usize size = 0;
    usize alignment = 1;
    ScalarType scalar;
    const CType* element = nullptr;
    std::optional<usize> count;
    std::vector<Field> fields;
    bool complete = true;
};

} // namespace cparser

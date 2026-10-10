#include "TypeTable.h"

#include "cparser/CType.h"
#include "cparser/ScalarType.h"
#include "cparser/Types.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace cparser {

namespace {

constexpr usize PointerSize = 4;
constexpr usize BitsPerByte = 8;

usize alignUp(const usize value, const usize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace

const CType* TypeTable::findTypedef(const std::string_view name) const {
    const auto found = typedefs.find(std::string(name));
    return found == typedefs.end() ? nullptr : found->second;
}

const CType* TypeTable::findTag(const std::string_view name) const {
    const auto found = tags.find(std::string(name));
    return found == tags.end() ? nullptr : found->second;
}

std::optional<s64> TypeTable::findEnumerator(const std::string_view name) const {
    const auto found = enumerators.find(std::string(name));
    return found == enumerators.end() ? std::nullopt : std::optional<s64>(found->second);
}

CType& TypeTable::make(CType type) {
    return types.emplace_back(std::move(type));
}

const CType* TypeTable::getScalar(const ScalarType& scalar) {
    if (scalar.size == 0) {
        return getVoid();
    }
    const auto key = std::make_tuple(scalar.size, scalar.isSigned, scalar.isReal);
    if (const auto found = scalars.find(key); found != scalars.end()) {
        return found->second;
    }
    const CType& made = make(CType{.kind = TypeKind::Scalar,
                                   .name = std::string(scalar.name),
                                   .size = scalar.size,
                                   .alignment = scalar.size,
                                   .scalar = scalar});
    scalars.emplace(key, &made);
    return &made;
}

const CType* TypeTable::getVoid() {
    if (voidType == nullptr) {
        voidType = &make(CType{.kind = TypeKind::Void, .name = "void", .complete = false});
    }
    return voidType;
}

const CType* TypeTable::getFunction() {
    if (functionType == nullptr) {
        functionType =
            &make(CType{.kind = TypeKind::Function, .name = "function", .complete = false});
    }
    return functionType;
}

const CType* TypeTable::getPointer(const CType* element) {
    if (const auto found = pointers.find(element); found != pointers.end()) {
        return found->second;
    }
    const CType& made = make(CType{.kind = TypeKind::Pointer,
                                   .name = element->name + "*",
                                   .size = PointerSize,
                                   .alignment = PointerSize,
                                   .element = element});
    pointers.emplace(element, &made);
    return &made;
}

const CType* TypeTable::getArray(const CType* element, const std::optional<usize> count) {
    const auto key = std::make_pair(element, count);
    if (const auto found = arrays.find(key); found != arrays.end()) {
        return found->second;
    }
    const CType& made = make(CType{.kind = TypeKind::Array,
                                   .name = element->name + "[]",
                                   .size = count.value_or(0) * element->size,
                                   .alignment = element->alignment,
                                   .element = element,
                                   .count = count,
                                   .complete = count.has_value() && element->complete});
    if (element->complete) {
        arrays.emplace(key, &made);
    }
    return &made;
}

CType* TypeTable::declareRecord(const TypeKind kind, const std::string_view name) {
    if (!name.empty()) {
        if (const auto found = tags.find(std::string(name)); found != tags.end()) {
            return found->second;
        }
    }
    CType& made = make(CType{.kind = kind, .name = std::string(name), .complete = false});
    if (!name.empty()) {
        tags.emplace(std::string(name), &made);
    }
    return &made;
}

void TypeTable::completeRecord(CType& record, std::vector<Field> fields, const usize alignment) {
    usize bitPosition = 0;
    usize largest = 0;
    usize strictest = 1;
    const bool isUnion = record.kind == TypeKind::Union;
    std::vector<Field> placed;
    for (Field& field : fields) {
        const CType& type = *field.type;
        const usize unitBits = type.size * BitsPerByte;
        const bool isZeroWidth =
            field.name.empty() && type.kind == TypeKind::Scalar && field.bitWidth == 0;
        if (isZeroWidth) {
            bitPosition = alignUp(bitPosition, std::max<usize>(unitBits, 1));
            continue;
        }
        strictest = std::max(strictest, type.alignment);
        largest = std::max(largest, type.size);
        if (isUnion) {
            field.offset = 0;
            placed.push_back(std::move(field));
            continue;
        }
        if (field.bitWidth > 0 && unitBits > 0) {
            const usize width = field.bitWidth;
            if (bitPosition / unitBits != (bitPosition + width - 1) / unitBits) {
                bitPosition = alignUp(bitPosition, unitBits);
            }
            const usize unitStart = bitPosition / unitBits * unitBits;
            field.offset = unitStart / BitsPerByte;
            field.bitOffset = static_cast<u8>(bitPosition - unitStart);
            bitPosition += width;
        } else {
            const usize offset =
                alignUp(alignUp(bitPosition, BitsPerByte) / BitsPerByte, type.alignment);
            field.offset = offset;
            bitPosition = (offset + type.size) * BitsPerByte;
        }
        placed.push_back(std::move(field));
    }
    record.alignment = std::max(strictest, alignment);
    const usize end = isUnion ? largest : alignUp(bitPosition, BitsPerByte) / BitsPerByte;
    record.size = alignUp(end, record.alignment);
    record.fields = std::move(placed);
    record.complete = true;
}

void TypeTable::defineTypedef(const std::string_view name, const CType* type) {
    typedefs.insert_or_assign(std::string(name), type);
}

void TypeTable::defineEnumerator(const std::string_view name, const s64 value) {
    enumerators.insert_or_assign(std::string(name), value);
}

const Enumeration* TypeTable::findEnumeration(const std::string_view name) const {
    const auto found = enumerationNames.find(std::string(name));
    return found == enumerationNames.end() ? nullptr : &enumerations[found->second];
}

std::vector<std::string_view> TypeTable::listEnumerationNames() const {
    std::vector<std::string_view> names;
    names.reserve(enumerationNames.size());
    for (const auto& [name, enumeration] : enumerationNames) {
        names.emplace_back(name);
    }
    return names;
}

usize TypeTable::defineEnumeration(Enumeration enumeration) {
    enumerations.push_back(std::move(enumeration));
    return enumerations.size() - 1;
}

void TypeTable::nameEnumeration(const std::string_view name, const usize enumeration) {
    enumerationNames.insert_or_assign(std::string(name), enumeration);
}

} // namespace cparser

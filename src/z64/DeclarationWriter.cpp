#include "DeclarationWriter.h"

#include "base/Diagnostic.h"
#include "cparser/CType.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"
#include "z64/Types.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr usize AddressSize = 4;
constexpr usize LineWidth = 96;
constexpr std::string_view Indent = "    ";

u64 readUnsigned(const std::span<const u8> bytes, const usize offset, const usize size) {
    u64 value = 0;
    for (usize i = 0; i < size && offset + i < bytes.size(); ++i) {
        value = (value << 8) | bytes[offset + i];
    }
    return value;
}

bool isAggregate(const cparser::CType& type) {
    return type.kind == cparser::TypeKind::Array || type.kind == cparser::TypeKind::Struct ||
           type.kind == cparser::TypeKind::Union;
}

std::string writeReal(const double value, const bool isSingle) {
    std::string text =
        isSingle ? std::format("{}", static_cast<f32>(value)) : std::format("{}", value);
    if (!text.contains('.') && !text.contains('e')) {
        text += ".0";
    }
    if (isSingle) {
        text += 'f';
    }
    return text;
}

ElementData makeSlice(const ElementData& element, const cparser::CType& type, const usize offset) {
    ElementData slice{
        .type = &type,
        .bytes = element.bytes.subspan(
            std::min(offset, element.bytes.size()),
            std::min(type.size, element.bytes.size() - std::min(offset, element.bytes.size())))};
    for (const Relocation& relocation : element.relocations) {
        if (relocation.offset >= offset && relocation.offset < offset + type.size) {
            Relocation moved = relocation;
            moved.offset -= offset;
            slice.relocations.push_back(std::move(moved));
        }
    }
    return slice;
}

class ValueWriter {
public:
    explicit ValueWriter(const DeclarationWriter& writer) : writer(writer) {}

    std::string write(const ElementData& element, bool isTopLevel);

    std::vector<base::Diagnostic> takeDiagnostics() {
        return std::move(diagnostics);
    }

private:
    std::string writeScalar(const ElementData& element, usize offset, const cparser::CType& type);
    std::string writeRecord(const ElementData& element, bool isTopLevel);
    std::string writeArray(const ElementData& element);
    std::string writeBitfield(const ElementData& element, const cparser::Field& field);

    const DeclarationWriter& writer;
    std::vector<base::Diagnostic> diagnostics;
};

std::string ValueWriter::writeScalar(const ElementData& element, const usize offset,
                                     const cparser::CType& type) {
    const u64 bits = readUnsigned(element.bytes, offset, type.size);
    if (const Relocation* relocation = element.findRelocation(offset);
        relocation != nullptr && type.size == AddressSize) {
        return writer.writeAddress(*relocation, static_cast<u32>(bits), type);
    }
    if (type.kind == cparser::TypeKind::Pointer) {
        return bits == 0 ? std::string("NULL") : std::format("(void*)0x{:08X}", bits);
    }
    if (type.scalar.isReal) {
        const double value = type.size == 4 ? double{std::bit_cast<f32>(static_cast<u32>(bits))}
                                            : std::bit_cast<f64>(bits);
        if (!std::isfinite(value)) {
            diagnostics.push_back(base::makeError(
                {},
                std::format("a real that is not finite (0x{:X}) cannot be written in C", bits)));
            return "0";
        }
        return writeReal(value, type.size == 4);
    }
    return writer.writeInteger(type, bits);
}

std::string ValueWriter::writeBitfield(const ElementData& element, const cparser::Field& field) {
    const usize unitSize = field.type->size;
    const u64 unit = readUnsigned(element.bytes, field.offset, unitSize);
    const usize shift = (unitSize * 8) - field.bitOffset - field.bitWidth;
    const u64 mask = field.bitWidth >= 64 ? ~u64{0} : ((u64{1} << field.bitWidth) - 1);
    u64 value = (unit >> shift) & mask;
    if (field.type->scalar.isSigned && field.bitWidth < 64 &&
        (value >> (field.bitWidth - 1)) != 0) {
        return std::format("{}", static_cast<s64>(value | ~mask));
    }
    return field.type->scalar.isSigned ? std::format("{}", value) : std::format("0x{:X}", value);
}

std::string ValueWriter::writeRecord(const ElementData& element, const bool isTopLevel) {
    const cparser::CType& record = *element.type;
    std::vector<std::string> members;
    for (const cparser::Field& field : record.fields) {
        if (field.bitWidth > 0) {
            if (!field.name.empty()) {
                members.push_back(writeBitfield(element, field));
            }
        } else if (isAggregate(*field.type)) {
            members.push_back(write(makeSlice(element, *field.type, field.offset), false));
        } else {
            members.push_back(writeScalar(element, field.offset, *field.type));
        }
        if (record.kind == cparser::TypeKind::Union) {
            if (!members.empty() && isAggregate(*record.fields.front().type)) {
                return members.front();
            }
            break;
        }
    }
    if (isTopLevel && record.kind == cparser::TypeKind::Struct) {
        std::string text = "{\n";
        for (const std::string& member : members) {
            text += std::format("{}{},\n", Indent, member);
        }
        return text + "}";
    }
    std::string text = "{ ";
    for (usize i = 0; i < members.size(); ++i) {
        text += i == 0 ? members[i] : ", " + members[i];
    }
    return text + " }";
}

std::string ValueWriter::writeArray(const ElementData& element) {
    const cparser::CType& array = *element.type;
    const usize stride = std::max<usize>(array.element->size, 1);
    const usize count = array.count.value_or(element.bytes.size() / stride);
    std::string text = "{ ";
    for (usize i = 0; i < count; ++i) {
        const ElementData item = makeSlice(element, *array.element, i * stride);
        text += (i == 0 ? "" : ", ") + write(item, false);
    }
    return text + " }";
}

std::string ValueWriter::write(const ElementData& element, const bool isTopLevel) {
    const cparser::CType& type = *element.type;
    if (type.kind == cparser::TypeKind::Struct || type.kind == cparser::TypeKind::Union) {
        if (!type.complete) {
            diagnostics.push_back(base::makeError(
                {}, std::format("'{}' is never defined, so it cannot be written", type.name)));
            return "{ 0 }";
        }
        return writeRecord(element, isTopLevel);
    }
    if (type.kind == cparser::TypeKind::Array) {
        return writeArray(element);
    }
    return writeScalar(element, 0, type);
}

} // namespace

const Relocation* ElementData::findRelocation(const usize offset) const {
    const auto found = std::ranges::find(relocations, offset, &Relocation::offset);
    return found == relocations.end() ? nullptr : &*found;
}

DeclarationWriter::DeclarationWriter(DeclarationContext context) : context(std::move(context)) {}

const cparser::CType* DeclarationWriter::findType(const std::string_view name) const {
    return context.findType ? context.findType(name) : nullptr;
}

std::optional<SymbolShape> DeclarationWriter::findSymbol(const std::string_view name) const {
    return context.findSymbol ? context.findSymbol(name) : std::nullopt;
}

std::string DeclarationWriter::writeInteger(const cparser::CType& type, const u64 value) const {
    const usize size = std::clamp<usize>(type.size, 1, 8);
    const u64 mask = size >= 8 ? ~u64{0} : ((u64{1} << (size * 8)) - 1);
    const u64 bits = value & mask;
    if (type.scalar.isSigned) {
        const bool isNegative = (bits >> ((size * 8) - 1)) != 0;
        return std::format("{}",
                           isNegative ? static_cast<s64>(bits | ~mask) : static_cast<s64>(bits));
    }
    return std::format("0x{:0{}X}", bits, size * 2);
}

std::string DeclarationWriter::writeAddress(const Relocation& relocation, const u32 addend,
                                            const cparser::CType& field) const {
    const std::optional<SymbolShape> shape = findSymbol(relocation.symbol);
    const bool isArray = !shape || shape->isArray;
    const usize elementSize = shape ? std::max<usize>(shape->elementSize, 1) : 1;
    std::string address;
    bool isTyped = true;
    if (addend == 0) {
        address = isArray ? relocation.symbol : "&" + relocation.symbol;
    } else if (isArray && addend % elementSize == 0) {
        address = std::format("&{}[{}]", relocation.symbol, addend / elementSize);
    } else {
        address = std::format("(void*)((uintptr_t){}{} + 0x{:X})", isArray ? "" : "&",
                              relocation.symbol, addend);
        isTyped = false;
    }
    if (field.kind != cparser::TypeKind::Pointer) {
        return std::format("(uintptr_t){}", address);
    }
    const cparser::CType* pointee = field.element;
    const bool isSameType = shape && pointee != nullptr &&
                            pointee->kind != cparser::TypeKind::Void &&
                            findType(shape->type) == pointee;
    if (!isTyped || pointee == nullptr || pointee->kind == cparser::TypeKind::Void || isSameType) {
        return address;
    }
    return std::format("(void*){}", address);
}

const cparser::CType* DeclarationWriter::findDeclaredType(const SymbolData& symbol) const {
    return findType(symbol.type);
}

std::string DeclarationWriter::writeDeclarator(const SymbolData& symbol) const {
    std::string declarator(symbol.pointerDepth, '*');
    declarator += ' ';
    declarator += symbol.name;
    if (!symbol.isArray) {
        return declarator;
    }
    usize elementSize = AddressSize;
    if (symbol.pointerDepth == 0) {
        const cparser::CType* base = findDeclaredType(symbol);
        elementSize = base != nullptr ? std::max<usize>(base->size, 1) : 1;
    }
    for (const usize inner : symbol.innerLengths) {
        elementSize *= std::max<usize>(inner, 1);
    }
    declarator += std::format("[{}]", symbol.bytes.size() / elementSize);
    for (const usize inner : symbol.innerLengths) {
        declarator += std::format("[{}]", inner);
    }
    return declarator;
}

std::string DeclarationWriter::writeExtern(const SymbolData& symbol) const {
    return std::format("extern {}{};\n", symbol.type, writeDeclarator(symbol));
}

std::optional<std::string> DeclarationWriter::writeTyped(const std::string_view name,
                                                         const ElementData& element) const {
    const auto found = context.elementWriters.find(name);
    if (found == context.elementWriters.end()) {
        return std::nullopt;
    }
    std::optional<std::string> written = found->second(*this, element);
    if (written && context.isExact && !context.isExact(element, *written)) {
        return std::nullopt;
    }
    return written;
}

WrittenText DeclarationWriter::writeElement(const ElementData& element) const {
    if (std::optional<std::string> written = writeTyped(element.type->name, element)) {
        return WrittenText{.text = std::move(*written)};
    }
    ValueWriter values(*this);
    std::string text = values.write(element, false);
    return WrittenText{.text = std::move(text), .diagnostics = values.takeDiagnostics()};
}

WrittenText DeclarationWriter::writeDefinition(const SymbolData& symbol) const {
    WrittenText written;
    written.text = std::format("{}{} = ", symbol.type, writeDeclarator(symbol));
    if (symbol.embedded) {
        written.text += std::format("{{\n#include \"{}\"\n}};\n", symbol.embedded->spelling);
        return written;
    }
    const cparser::CType* base = findDeclaredType(symbol);
    if (base == nullptr || (!base->complete && symbol.pointerDepth == 0)) {
        written.diagnostics.push_back(
            base::makeError({}, std::format("'{}' has the type '{}', which no header defines",
                                            symbol.name, symbol.type)));
        written.text.clear();
        return written;
    }
    std::deque<cparser::CType> made;
    const cparser::CType* element = base;
    for (usize i = 0; i < symbol.pointerDepth; ++i) {
        element = &made.emplace_back(cparser::CType{.kind = cparser::TypeKind::Pointer,
                                                    .size = AddressSize,
                                                    .alignment = AddressSize,
                                                    .element = element});
    }
    for (auto inner = symbol.innerLengths.rbegin(); inner != symbol.innerLengths.rend(); ++inner) {
        element = &made.emplace_back(cparser::CType{.kind = cparser::TypeKind::Array,
                                                    .size = element->size * *inner,
                                                    .alignment = element->alignment,
                                                    .element = element,
                                                    .count = *inner});
    }
    const ElementData whole{
        .type = element, .bytes = symbol.bytes, .relocations = symbol.relocations};
    const auto writeOne = [&](const ElementData& item, const std::string_view name,
                              const bool isTopLevel) {
        if (std::optional<std::string> typed = writeTyped(name, item)) {
            return std::move(*typed);
        }
        ValueWriter values(*this);
        std::string text = values.write(item, isTopLevel);
        std::ranges::move(values.takeDiagnostics(), std::back_inserter(written.diagnostics));
        return text;
    };
    const std::string_view writerName = symbol.pointerDepth == 0 && symbol.innerLengths.empty()
                                            ? std::string_view(symbol.type)
                                            : std::string_view();
    if (!symbol.isArray) {
        written.text += writeOne(whole, writerName, true);
        written.text += ";\n";
        return written;
    }
    const usize stride = std::max<usize>(element->size, 1);
    const usize count = symbol.bytes.size() / stride;
    std::vector<std::string> items;
    items.reserve(count);
    for (usize i = 0; i < count; ++i) {
        items.push_back(writeOne(makeSlice(whole, *element, i * stride), writerName, false));
    }
    written.text += "{\n";
    if (isAggregate(*element) || context.elementWriters.contains(writerName)) {
        const auto condition = context.elementConditions.find(writerName);
        for (usize i = 0; i < count; ++i) {
            const std::optional<std::string> guard =
                condition == context.elementConditions.end()
                    ? std::nullopt
                    : condition->second(makeSlice(whole, *element, i * stride));
            written.text += guard ? std::format("#if {}\n{}{},\n#endif\n", *guard, Indent, items[i])
                                  : std::format("{}{},\n", Indent, items[i]);
        }
    } else {
        std::string line;
        for (const std::string& item : items) {
            if (!line.empty() && Indent.size() + line.size() + item.size() + 2 > LineWidth) {
                written.text += std::format("{}{}\n", Indent, line);
                line.clear();
            }
            line += line.empty() ? item + "," : " " + item + ",";
        }
        if (!line.empty()) {
            written.text += std::format("{}{}\n", Indent, line);
        }
    }
    written.text += "};\n";
    return written;
}

} // namespace z64

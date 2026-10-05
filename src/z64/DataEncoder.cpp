#include "DataEncoder.h"

#include "base/Diagnostic.h"
#include "cparser/CType.h"
#include "cparser/Dialect.h"
#include "cparser/Expand.h"
#include "cparser/Expression.h"
#include "cparser/Initializer.h"
#include "cparser/Spellings.h"
#include "cparser/StringLiteral.h"
#include "cparser/Token.h"
#include "cparser/TypeTable.h"
#include "cparser/Types.h"
#include "cparser/Value.h"
#include "z64/DecompDialect.h"
#include "z64/DisplayList.h"
#include "z64/DisplayListEncoder.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr usize AddressSize = 4;

using Element = std::span<const cparser::Token>;

class Elements {
public:
    explicit Elements(const Element body) : items(cparser::splitElements(body)) {}

    [[nodiscard]] bool isDone() const {
        return next >= items.size();
    }

    [[nodiscard]] Element peek() const {
        return items[next];
    }

    Element take() {
        return items[next++];
    }

    void replaceNext(const Element rest) {
        items[next] = rest;
    }

    [[nodiscard]] usize countLeft() const {
        return items.size() - next;
    }

private:
    std::vector<Element> items;
    usize next = 0;
};

bool isBuiltin(const std::string_view name) {
    return cparser::findBuiltin(getDecompDialect(), name) != nullptr;
}

bool isEmbedded(const Element element) {
    return element.size() == 1 && element.front().kind == cparser::TokenKind::Embedded;
}

bool isStringLiteral(const Element element) {
    return !element.empty() && std::ranges::all_of(element, [](const cparser::Token& token) {
        return token.kind == cparser::TokenKind::String;
    });
}

std::string readStringLiteral(const Element element) {
    std::string text;
    for (const cparser::Token& token : element) {
        text += cparser::decodeEscapes(token.text.substr(1, token.text.size() - 2));
    }
    return text;
}

class Writer {
public:
    Writer(const DataContext& context, EncodedData& data)
        : context(context), data(data),
          evalContext{
              .dialect = &getDecompDialect(),
              .resolve = [this](const std::string_view name) -> std::optional<cparser::Value> {
                  if (const std::optional<cparser::s64> value =
                          this->context.types->findEnumerator(name)) {
                      return cparser::makeInteger(*value);
                  }
                  return this->context.resolve ? this->context.resolve(name) : std::nullopt;
              },
              .findSymbol = context.findSymbol,
              .findTypeSize = [this](const std::string_view name) -> std::optional<usize> {
                  const cparser::CType* type = this->context.types->findTypedef(name);
                  return type != nullptr && type->complete ? std::optional(type->size)
                                                           : std::nullopt;
              },
              .findArrayLength = context.findArrayLength,
              .unknownIsZero = false} {}

    usize fill(const cparser::CType& type, usize offset, Elements& cursor);
    usize fillAggregate(const cparser::CType& type, usize offset, Elements& cursor);

private:
    usize fillArray(const cparser::CType& array, usize offset, Elements& cursor);
    void fillRecord(const cparser::CType& record, usize offset, Elements& cursor);
    void writeScalar(const cparser::CType& type, usize offset, Element expression,
                     const cparser::Field* bitfield);
    usize writeEmbedded(const cparser::Token& token, usize offset);
    void writeBytes(usize offset, std::span<const u8> bytes);
    void writeInteger(usize offset, usize size, u64 value);

    void report(const Element where, std::string message) {
        data.diagnostics.push_back(base::makeWarning(where.empty() ? base::SourceLocation{}
                                                                   : cparser::locate(where.front()),
                                                     std::move(message)));
    }

    const DataContext& context;
    EncodedData& data;
    cparser::EvalContext evalContext;
};

void Writer::writeBytes(const usize offset, const std::span<const u8> bytes) {
    if (data.bytes.size() < offset + bytes.size()) {
        data.bytes.resize(offset + bytes.size(), 0);
    }
    std::ranges::copy(bytes, data.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void Writer::writeInteger(const usize offset, const usize size, const u64 value) {
    std::vector<u8> bytes(size);
    for (usize i = 0; i < size; ++i) {
        bytes[i] = static_cast<u8>(value >> (8 * (size - 1 - i)));
    }
    writeBytes(offset, bytes);
}

usize Writer::writeEmbedded(const cparser::Token& token, const usize offset) {
    if (!context.encodeEmbedded) {
        report({&token, 1}, "embedded files cannot be encoded here");
        return 0;
    }
    const std::expected<std::vector<u8>, std::string> bytes =
        context.encodeEmbedded(token.embedded);
    if (!bytes) {
        report({&token, 1}, bytes.error());
        return 0;
    }
    writeBytes(offset, *bytes);
    return bytes->size();
}

void Writer::writeScalar(const cparser::CType& type, const usize offset, Element expression,
                         const cparser::Field* bitfield) {
    if (const std::optional<Element> inner = cparser::getBraceBody(expression)) {
        const std::vector<Element> elements = cparser::splitElements(*inner);
        expression = elements.empty() ? Element{} : elements.front();
    }
    if (expression.empty()) {
        return;
    }
    const std::expected<cparser::Value, base::Diagnostic> value =
        cparser::evaluate(expression, evalContext, data.diagnostics);
    if (!value) {
        data.diagnostics.push_back(value.error());
        return;
    }
    if (!value->symbol.empty()) {
        if (type.size != AddressSize || bitfield != nullptr) {
            report(expression, std::format("the address of '{}' does not fit a {}-byte field",
                                           value->symbol, type.size));
            return;
        }
        data.relocations.push_back(
            Relocation{.offset = offset, .symbol = std::string(value->symbol)});
    }
    if (type.kind == cparser::TypeKind::Scalar && type.scalar.isReal) {
        if (type.size == 4) {
            writeInteger(offset, 4, std::bit_cast<u32>(static_cast<f32>(cparser::getReal(*value))));
        } else {
            writeInteger(offset, 8, std::bit_cast<u64>(cparser::getReal(*value)));
        }
        return;
    }
    const u64 bits = static_cast<u64>(cparser::getInteger(*value));
    if (bitfield == nullptr || bitfield->bitWidth == 0) {
        writeInteger(offset, type.size, bits);
        return;
    }
    if (data.bytes.size() < offset + type.size) {
        data.bytes.resize(offset + type.size, 0);
    }
    u64 unit = 0;
    for (usize i = 0; i < type.size; ++i) {
        unit = (unit << 8) | data.bytes[offset + i];
    }
    const usize shift = (type.size * 8) - bitfield->bitOffset - bitfield->bitWidth;
    const u64 mask = ((u64{1} << bitfield->bitWidth) - 1) << shift;
    unit = (unit & ~mask) | ((bits << shift) & mask);
    writeInteger(offset, type.size, unit);
}

usize Writer::fill(const cparser::CType& type, const usize offset, Elements& cursor) {
    if (cursor.isDone()) {
        return 0;
    }
    if (type.kind == cparser::TypeKind::Scalar || type.kind == cparser::TypeKind::Pointer) {
        writeScalar(type, offset, cursor.take(), nullptr);
        return 1;
    }
    const Element next = cursor.peek();
    if (const std::optional<Element> body = cparser::getBraceBody(next)) {
        cursor.take();
        Elements inner(*body);
        const usize filled = fillAggregate(type, offset, inner);
        if (!inner.isDone()) {
            report(inner.peek(),
                   std::format("{} more elements than '{}' holds", inner.countLeft(), type.name));
        }
        return filled;
    }
    return fillAggregate(type, offset, cursor);
}

usize Writer::fillAggregate(const cparser::CType& type, const usize offset, Elements& cursor) {
    if (type.kind == cparser::TypeKind::Array) {
        return fillArray(type, offset, cursor);
    }
    if (type.kind == cparser::TypeKind::Struct || type.kind == cparser::TypeKind::Union) {
        if (!type.complete) {
            report(cursor.isDone() ? Element{} : cursor.peek(),
                   std::format("'{}' is never defined", type.name));
            if (!cursor.isDone()) {
                cursor.take();
            }
            return 0;
        }
        fillRecord(type, offset, cursor);
        return 1;
    }
    if (type.kind == cparser::TypeKind::Scalar || type.kind == cparser::TypeKind::Pointer) {
        return fill(type, offset, cursor);
    }
    report(cursor.isDone() ? Element{} : cursor.peek(),
           std::format("'{}' cannot hold data", type.name));
    if (!cursor.isDone()) {
        cursor.take();
    }
    return 0;
}

usize Writer::fillArray(const cparser::CType& array, const usize offset, Elements& cursor) {
    const cparser::CType& element = *array.element;
    const usize stride = std::max<usize>(element.size, 1);
    if (!cursor.isDone() && isStringLiteral(cursor.peek()) && element.size == 1) {
        std::string text = readStringLiteral(cursor.take());
        text.push_back('\0');
        const usize length = array.count ? std::min(*array.count, text.size()) : text.size();
        const std::vector<u8> bytes(text.begin(),
                                    text.begin() + static_cast<std::ptrdiff_t>(length));
        writeBytes(offset, bytes);
        return length;
    }
    usize index = 0;
    usize highest = 0;
    while (!cursor.isDone() && (!array.count || index < *array.count)) {
        Element next = cursor.peek();
        if (next.size() > 3 && cparser::isPunctuator(next.front(), "[")) {
            const auto close = std::ranges::find_if(next, [](const cparser::Token& token) {
                return cparser::isPunctuator(token, "]");
            });
            const usize closeAt = static_cast<usize>(close - next.begin());
            if (closeAt + 1 < next.size() && cparser::isPunctuator(next[closeAt + 1], "=")) {
                const std::expected<cparser::Value, base::Diagnostic> at =
                    cparser::evaluate(next.subspan(1, closeAt - 1), evalContext, data.diagnostics);
                if (at) {
                    index = static_cast<usize>(cparser::getInteger(*at));
                } else {
                    data.diagnostics.push_back(at.error());
                }
                next = next.subspan(closeAt + 2);
                cursor.replaceNext(next);
            }
        }
        if (isEmbedded(next)) {
            const usize written = writeEmbedded(cursor.take().front(), offset + (index * stride));
            index += (written + stride - 1) / stride;
        } else {
            fill(element, offset + (index * stride), cursor);
            ++index;
        }
        highest = std::max(highest, index);
    }
    return highest;
}

void Writer::fillRecord(const cparser::CType& record, const usize offset, Elements& cursor) {
    usize at = 0;
    while (!cursor.isDone() && at < record.fields.size()) {
        const Element next = cursor.peek();
        if (next.size() > 3 && cparser::isPunctuator(next.front(), ".") &&
            cparser::isPunctuator(next[2], "=")) {
            const auto field =
                std::ranges::find(record.fields, next[1].text, &cparser::Field::name);
            if (field == record.fields.end()) {
                report(next, std::format("'{}' has no member '{}'", record.name, next[1].text));
                cursor.take();
                continue;
            }
            at = static_cast<usize>(field - record.fields.begin());
            cursor.replaceNext(next.subspan(3));
        }
        const cparser::Field& field = record.fields[at];
        if (field.name.empty() && field.bitWidth > 0) {
            ++at;
            continue;
        }
        if (field.bitWidth > 0) {
            writeScalar(*field.type, offset + field.offset, cursor.take(), &field);
        } else {
            fill(*field.type, offset + field.offset, cursor);
        }
        ++at;
        if (record.kind == cparser::TypeKind::Union) {
            break;
        }
    }
}

} // namespace

DataEncoder::DataEncoder(DataContext context)
    : context(std::move(context)),
      displayLists(DisplayListContext{.macros = this->context.macros,
                                      .findSymbol = this->context.findSymbol,
                                      .findArrayLength = this->context.findArrayLength}) {}

bool DataEncoder::isDisplayList(const cparser::CType& type) const {
    const cparser::CType* gfx = context.types->findTypedef("Gfx");
    return gfx != nullptr && type.kind == cparser::TypeKind::Array && type.element == gfx;
}

EncodedData DataEncoder::encodeDisplayLists(const cparser::CType& type,
                                            const std::span<const cparser::Token> initializer) {
    EncodedData data;
    std::span<const cparser::Token> body = initializer;
    if (const std::optional<std::span<const cparser::Token>> inner =
            cparser::getBraceBody(initializer)) {
        body = *inner;
    }
    const usize elementSize = type.element->size;
    for (const std::span<const cparser::Token> element : cparser::splitElements(body)) {
        const usize offset = data.bytes.size();
        const std::optional<std::span<const cparser::Token>> commands =
            cparser::getBraceBody(element);
        DisplayList list = displayLists.encode(commands ? *commands : element);
        if (list.bytes.size() > elementSize) {
            data.diagnostics.push_back(base::makeWarning(
                cparser::locate(element.front()),
                std::format("a display list of {} bytes is cut to the {} of an element of '{}'",
                            list.bytes.size(), elementSize, type.name)));
            list.bytes.resize(elementSize);
        }
        data.bytes.insert(data.bytes.end(), list.bytes.begin(), list.bytes.end());
        data.bytes.resize(offset + elementSize, 0);
        for (Relocation& relocation : list.relocations) {
            if (relocation.offset < elementSize) {
                relocation.offset += offset;
                data.relocations.push_back(std::move(relocation));
            }
        }
        std::ranges::move(list.diagnostics, std::back_inserter(data.diagnostics));
    }
    return data;
}

EncodedData DataEncoder::encode(const cparser::CType& type,
                                const std::span<const cparser::Token> initializer,
                                const bool isBraced) {
    EncodedData data;
    if (type.kind == cparser::TypeKind::Array && type.element != nullptr &&
        isDisplayList(*type.element)) {
        data = encodeDisplayLists(type, initializer);
    } else if (isDisplayList(type)) {
        DisplayList list = displayLists.encode(initializer);
        data.bytes = std::move(list.bytes);
        data.relocations = std::move(list.relocations);
        data.diagnostics = std::move(list.diagnostics);
    } else {
        cparser::Spellings spellings;
        const std::vector<cparser::Token> expanded = cparser::expand(
            initializer,
            cparser::ExpandOptions{
                .macros = context.macros, .keep = &isBuiltin, .spellings = &spellings},
            data.diagnostics);
        Writer writer(context, data);
        std::span<const cparser::Token> body = expanded;
        if (!isBraced && cparser::countElements(body) == 1) {
            if (const std::optional<std::span<const cparser::Token>> inner =
                    cparser::getBraceBody(body)) {
                body = *inner;
            }
        }
        Elements cursor(body);
        const bool isAggregate = type.kind == cparser::TypeKind::Array ||
                                 type.kind == cparser::TypeKind::Struct ||
                                 type.kind == cparser::TypeKind::Union;
        const usize filled =
            isAggregate ? writer.fillAggregate(type, 0, cursor) : writer.fill(type, 0, cursor);
        if (type.kind == cparser::TypeKind::Array && !type.count) {
            data.bytes.resize(filled * type.element->size, 0);
        }
    }
    if (type.complete && data.bytes.size() != type.size) {
        if (data.bytes.size() > type.size) {
            data.diagnostics.push_back(base::makeWarning(
                initializer.empty() ? base::SourceLocation{} : cparser::locate(initializer.front()),
                std::format("the initializer holds {} bytes, more than the {} of '{}'",
                            data.bytes.size(), type.size, type.name)));
        }
        data.bytes.resize(type.size, 0);
    }
    return data;
}

} // namespace z64

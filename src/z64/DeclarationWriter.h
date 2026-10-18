#pragma once

#include "base/Diagnostic.h"
#include "cparser/CType.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"
#include "z64/Types.h"

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace z64 {

class DeclarationWriter;

/// The bytes of one element of a declaration, an array's element or the whole
/// of a single object, and the relocations among them, each at an offset from
/// the element's start.
struct ElementData {
    const cparser::CType* type = nullptr;
    std::span<const u8> bytes;
    std::vector<Relocation> relocations;

    /// The relocation of the word at `offset`, if that word is an address.
    [[nodiscard]] const Relocation* findRelocation(usize offset) const;
};

/// What a symbol a relocation names is, so an address of it is written as an
/// address of its element: `&gFooVtx[4]`, not a byte offset.
struct SymbolShape {
    std::string type;
    bool isArray = false;
    usize elementSize = 1;
};

/// Writes one element of a type another way than member by member, as a
/// display list's command is written as its `gs*` macro. Nothing means the
/// element is written member by member after all.
using ElementWriter =
    std::function<std::optional<std::string>(const DeclarationWriter& writer, const ElementData&)>;

/// The condition an element of an array is written under, as `#if` would test
/// it, or nothing for one that is always there.
using ElementCondition = std::function<std::optional<std::string>(const ElementData& element)>;

/// Whether `text`, as the initializer of an element, encodes to that element's
/// bytes and relocations.
using ElementCheck = std::function<bool(const ElementData& element, std::string_view text)>;

struct DeclarationContext {
    /// The type a declaration's base type name names: a typedef, a tag or a
    /// scalar's words, as the prelude the symbol was read against defines it.
    std::function<const cparser::CType*(std::string_view)> findType;

    /// The symbols relocations may name.
    std::function<std::optional<SymbolShape>(std::string_view)> findSymbol;

    /// The writers of elements of the types named, by the base type name a
    /// declaration is written with (`Gfx`, `SceneCmd`).
    std::map<std::string, ElementWriter, std::less<>> elementWriters;

    /// The conditions elements of the types named are written under.
    std::map<std::string, ElementCondition, std::less<>> elementConditions;

    /// What an element writer writes is used only when this says it is exact.
    /// Without it, the writers are trusted.
    ElementCheck isExact;
};

/// What writing made, and what it could not write exactly: a union whose bytes
/// lie outside its first member, padding that is not zero, a real C cannot
/// spell.
struct WrittenText {
    std::string text;
    std::vector<base::Diagnostic> diagnostics;
};

/// Writes C declarations back from the bytes a symbol's initializer encoded to
/// (`SymbolData`), so that `DataEncoder` encodes them to the same bytes and
/// relocations again. The type says how the bytes split: a struct is written
/// as a positional initializer, a union as its first member, an array element
/// by element, a bitfield by its bits. An integer is written in hex when it is
/// unsigned and in decimal when it is signed, a real as the shortest decimal
/// that reads back to it, and a word a relocation patches as an address of the
/// symbol it names, plus the constant the word holds. A symbol built from an
/// embedded file is written as the include of it again.
///
/// The writers given for element types write those elements instead, as long
/// as what they write passes the context's check, so the generic form is the
/// fallback that is always exact.
class DeclarationWriter {
public:
    explicit DeclarationWriter(DeclarationContext context);

    /// `Type name[count] = { ... };` and a newline.
    [[nodiscard]] WrittenText writeDefinition(const SymbolData& symbol) const;

    /// `extern Type name[count];` and a newline.
    [[nodiscard]] std::string writeExtern(const SymbolData& symbol) const;

    /// The declarator after the base type: `* name[3][2]`.
    [[nodiscard]] std::string writeDeclarator(const SymbolData& symbol) const;

    /// An element of `element.type` as an initializer.
    [[nodiscard]] WrittenText writeElement(const ElementData& element) const;

    /// The address `relocation` patches into a field of `field`'s type, the
    /// constant `addend` added: `sym`, `&sym`, `&sym[4]`, a byte offset cast
    /// through `uintptr_t`, and an integer field's address cast to it.
    [[nodiscard]] std::string writeAddress(const Relocation& relocation, u32 addend,
                                           const cparser::CType& field) const;

    /// An integer as the writer writes it in a field of `type`.
    [[nodiscard]] std::string writeInteger(const cparser::CType& type, u64 value) const;

    [[nodiscard]] const cparser::CType* findType(std::string_view name) const;

    [[nodiscard]] std::optional<SymbolShape> findSymbol(std::string_view name) const;

private:
    [[nodiscard]] const cparser::CType* findDeclaredType(const SymbolData& symbol) const;

    [[nodiscard]] std::optional<std::string> writeTyped(std::string_view name,
                                                        const ElementData& element) const;

    DeclarationContext context;
};

} // namespace z64

#pragma once

#include "cparser/CType.h"
#include "cparser/ScalarType.h"
#include "cparser/Types.h"

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cparser {

/// An enumeration constant, as its enumeration lists it.
struct Enumerator {
    std::string name;
    s64 value = 0;
};

/// The types one reading pass defined: typedef names, struct, union and enum
/// tags, and enumeration constants, with every type laid out for the target.
/// Types are never freed or moved while the table lives, so a `const CType*`
/// stays valid as long as the table, and moving the table keeps it valid.
class TypeTable {
public:
    [[nodiscard]] const CType* findTypedef(std::string_view name) const;

    [[nodiscard]] const CType* findTag(std::string_view name) const;

    [[nodiscard]] std::optional<s64> findEnumerator(std::string_view name) const;

    /// The constants of the enumeration that `name` tags or names by a typedef,
    /// in the order they are written, if one was read.
    [[nodiscard]] const std::vector<Enumerator>* findEnumeration(std::string_view name) const;

    /// The node for a scalar, a pointer to `element`, or an array of `count`
    /// elements, made once and shared. An array of a struct not yet defined is
    /// made anew each time, so it is never sized before its element is.
    [[nodiscard]] const CType* getScalar(const ScalarType& scalar);
    [[nodiscard]] const CType* getVoid();
    [[nodiscard]] const CType* getPointer(const CType* element);
    [[nodiscard]] const CType* getArray(const CType* element, std::optional<usize> count);
    [[nodiscard]] const CType* getFunction();

    /// The struct or union `name` names, made incomplete the first time it is
    /// mentioned, so that a pointer to it can exist before its definition.
    /// An anonymous one is new every time.
    [[nodiscard]] CType* declareRecord(TypeKind kind, std::string_view name);

    /// Lays out `record`'s fields, given in declaration order with their
    /// types and bit widths, and marks it complete. `alignment` overrides the
    /// natural alignment when larger, as `__attribute__((aligned))` does.
    static void completeRecord(CType& record, std::vector<Field> fields, usize alignment);

    void defineTypedef(std::string_view name, const CType* type);

    void defineEnumerator(std::string_view name, s64 value);

    /// Keeps the constants of one enumeration, and returns what tells it apart
    /// for `nameEnumeration`.
    [[nodiscard]] usize defineEnumeration(std::vector<Enumerator> constants);

    /// Lets `findEnumeration` find `enumeration` by `name`, its tag or a
    /// typedef of it.
    void nameEnumeration(std::string_view name, usize enumeration);

    [[nodiscard]] usize countTypedefs() const {
        return typedefs.size();
    }

private:
    CType& make(CType type);

    std::deque<CType> types;
    std::unordered_map<std::string, const CType*> typedefs;
    std::unordered_map<std::string, CType*> tags;
    std::unordered_map<std::string, s64> enumerators;
    std::deque<std::vector<Enumerator>> enumerations;
    std::unordered_map<std::string, usize> enumerationNames;
    std::map<std::tuple<u8, bool, bool>, const CType*> scalars;
    std::map<const CType*, const CType*> pointers;
    std::map<std::pair<const CType*, std::optional<usize>>, const CType*> arrays;
    const CType* voidType = nullptr;
    const CType* functionType = nullptr;
};

} // namespace cparser

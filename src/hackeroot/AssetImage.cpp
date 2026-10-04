#include "AssetImage.h"

#include "base/Diagnostic.h"
#include "cparser/CType.h"
#include "cparser/DirectoryCache.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/TypeName.h"
#include "cparser/TypeParser.h"
#include "cparser/TypeTable.h"
#include "cparser/Types.h"
#include "cparser/Value.h"
#include "hackeroot/AssetSource.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Types.h"
#include "hackeroot/Workspace.h"
#include "z64/CodeSymbols.h"
#include "z64/DataEncoder.h"
#include "z64/DecompDialect.h"
#include "z64/MemoryImage.h"
#include "z64/TextureEncoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr usize WordAlignment = 4;
constexpr std::array<std::string_view, 1> AlsoHidden{"ultra64/gbi.f3dex3.h"};

struct FileRead {
    u8 segment = 0;
    cparser::TranslationUnit unit;
    const Prelude* prelude = nullptr;
    std::unique_ptr<cparser::MacroTable> macros;
    std::vector<cparser::Declaration> declarations;
    std::vector<const cparser::CType*> types;
    cparser::TypeTable ownTypes;
};

const cparser::CType* findBaseType(Prelude& prelude, const std::string_view name) {
    if (const cparser::CType* type = prelude.types.findTypedef(name)) {
        return type;
    }
    if (const cparser::CType* type = prelude.types.findTag(name)) {
        return type;
    }
    const std::array<std::string_view, 1> words{name};
    if (const std::optional<cparser::ScalarType> scalar =
            cparser::combineScalarWords(z64::getDecompDialect(), words)) {
        return prelude.types.getScalar(*scalar);
    }
    return nullptr;
}

const cparser::CType* findDeclaredType(Prelude& prelude, const cparser::Declaration& declaration) {
    const cparser::CType* type = findBaseType(prelude, declaration.type);
    if (type == nullptr) {
        return nullptr;
    }
    for (usize i = 0; i < declaration.pointerDepth; ++i) {
        type = prelude.types.getPointer(type);
    }
    if (declaration.isArray) {
        type = prelude.types.getArray(type, declaration.hasDeclaredLength ? declaration.arrayLength
                                                                          : std::nullopt);
    }
    return type;
}

std::optional<cparser::Value> findEnumerator(const FileRead& file, const std::string_view name) {
    std::optional<cparser::s64> value = file.ownTypes.findEnumerator(name);
    if (!value) {
        value = file.prelude->types.findEnumerator(name);
    }
    return value ? std::optional(cparser::makeInteger(*value)) : std::nullopt;
}

bool isLinkerSymbol(const std::string_view name) {
    return name.starts_with('_') && name.contains("Segment") &&
           (name.ends_with("Start") || name.ends_with("End"));
}

void completeLengths(std::vector<cparser::Declaration>& declarations) {
    std::map<std::string_view, usize> declared;
    for (const cparser::Declaration& declaration : declarations) {
        if (declaration.hasDeclaredLength && declaration.arrayLength) {
            declared.emplace(declaration.name, *declaration.arrayLength);
        }
    }
    for (cparser::Declaration& declaration : declarations) {
        const auto found = declared.find(declaration.name);
        if (declaration.isArray && !declaration.hasDeclaredLength && found != declared.end()) {
            declaration.arrayLength = found->second;
            declaration.hasDeclaredLength = true;
        }
    }
}

usize findAlignment(const cparser::CType& type) {
    const bool isAggregate = type.kind == cparser::TypeKind::Array ||
                             type.kind == cparser::TypeKind::Struct ||
                             type.kind == cparser::TypeKind::Union;
    return isAggregate ? std::max(type.alignment, WordAlignment) : type.alignment;
}

class ImageReader {
public:
    explicit ImageReader(const Workspace& workspace)
        : workspace(workspace), options(makeAssetPreprocessOptions(
                                    workspace.getRoot(), workspace.getBuildDefines().version)),
          textures(directories, options.allowedRoots) {
        for (const z64::CodeSymbol& symbol : z64::getCodeSymbols()) {
            elementSizes.emplace(std::string(symbol.name), symbol.elementSize);
        }
    }

    z64::MemoryImage read(std::span<const SegmentFiles> segments);

private:
    void readFile(u8 segment, const std::filesystem::path& file);
    cparser::ParseResult parseFile(FileRead& read);
    void keepLoud(std::vector<base::Diagnostic>& found);
    void placeObjects(FileRead& read, Prelude& prelude,
                      const std::vector<cparser::Declaration>& declarations);
    Prelude& findPrelude(const std::vector<std::string>& includes);
    void encodeFile(FileRead& file, std::vector<z64::ImageObject>& objects);

    const Workspace& workspace;
    cparser::PreprocessOptions options;
    cparser::DirectoryCache directories;
    z64::TextureEncoder textures;
    std::map<std::vector<std::string>, std::unique_ptr<Prelude>> preludes;
    std::vector<std::unique_ptr<FileRead>> files;
    std::map<std::string, usize, std::less<>> elementSizes;
    std::vector<base::Diagnostic> diagnostics;
};

Prelude& ImageReader::findPrelude(const std::vector<std::string>& includes) {
    std::unique_ptr<Prelude>& prelude = preludes[includes];
    if (!prelude) {
        prelude = std::make_unique<Prelude>(readPrelude(workspace, includes, AlsoHidden));
        for (const base::Diagnostic& diagnostic : prelude->diagnostics) {
            if (diagnostic.severity != base::Severity::Note) {
                diagnostics.push_back(diagnostic);
            }
        }
    }
    return *prelude;
}

void ImageReader::readFile(const u8 segment, const std::filesystem::path& file) {
    std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        cparser::preprocess(workspace.getMacros(), file, options);
    if (!unit) {
        diagnostics.push_back(unit.error());
        return;
    }
    auto read = std::make_unique<FileRead>(FileRead{.segment = segment, .unit = std::move(*unit)});
    Prelude& prelude = findPrelude(read->unit.unresolvedIncludes);
    read->prelude = &prelude;
    read->macros = std::make_unique<cparser::MacroTable>(&prelude.macros);
    read->macros->defineFrom(read->unit.macros);
    read->ownTypes =
        cparser::parseTypes(read->unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()})
            .types;

    cparser::ParseResult parsed = parseFile(*read);
    completeLengths(parsed.declarations);
    placeObjects(*read, prelude, parsed.declarations);
    files.push_back(std::move(read));
}

cparser::ParseResult ImageReader::parseFile(FileRead& read) {
    const FileRead& reading = read;
    cparser::ParseResult parsed = cparser::parseDeclarations(
        read.unit,
        cparser::EvalContext{
            .dialect = &z64::getDecompDialect(),
            .resolve =
                [&reading](const std::string_view name) { return findEnumerator(reading, name); },
            .findTypeSize = [&reading](const std::string_view name) -> std::optional<usize> {
                const cparser::CType* type = reading.prelude->types.findTypedef(name);
                return type != nullptr && type->complete ? std::optional(type->size) : std::nullopt;
            }});
    keepLoud(read.unit.diagnostics);
    keepLoud(parsed.diagnostics);
    return parsed;
}

void ImageReader::keepLoud(std::vector<base::Diagnostic>& found) {
    for (base::Diagnostic& diagnostic : found) {
        if (diagnostic.severity != base::Severity::Note) {
            diagnostics.push_back(std::move(diagnostic));
        }
    }
}

void ImageReader::placeObjects(FileRead& read, Prelude& prelude,
                               const std::vector<cparser::Declaration>& declarations) {
    std::map<std::string_view, usize> positions;
    std::vector<bool> isDefined;
    for (const cparser::Declaration& declaration : declarations) {
        const cparser::CType* type = findDeclaredType(prelude, declaration);
        if (type == nullptr) {
            if (declaration.hasInitializer) {
                diagnostics.push_back(base::makeError(
                    declaration.location,
                    std::format("'{}' has the type '{}', which no header it includes defines",
                                declaration.name, declaration.type)));
            }
            continue;
        }
        const usize elementSize =
            type->kind == cparser::TypeKind::Array ? type->element->size : type->size;
        if (declaration.isExtern) {
            elementSizes.emplace(std::string(declaration.name), elementSize);
            continue;
        }
        elementSizes.insert_or_assign(std::string(declaration.name), elementSize);
        if (const auto position = positions.find(declaration.name); position != positions.end()) {
            if (declaration.hasInitializer) {
                read.declarations[position->second] = declaration;
                read.types[position->second] = type;
                isDefined[position->second] = true;
            }
            continue;
        }
        positions.emplace(declaration.name, read.declarations.size());
        read.declarations.push_back(declaration);
        read.types.push_back(type);
        isDefined.push_back(declaration.hasInitializer);
    }
    usize kept = 0;
    for (usize i = 0; i < isDefined.size(); ++i) {
        if (!isDefined[i]) {
            continue;
        }
        if (kept != i) {
            read.declarations[kept] = std::move(read.declarations[i]);
            read.types[kept] = read.types[i];
        }
        ++kept;
    }
    read.declarations.resize(kept);
    read.types.resize(kept);
}

void ImageReader::encodeFile(FileRead& file, std::vector<z64::ImageObject>& objects) {
    std::map<std::string_view, usize> lengths;
    for (const cparser::Declaration& declaration : file.declarations) {
        if (declaration.arrayLength) {
            lengths.emplace(declaration.name, *declaration.arrayLength);
        }
    }
    z64::DataEncoder encoder(z64::DataContext{
        .macros = file.macros.get(),
        .types = &file.prelude->types,
        .resolve = [&file](const std::string_view name) { return findEnumerator(file, name); },
        .findSymbol = [this](const std::string_view name) -> std::optional<cparser::SymbolAddress> {
            const auto found = elementSizes.find(name);
            if (found != elementSizes.end()) {
                return cparser::SymbolAddress{.name = found->first, .elementSize = found->second};
            }
            if (isLinkerSymbol(name)) {
                return cparser::SymbolAddress{.elementSize = 1};
            }
            return std::nullopt;
        },
        .findArrayLength = [&lengths](const std::string_view name) -> std::optional<usize> {
            const auto found = lengths.find(name);
            return found == lengths.end() ? std::nullopt : std::optional(found->second);
        },
        .encodeEmbedded = [this,
                           &file](const u32 index) -> std::expected<std::vector<u8>, std::string> {
            const cparser::EmbeddedFile& embedded = file.unit.embedded[index];
            return textures.encode(embedded.path, embedded.tag);
        },
    });
    for (usize i = 0; i < file.declarations.size(); ++i) {
        const cparser::Declaration& declaration = file.declarations[i];
        const cparser::CType& type = *file.types[i];
        objects.push_back(z64::ImageObject{
            .segment = file.segment,
            .name = std::string(declaration.name),
            .alignment = findAlignment(type),
            .startsFile = i == 0,
            .data = encoder.encode(type, declaration.initializer, declaration.isBraced),
            .location = declaration.location,
        });
    }
}

z64::MemoryImage ImageReader::read(const std::span<const SegmentFiles> segments) {
    for (const SegmentFiles& segment : segments) {
        for (const std::filesystem::path& file : segment.files) {
            readFile(segment.segment, file);
        }
    }
    std::vector<z64::ImageObject> objects;
    for (const z64::CodeSymbol& symbol : z64::getCodeSymbols()) {
        objects.push_back(z64::ImageObject{
            .segment = 0,
            .name = std::string(symbol.name),
            .alignment = WordAlignment,
            .data = z64::EncodedData{.bytes = {symbol.bytes.begin(), symbol.bytes.end()}},
        });
    }
    for (const std::unique_ptr<FileRead>& file : files) {
        encodeFile(*file, objects);
    }
    z64::MemoryImage image = z64::layOutImage(
        std::move(objects),
        z64::LayoutOptions{.findExternal = [](const std::string_view name) -> std::optional<u32> {
            return isLinkerSymbol(name) ? std::optional<u32>(0) : std::nullopt;
        }});
    image.diagnostics.insert(image.diagnostics.begin(), diagnostics.begin(), diagnostics.end());
    return image;
}

} // namespace

z64::MemoryImage readAssetImage(const Workspace& workspace,
                                const std::span<const SegmentFiles> segments) {
    return ImageReader(workspace).read(segments);
}

} // namespace hackeroot

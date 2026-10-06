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
#include "hackeroot/FilePoller.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Types.h"
#include "hackeroot/Workspace.h"
#include "z64/CodeSymbols.h"
#include "z64/DataEncoder.h"
#include "z64/DecompDialect.h"
#include "z64/ImageTexture.h"
#include "z64/MemoryImage.h"
#include "z64/TextureEncoder.h"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
    usize image = 0;
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
        for (auto inner = declaration.innerLengths.rbegin();
             inner != declaration.innerLengths.rend(); ++inner) {
            type = prelude.types.getArray(type, *inner);
        }
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

using PreludeCache = std::map<std::vector<std::string>, std::unique_ptr<Prelude>>;

class ImageReader {
public:
    ImageReader(const Workspace& workspace, PreludeCache& preludes)
        : workspace(workspace), options(makeAssetPreprocessOptions(
                                    workspace.getRoot(), workspace.getBuildDefines().version)),
          textures(directories, options.allowedRoots), preludes(preludes) {
        for (const z64::CodeSymbol& symbol : z64::getCodeSymbols()) {
            elementSizes.emplace(std::string(symbol.name), symbol.elementSize);
        }
    }

    std::vector<z64::MemoryImage> read(std::span<const std::vector<SegmentFiles>> images,
                                       std::span<const z64::MemoryImage* const> earlier);

private:
    void readFile(usize image, u8 segment, const std::filesystem::path& file);
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
    PreludeCache& preludes;
    std::set<const Prelude*> reported;
    std::vector<std::unique_ptr<FileRead>> files;
    std::map<std::string, usize, std::less<>> elementSizes;
    std::vector<std::vector<base::Diagnostic>> imageDiagnostics;
    std::vector<base::Diagnostic>* diagnostics = nullptr;
};

Prelude& ImageReader::findPrelude(const std::vector<std::string>& includes) {
    std::unique_ptr<Prelude>& prelude = preludes[includes];
    if (!prelude) {
        prelude = std::make_unique<Prelude>(readPrelude(workspace, includes, AlsoHidden));
    }
    if (reported.insert(prelude.get()).second) {
        for (const base::Diagnostic& diagnostic : prelude->diagnostics) {
            if (diagnostic.severity != base::Severity::Note) {
                diagnostics->push_back(diagnostic);
            }
        }
    }
    return *prelude;
}

void ImageReader::readFile(const usize image, const u8 segment, const std::filesystem::path& file) {
    std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        cparser::preprocess(workspace.getMacros(), file, options);
    if (!unit) {
        diagnostics->push_back(unit.error());
        return;
    }
    auto read = std::make_unique<FileRead>(
        FileRead{.image = image, .segment = segment, .unit = std::move(*unit)});
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
            diagnostics->push_back(std::move(diagnostic));
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
                diagnostics->push_back(base::makeError(
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
    std::optional<z64::ImageTexture> texture;
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
        .encodeEmbedded = [this, &file, &texture](
                              const u32 index) -> std::expected<std::vector<u8>, std::string> {
            const cparser::EmbeddedFile& embedded = file.unit.embedded[index];
            std::expected<std::vector<u8>, std::string> bytes =
                textures.encode(embedded.path, embedded.tag);
            const auto [width, height] =
                textures.findImageSize(embedded.path).value_or(std::pair<u32, u32>(0, 0));
            texture = z64::makeImageTexture(embedded.path.filename().string(), embedded.tag, width,
                                            height);
            return bytes;
        },
    });
    for (usize i = 0; i < file.declarations.size(); ++i) {
        const cparser::Declaration& declaration = file.declarations[i];
        const cparser::CType& type = *file.types[i];
        texture.reset();
        z64::EncodedData data = encoder.encode(type, declaration.initializer, declaration.isBraced);
        objects.push_back(z64::ImageObject{
            .segment = file.segment,
            .name = std::string(declaration.name),
            .type = std::string(declaration.type),
            .isArray = declaration.isArray,
            .alignment = findAlignment(type),
            .startsFile = i == 0,
            .texture = std::move(texture),
            .data = std::move(data),
            .location = declaration.location,
        });
    }
}

std::vector<z64::MemoryImage>
ImageReader::read(const std::span<const std::vector<SegmentFiles>> images,
                  const std::span<const z64::MemoryImage* const> earlier) {
    imageDiagnostics.resize(images.size());
    for (usize image = 0; image < images.size(); ++image) {
        diagnostics = &imageDiagnostics[image];
        for (const SegmentFiles& segment : images[image]) {
            for (const std::filesystem::path& file : segment.files) {
                readFile(image, segment.segment, file);
            }
        }
    }

    std::vector<std::vector<z64::ImageObject>> objects(images.size());
    if (!objects.empty() && earlier.empty()) {
        for (const z64::CodeSymbol& symbol : z64::getCodeSymbols()) {
            objects.front().push_back(z64::ImageObject{
                .segment = 0,
                .name = std::string(symbol.name),
                .alignment = WordAlignment,
                .data = z64::EncodedData{.bytes = {symbol.bytes.begin(), symbol.bytes.end()}},
            });
        }
    }
    for (const std::unique_ptr<FileRead>& file : files) {
        diagnostics = &imageDiagnostics[file->image];
        encodeFile(*file, objects[file->image]);
    }

    std::vector<z64::MemoryImage> laidOut;
    laidOut.reserve(images.size());
    for (usize image = 0; image < images.size(); ++image) {
        z64::MemoryImage memoryImage = z64::layOutImage(
            std::move(objects[image]),
            z64::LayoutOptions{.findExternal = [&laidOut, earlier](const std::string_view name)
                                   -> std::optional<u32> {
                for (const z64::MemoryImage* const before : earlier) {
                    if (const std::optional<u32> address = before->findAddress(name)) {
                        return address;
                    }
                }
                for (const z64::MemoryImage& before : laidOut) {
                    if (const std::optional<u32> address = before.findAddress(name)) {
                        return address;
                    }
                }
                return isLinkerSymbol(name) ? std::optional<u32>(0) : std::nullopt;
            }});
        memoryImage.diagnostics.insert(memoryImage.diagnostics.begin(),
                                       imageDiagnostics[image].begin(),
                                       imageDiagnostics[image].end());
        laidOut.push_back(std::move(memoryImage));
    }
    return laidOut;
}

} // namespace

struct AssetImageReader::Caches {
    struct SharedImage {
        std::vector<SegmentFiles> files;
        std::vector<std::shared_ptr<const z64::MemoryImage>> earlier;
        FilePoller poller;
        std::shared_ptr<const z64::MemoryImage> image;
    };

    PreludeCache preludes;
    std::vector<SharedImage> sharedImages;
};

AssetImageReader::AssetImageReader(std::shared_ptr<const Workspace> workspace)
    : workspace(std::move(workspace)), caches(std::make_unique<Caches>()) {}

AssetImageReader::AssetImageReader(AssetImageReader&&) noexcept = default;

AssetImageReader& AssetImageReader::operator=(AssetImageReader&&) noexcept = default;

AssetImageReader::~AssetImageReader() = default;

std::vector<z64::MemoryImage>
AssetImageReader::read(const std::span<const std::vector<SegmentFiles>> images) {
    return ImageReader(*workspace, caches->preludes).read(images, {});
}

ImageSet AssetImageReader::read(const std::span<const SharedImageFiles> shared,
                                const std::span<const std::vector<SegmentFiles>> images) {
    ImageSet set;
    std::vector<const z64::MemoryImage*> earlier;
    for (const SharedImageFiles& files : shared) {
        auto cached =
            std::ranges::find_if(caches->sharedImages, [&files](const Caches::SharedImage& image) {
                return image.files == files.files;
            });
        if (cached == caches->sharedImages.end()) {
            cached = caches->sharedImages.insert(
                cached, Caches::SharedImage{.files = files.files, .poller = FilePoller({})});
        }
        if (!cached->image || cached->earlier != set.shared ||
            !cached->poller.findChanges().empty()) {
            cached->poller = FilePoller(files.watched);
            const std::array<std::vector<SegmentFiles>, 1> image{files.files};
            cached->image = std::make_shared<const z64::MemoryImage>(
                std::move(ImageReader(*workspace, caches->preludes).read(image, earlier).front()));
            cached->earlier = set.shared;
        }
        earlier.push_back(cached->image.get());
        set.shared.push_back(cached->image);
    }
    set.images = ImageReader(*workspace, caches->preludes).read(images, earlier);
    return set;
}

z64::MemoryImage readAssetImage(const Workspace& workspace,
                                const std::span<const SegmentFiles> segments) {
    const std::array<std::vector<SegmentFiles>, 1> images{
        std::vector<SegmentFiles>(segments.begin(), segments.end())};
    PreludeCache preludes;
    return std::move(ImageReader(workspace, preludes).read(images, {}).front());
}

} // namespace hackeroot

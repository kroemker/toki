#include "SceneSave.h"

#include "app/scene/domain/ActorEntryWriter.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneCommandWriter.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneLayout.h"
#include "app/scene/domain/SceneSourceScan.h"
#include "base/Diagnostic.h"
#include "cparser/CType.h"
#include "cparser/Expression.h"
#include "cparser/Scanner.h"
#include "cparser/SourceFile.h"
#include "cparser/TypeName.h"
#include "cparser/TypeTable.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"
#include "z64/ElementWriters.h"
#include "z64/EncodingCheck.h"
#include "z64/GbiNames.h"
#include "z64/ImageDifference.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize AddressSize = 4;
constexpr usize CommandSize = 8;
constexpr usize ListedLimit = 10;
constexpr std::array<u8, 5> AddressOpcodes{0x01, 0xDA, 0xDC, 0xDE, 0xFD};

std::string joinLines(const std::vector<std::string>& lines) {
    std::string joined;
    for (usize i = 0; i < std::min(lines.size(), ListedLimit); ++i) {
        joined += std::format("{}{}", i == 0 ? "" : "\n", lines[i]);
    }
    if (lines.size() > ListedLimit) {
        joined += std::format("\nand {} more", lines.size() - ListedLimit);
    }
    return joined;
}

std::string joinNames(const std::vector<std::string>& names) {
    constexpr usize Named = 3;
    std::string joined;
    for (usize i = 0; i < std::min(names.size(), Named); ++i) {
        joined += std::format("{}{}", i == 0 ? "" : ", ", names[i]);
    }
    if (names.size() > Named) {
        joined += std::format(" and {} more", names.size() - Named);
    }
    return joined;
}

u32 readWord(const std::span<const u8> bytes, const usize offset) {
    return (u32{bytes[offset]} << 24) | (u32{bytes[offset + 1]} << 16) |
           (u32{bytes[offset + 2]} << 8) | u32{bytes[offset + 3]};
}

class SceneSaver {
public:
    SceneSaver(const std::shared_ptr<const hackeroot::Workspace>& workspace,
               const SceneSaveInput& input)
        : root(workspace->getRoot()), reader(workspace), input(input),
          prelude(reader.getPrelude(getSceneIncludes())), gbiNames(prelude.macros) {}

    [[nodiscard]] std::expected<hackeroot::EditPlan, std::string> plan();

private:
    [[nodiscard]] const cparser::CType* findType(std::string_view name) const;
    [[nodiscard]] std::optional<z64::SymbolShape> findShape(std::string_view name) const;
    [[nodiscard]] bool isSwitchOn(std::string_view name) const;
    [[nodiscard]] z64::DeclarationWriter makeWriter() const;
    [[nodiscard]] std::vector<std::string> listLiteralAddresses() const;
    [[nodiscard]] std::vector<std::string> listObjectOverflows() const;

    /// The symbol whose name `name` starts with, followed by `_`, if one does.
    [[nodiscard]] std::optional<std::string> findDefinedSymbol(std::string_view name) const {
        for (usize end = name.rfind('_'); end != std::string_view::npos && end > 0;
             end = name.rfind('_', end - 1)) {
            if (input.model.findSymbol(name.substr(0, end)) != nullptr) {
                return std::string(name.substr(0, end));
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::expected<void, std::string> check(const WrittenScene& written);
    [[nodiscard]] std::expected<void, std::string> planSpec(const WrittenScene& written,
                                                            hackeroot::EditPlan& plan) const;

    [[nodiscard]] std::filesystem::path getReadFolder() const {
        return input.directory.lexically_relative(root);
    }

    /// Whether the scene was read from `extracted/<version>/`, so that a save
    /// copies it to `assets/`.
    [[nodiscard]] bool isVanilla() const {
        const std::filesystem::path folder = getReadFolder();
        return folder.begin() != folder.end() && *folder.begin() == "extracted";
    }

    [[nodiscard]] std::filesystem::path getFolder() const {
        return findSavedDirectory(root, input.directory).lexically_relative(root);
    }

    void planCopies(hackeroot::EditPlan& plan) const;

    std::filesystem::path root;
    hackeroot::AssetImageReader reader;
    const SceneSaveInput& input;
    const hackeroot::Prelude& prelude;
    z64::GbiNames gbiNames;
    /// The scalar types a declaration names by their words, as `unsigned int`,
    /// made as they are asked for.
    mutable cparser::TypeTable scalars;
};

const cparser::CType* SceneSaver::findType(const std::string_view name) const {
    if (const cparser::CType* type = prelude.types.findTypedef(name)) {
        return type;
    }
    for (const std::string_view keyword : {"struct ", "union "}) {
        if (name.starts_with(keyword)) {
            return prelude.types.findTag(name.substr(keyword.size()));
        }
    }
    std::vector<std::string_view> words;
    for (const auto word : std::views::split(name, ' ')) {
        if (!word.empty()) {
            words.emplace_back(word.begin(), word.end());
        }
    }
    if (const std::optional<cparser::ScalarType> scalar =
            cparser::combineScalarWords(z64::getDecompDialect(), words)) {
        return scalars.getScalar(*scalar);
    }
    return nullptr;
}

std::optional<z64::SymbolShape> SceneSaver::findShape(const std::string_view name) const {
    if (const z64::SymbolData* symbol = input.model.findSymbol(name)) {
        usize size = AddressSize;
        if (symbol->pointerDepth == 0) {
            const cparser::CType* type = findType(symbol->type);
            size = type != nullptr ? std::max<usize>(type->size, 1) : 1;
        }
        for (const usize inner : symbol->innerLengths) {
            size *= std::max<usize>(inner, 1);
        }
        return z64::SymbolShape{
            .type = symbol->type, .isArray = symbol->isArray, .elementSize = size};
    }
    if (input.model.getGameCode()) {
        if (const std::optional<z64::ImageSymbol> symbol =
                input.model.getGameCode()->findSymbol(name)) {
            const cparser::CType* type = findType(symbol->type);
            return z64::SymbolShape{.type = symbol->type,
                                    .isArray = symbol->isArray,
                                    .elementSize =
                                        type != nullptr ? std::max<usize>(type->size, 1) : 1};
        }
    }
    if (hackeroot::isLinkerSymbol(name)) {
        return z64::SymbolShape{.type = "u8", .isArray = true, .elementSize = 1};
    }
    return std::nullopt;
}

bool SceneSaver::isSwitchOn(const std::string_view name) const {
    const cparser::SourceFile file{.path = "switch.h", .contents = std::string(name)};
    const cparser::ScanResult scanned = cparser::scan(file);
    std::vector<base::Diagnostic> found;
    const std::expected<cparser::Value, base::Diagnostic> value = cparser::evaluate(
        scanned.tokens,
        cparser::EvalContext{.macros = &prelude.macros, .dialect = &z64::getDecompDialect()},
        found);
    return value && cparser::getInteger(*value) != 0;
}

z64::DeclarationWriter SceneSaver::makeWriter() const {
    const z64::DataContext data{
        .macros = &prelude.macros,
        .types = &prelude.types,
        .findSymbol = [this](const std::string_view name) -> std::optional<cparser::SymbolAddress> {
            const std::optional<z64::SymbolShape> shape = findShape(name);
            if (!shape) {
                return std::nullopt;
            }
            return cparser::SymbolAddress{.elementSize = shape->elementSize};
        }};
    return z64::DeclarationWriter(z64::DeclarationContext{
        .findType = [this](const std::string_view name) { return findType(name); },
        .findSymbol = [this](const std::string_view name) { return findShape(name); },
        .elementWriters = {{"SceneCmd", makeSceneCommandWriter(prelude.types)},
                           {"ActorEntry", makeActorEntryWriter(prelude.types)},
                           {"TransitionActorEntry", makeTransitionActorEntryWriter(prelude.types)},
                           {"Vtx", z64::makeVertexWriter()},
                           {"Gfx", z64::makeDisplayListWriter(gbiNames)}},
        .elementConditions = {{"SceneCmd", makeSceneCommandCondition(prelude.types)}},
        .isExact = z64::makeEncodingCheck(data)});
}

std::vector<std::string> SceneSaver::listLiteralAddresses() const {
    std::vector<std::string> found;
    for (const std::vector<z64::SymbolData>& symbols : input.model.getImages()) {
        for (const z64::SymbolData& symbol : symbols) {
            if (symbol.type != "Gfx" || symbol.pointerDepth != 0) {
                continue;
            }
            for (usize at = 0; at + CommandSize <= symbol.bytes.size(); at += CommandSize) {
                const u32 address = readWord(symbol.bytes, at + AddressSize);
                if (std::ranges::contains(AddressOpcodes, symbol.bytes[at]) && address != 0 &&
                    !std::ranges::contains(symbol.relocations, at + AddressSize,
                                           &z64::Relocation::offset)) {
                    found.push_back(
                        std::format("{}[{}] 0x{:08X}", symbol.name, at / CommandSize, address));
                }
            }
        }
    }
    return found;
}

std::vector<std::string> SceneSaver::listObjectOverflows() const {
    const cparser::CType* context = findType("ObjectContext");
    const auto slots =
        context != nullptr
            ? std::ranges::find(context->fields, std::string_view("slots"), &cparser::Field::name)
            : std::vector<cparser::Field>::const_iterator{};
    if (context == nullptr || slots == context->fields.end() || !slots->type->count) {
        return {};
    }
    Scene laidOut{.name = input.name, .directory = input.directory, .images = input.model.layOut()};
    readSceneLayers(laidOut);
    std::vector<std::string> found;
    std::set<std::pair<usize, std::vector<s16>>> named;
    for (const SceneLayer& layer : laidOut.layers) {
        const usize persistent = layer.header.keepObjectId != 0 ? 2 : 1;
        for (usize room = 0; room < layer.rooms.size(); ++room) {
            const std::vector<s16>& objects = layer.rooms[room].objects;
            if (objects.size() + persistent > *slots->type->count &&
                named.emplace(room, objects).second) {
                found.push_back(std::format(
                    "Room {} lists {} objects in the {} layer: with the {} keep objects that is "
                    "more than the {} slots the game loads objects into.",
                    room, objects.size(), makeLayerName(layer.index), persistent,
                    *slots->type->count));
            }
        }
    }
    return found;
}

std::expected<void, std::string> SceneSaver::check(const WrittenScene& written) {
    cparser::PendingFiles pending;
    for (const WrittenFile& file : written.files) {
        pending.emplace(cparser::canonicalize(root / getFolder() / file.name), file.text);
    }
    std::vector<std::vector<hackeroot::SegmentFiles>> images;
    for (usize image = 0; image < written.segments.size(); ++image) {
        hackeroot::SegmentFiles segment{.segment = static_cast<u8>(image == 0 ? 2 : 3)};
        for (const std::string& stem : written.segments[image]) {
            segment.files.push_back(root / getFolder() / (stem + ".c"));
        }
        images.push_back({std::move(segment)});
    }
    const std::array shared{hackeroot::makeSharedGameCode(root)};
    const hackeroot::ImageSet read = reader.read(shared, images, &pending);
    const std::vector<std::shared_ptr<const z64::MemoryImage>> expected = input.model.layOut();
    std::vector<std::string> problems;
    for (usize image = 0; image < read.images.size(); ++image) {
        for (const base::Diagnostic& diagnostic : read.images[image].diagnostics) {
            if (diagnostic.severity == base::Severity::Error) {
                problems.push_back(diagnostic.message);
            }
        }
        const u8 segment = images[image].front().segment;
        for (const z64::ImageDifference& difference :
             z64::compareSymbols(*expected[image + 1], read.images[image], segment)) {
            problems.push_back(std::format("'{}' {}", difference.symbol, difference.message));
        }
    }
    if (!problems.empty()) {
        return std::unexpected(std::format(
            "The scene was not saved: written out, it would not read back the same.\n{}",
            joinLines(problems)));
    }
    return {};
}

std::expected<void, std::string> SceneSaver::planSpec(const WrittenScene& written,
                                                      hackeroot::EditPlan& plan) const {
    std::vector<std::filesystem::path> specs{root / "spec" / "spec"};
    std::error_code status;
    for (const auto& entry : std::filesystem::directory_iterator(root / "spec", status)) {
        if (entry.path().extension() == ".inc") {
            specs.push_back(entry.path());
        }
    }
    bool hasScene = false;
    for (const std::filesystem::path& spec : specs) {
        std::expected<std::string, std::string> text = hackeroot::readCheckoutText(spec);
        if (!text) {
            continue;
        }
        hackeroot::FileEdit edit{
            .file = spec.lexically_relative(root), .before = *text, .after = *text};
        for (usize image = 0; image < written.segments.size(); ++image) {
            const std::string segment = image == 0
                                            ? std::format("{}_scene", input.name)
                                            : std::format("{}_room_{}", input.name, image - 1);
            std::vector<std::string> objects;
            for (const std::string& stem : written.segments[image]) {
                objects.push_back((getFolder() / (stem + ".o")).generic_string());
            }
            if (const std::optional<hackeroot::EditStatus> segmentStatus =
                    hackeroot::setSpecSegmentIncludes(edit.after, segment, objects)) {
                hasScene = hasScene || image == 0;
                edit.items.push_back(hackeroot::EditItem{
                    .status = *segmentStatus, .label = std::format("segment \"{}\"", segment)});
            }
        }
        if (!edit.items.empty()) {
            plan.files.push_back(std::move(edit));
        }
    }
    if (!hasScene) {
        return std::unexpected(std::format(
            "The scene was not saved: no spec file has a segment named \"{}_scene\"", input.name));
    }
    return {};
}

void SceneSaver::planCopies(hackeroot::EditPlan& plan) const {
    std::set<std::filesystem::path> copied;
    for (const std::vector<z64::SymbolData>& symbols : input.model.getImages()) {
        for (const z64::SymbolData& symbol : symbols) {
            if (!symbol.embedded) {
                continue;
            }
            const std::filesystem::path& from = symbol.embedded->path;
            const std::filesystem::path within = from.lexically_relative(input.directory);
            if (within.empty() || *within.begin() == ".." || !copied.insert(from).second) {
                continue;
            }
            plan.copies.push_back(hackeroot::makeFileCopy(root, from, getFolder() / within));
        }
    }
}

std::expected<hackeroot::EditPlan, std::string> SceneSaver::plan() {
    std::vector<SceneSource> sources;
    for (const std::filesystem::path& file : input.files) {
        if (std::expected<std::string, std::string> text = hackeroot::readCheckoutText(file)) {
            sources.push_back(SceneSource{.file = file.lexically_relative(root), .text = *text});
        }
    }
    SceneSourceScan scan =
        scanSceneSources(sources, [this](const std::string_view name) { return isSwitchOn(name); });
    if (!scan.refusals.empty()) {
        return std::unexpected(
            std::format("The scene was not saved.\n{}", joinLines(scan.refusals)));
    }
    std::map<std::string, std::vector<std::string>, std::less<>> symbolDefines;
    std::map<std::filesystem::path, std::vector<std::string>> lostDefines;
    for (const SourceDefine& define : scan.defines) {
        if (const std::optional<std::string> symbol = findDefinedSymbol(define.name);
            symbol && !define.line.empty()) {
            symbolDefines[*symbol].push_back(define.line);
        } else {
            lostDefines[define.file].push_back(define.name);
        }
    }
    for (const auto& [file, names] : lostDefines) {
        scan.warnings.push_back(std::format("{} defines {}, which a save does not write again",
                                            file.generic_string(), joinNames(names)));
    }
    const z64::DeclarationWriter writer = makeWriter();
    const SceneLayout layout = assignSceneFiles(input.model, prelude.types);
    const WrittenScene written =
        writeSceneFiles(input.model, layout,
                        SceneWriting{.name = input.name,
                                     .includes = getSceneIncludes(),
                                     .writer = &writer,
                                     .lengthDefines = std::move(scan.lengthDefines),
                                     .symbolDefines = std::move(symbolDefines),
                                     .findExternal = [this](const std::string_view name) {
                                         return findShape(name);
                                     }});
    std::vector<std::string> errors;
    for (const base::Diagnostic& diagnostic : written.diagnostics) {
        if (diagnostic.severity == base::Severity::Error) {
            errors.push_back(diagnostic.message);
        }
    }
    if (!errors.empty()) {
        return std::unexpected(std::format("The scene was not saved.\n{}", joinLines(errors)));
    }
    if (std::expected<void, std::string> checked = check(written); !checked) {
        return std::unexpected(checked.error());
    }

    hackeroot::EditPlan plan;
    std::set<std::filesystem::path> kept;
    for (const WrittenFile& file : written.files) {
        const std::filesystem::path path = getFolder() / file.name;
        kept.insert(path);
        std::expected<std::string, std::string> before = hackeroot::readCheckoutText(root / path);
        plan.files.push_back(
            hackeroot::FileEdit{.file = path,
                                .before = before ? std::optional(std::move(*before)) : std::nullopt,
                                .after = file.text});
    }
    for (const SceneSource& source : sources) {
        if (!isVanilla() && !kept.contains(source.file)) {
            plan.files.push_back(
                hackeroot::FileEdit{.file = source.file, .before = source.text, .isRemoved = true});
        }
    }
    if (std::expected<void, std::string> spec = planSpec(written, plan); !spec) {
        return std::unexpected(spec.error());
    }
    if (isVanilla()) {
        planCopies(plan);
    }
    plan.warnings = std::move(scan.warnings);
    if (isVanilla()) {
        plan.warnings.insert(
            plan.warnings.begin(),
            std::format("{} is a vanilla scene. Saving it writes a copy to {}, textures and all, "
                        "and the build uses that copy from now on; the extracted scene stays as "
                        "it is.",
                        input.name, getFolder().generic_string()));
    } else if (!scan.isTokiScene) {
        plan.warnings.push_back(
            "This scene was exported by fast64. Once Toki saves it, Toki owns it: exporting it "
            "from Blender again replaces everything Toki changed, so edit it in Toki from now "
            "on.");
    }
    std::ranges::move(listObjectOverflows(), std::back_inserter(plan.warnings));
    if (const std::vector<std::string> literal = listLiteralAddresses(); !literal.empty()) {
        plan.warnings.push_back(std::format(
            "{} display list commands point at an address written as a number, not a symbol, and "
            "are written as numbers again; they point elsewhere once what is there moves: {}",
            literal.size(), literal.front()));
    }
    return plan;
}

} // namespace

std::filesystem::path findSavedDirectory(const std::filesystem::path& root,
                                         const std::filesystem::path& directory) {
    const std::filesystem::path folder = directory.lexically_relative(root);
    if (folder.begin() == folder.end() || *folder.begin() != "extracted") {
        return directory;
    }
    std::filesystem::path saved = root;
    usize index = 0;
    for (const std::filesystem::path& part : folder) {
        if (index++ >= 2) {
            saved /= part;
        }
    }
    return saved;
}

const std::vector<std::string>& getSceneIncludes() {
    static const std::vector<std::string> Includes{
        "ultra64.h",     "config.h", "scene.h",    "cutscene.h",
        "room.h",        "player.h", "camera.h",   "object.h",
        "ocarina.h",     "sfx.h",    "sequence.h", "animated_materials.h",
        "array_count.h", "save.h",   "actor.h",    "skybox.h"};
    return Includes;
}

std::expected<hackeroot::EditPlan, std::string>
planSceneSave(const std::shared_ptr<const hackeroot::Workspace>& workspace,
              const SceneSaveInput& input) {
    return SceneSaver(workspace, input).plan();
}

} // namespace toki::scene

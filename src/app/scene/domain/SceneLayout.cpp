#include "SceneLayout.h"

#include "app/scene/domain/SceneCommandWriter.h"
#include "app/scene/domain/SceneModel.h"
#include "cparser/TypeTable.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/CheckoutEdits.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize CommandSize = 8;

struct CommandCodes {
    std::optional<s64> collision;
    std::optional<s64> cutscene;
    std::optional<s64> roomShape;
    z64::ElementCondition findCondition;
};

class ImagePlacer {
public:
    ImagePlacer(const std::vector<z64::SymbolData>& symbols, const CommandCodes& codes,
                const bool isScene, usize& cutsceneCount)
        : symbols(symbols), codes(codes), isScene(isScene), cutsceneCount(cutsceneCount),
          places(symbols.size()), isPlaced(symbols.size(), false) {
        for (usize index = 0; index < symbols.size(); ++index) {
            indices.emplace(symbols[index].name, index);
        }
    }

    std::vector<SymbolPlace> place() {
        for (usize index = 0; index < symbols.size(); ++index) {
            if (symbols[index].offset == 0) {
                visit(index, SymbolPlace{});
            }
        }
        return std::move(places);
    }

private:
    void visit(usize index, SymbolPlace place);
    [[nodiscard]] SymbolPlace followCommand(const z64::SymbolData& commands, usize offset,
                                            const SymbolPlace& from, usize target);

    const std::vector<z64::SymbolData>& symbols;
    const CommandCodes& codes;
    const bool isScene;
    usize& cutsceneCount;
    std::map<std::string, usize, std::less<>> indices;
    std::vector<SymbolPlace> places;
    std::vector<bool> isPlaced;
};

SymbolPlace ImagePlacer::followCommand(const z64::SymbolData& commands, const usize offset,
                                       const SymbolPlace& from, const usize target) {
    SymbolPlace next = from;
    const usize start = offset - (offset % CommandSize);
    if (start + CommandSize > commands.bytes.size()) {
        return next;
    }
    const s64 code = commands.bytes[start];
    if (code == codes.collision && isScene) {
        next.part = FilePart::Collision;
    } else if (code == codes.cutscene && isScene && !isPlaced[target]) {
        next.part = FilePart::Cutscene;
        next.cutscene = cutsceneCount++;
    } else if (code == codes.roomShape && !isScene) {
        next.part = FilePart::ModelInfo;
    }
    if (codes.findCondition) {
        const z64::ElementData command{.bytes =
                                           std::span(commands.bytes).subspan(start, CommandSize)};
        if (std::optional<std::string> condition = codes.findCondition(command)) {
            next.condition = std::move(condition);
        }
    }
    return next;
}

void ImagePlacer::visit(const usize index, SymbolPlace place) {
    if (isPlaced[index]) {
        return;
    }
    const z64::SymbolData& symbol = symbols[index];
    if (place.part == FilePart::ModelInfo && symbol.type == "Gfx") {
        place.part = FilePart::Model;
    }
    isPlaced[index] = true;
    places[index] = place;
    const bool isCommands = symbol.type == "SceneCmd" && symbol.pointerDepth == 0;
    for (const z64::Relocation& relocation : symbol.relocations) {
        const auto found = indices.find(relocation.symbol);
        if (found == indices.end() || isPlaced[found->second]) {
            continue;
        }
        visit(found->second,
              isCommands ? followCommand(symbol, relocation.offset, place, found->second) : place);
    }
}

std::string makeGuardName(const std::string_view name) {
    std::string guard;
    for (const char c : name) {
        guard += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return guard + "_H";
}

std::string wrapCondition(const std::optional<std::string>& condition, std::string text) {
    return condition ? std::format("#if {}\n{}#endif\n", *condition, text) : text;
}

std::string makeFileSuffix(const SymbolPlace& place) {
    switch (place.part) {
    case FilePart::Main:
        return "_main";
    case FilePart::Collision:
        return "_col";
    case FilePart::Cutscene:
        return std::format("_cs_{}", place.cutscene);
    case FilePart::ModelInfo:
        return "_model_info";
    case FilePart::Model:
        return "_model";
    }
    return "_main";
}

class SceneWriter {
public:
    SceneWriter(const SceneModel& model, const SceneLayout& layout, const SceneWriting& writing)
        : model(model), layout(layout), writing(writing) {}

    WrittenScene write();

private:
    [[nodiscard]] std::string makeBase(usize image) const {
        return image == 0 ? std::format("{}_scene", writing.name)
                          : std::format("{}_room_{}", writing.name, image - 1);
    }

    void writeSources(usize image);
    [[nodiscard]] std::string writeDeclarations(usize image);
    [[nodiscard]] std::string writeOutsideDeclarations();
    void writeHeader(usize image);

    const SceneModel& model;
    const SceneLayout& layout;
    const SceneWriting& writing;
    WrittenScene written;
};

void SceneWriter::writeSources(const usize image) {
    const std::vector<z64::SymbolData>& symbols = model.getImages()[image];
    const std::vector<SymbolPlace>& places = layout.places[image];
    std::vector<std::string> suffixes{"_main"};
    if (image == 0) {
        suffixes.emplace_back("_col");
        for (usize cutscene = 0; cutscene < layout.cutsceneCount; ++cutscene) {
            suffixes.push_back(std::format("_cs_{}", cutscene));
        }
    } else {
        suffixes.emplace_back("_model_info");
        suffixes.emplace_back("_model");
    }
    const std::string base = makeBase(image);
    std::vector<std::string>& segment = written.segments.emplace_back();
    for (const std::string& suffix : suffixes) {
        std::string body;
        for (usize index = 0; index < symbols.size(); ++index) {
            if (makeFileSuffix(places[index]) != suffix) {
                continue;
            }
            z64::WrittenText definition = writing.writer->writeDefinition(symbols[index]);
            std::ranges::move(definition.diagnostics, std::back_inserter(written.diagnostics));
            body += std::format("\n{}",
                                wrapCondition(places[index].condition, std::move(definition.text)));
        }
        if (body.empty() && suffix != "_main") {
            continue;
        }
        std::string text;
        if (image == 0 && suffix == "_main") {
            text += hackeroot::makeTokiMarker("scene") + "\n";
        }
        text += std::format("#include \"{}_scene.h\"\n", writing.name);
        if (image != 0) {
            text += std::format("#include \"{}.h\"\n", base);
        }
        segment.push_back(base + suffix);
        written.files.push_back(WrittenFile{.name = base + suffix + ".c", .text = text + body});
    }
}

std::string SceneWriter::writeDeclarations(const usize image) {
    std::string text;
    const std::vector<z64::SymbolData>& symbols = model.getImages()[image];
    for (usize index = 0; index < symbols.size(); ++index) {
        std::string declaration;
        if (writing.lengthDefines.contains(symbols[index].name)) {
            declaration += std::format("#define LENGTH_{} {}\n", symbols[index].name,
                                       writing.writer->countElements(symbols[index]));
        }
        declaration += writing.writer->writeExtern(symbols[index]);
        text += wrapCondition(layout.places[image][index].condition, std::move(declaration));
    }
    return text;
}

std::string SceneWriter::writeOutsideDeclarations() {
    std::set<std::string, std::less<>> outside;
    for (const std::vector<z64::SymbolData>& symbols : model.getImages()) {
        for (const z64::SymbolData& symbol : symbols) {
            for (const z64::Relocation& relocation : symbol.relocations) {
                if (model.findSymbol(relocation.symbol) == nullptr) {
                    outside.insert(relocation.symbol);
                }
            }
        }
    }
    std::string text;
    for (const std::string& name : outside) {
        if (hackeroot::isLinkerSymbol(name)) {
            text += std::format("extern u8 {}[];\n", name);
            continue;
        }
        const std::optional<z64::SymbolShape> shape =
            writing.findExternal ? writing.findExternal(name) : std::nullopt;
        if (!shape) {
            written.diagnostics.push_back(base::makeError(
                {}, std::format("the scene points at '{}', which neither the scene nor the "
                                "game's code defines",
                                name)));
            continue;
        }
        text += std::format("extern {} {}{};\n", shape->type, name, shape->isArray ? "[]" : "");
    }
    return text;
}

void SceneWriter::writeHeader(const usize image) {
    const std::string base = makeBase(image);
    const std::string guard = makeGuardName(base);
    std::string text = std::format("#ifndef {0}\n#define {0}\n\n", guard);
    if (image == 0) {
        for (const std::string& include : writing.includes) {
            text += std::format("#include \"{}\"\n", include);
        }
    } else {
        text += std::format("#include \"{}_scene.h\"\n", writing.name);
    }
    text += "\n" + writeDeclarations(image);
    if (image == 0) {
        text += writeOutsideDeclarations();
    }
    text += "\n#endif\n";
    written.files.push_back(WrittenFile{.name = base + ".h", .text = std::move(text)});
}

WrittenScene SceneWriter::write() {
    for (usize image = 0; image < model.getImages().size(); ++image) {
        writeHeader(image);
        writeSources(image);
    }
    return std::move(written);
}

} // namespace

SceneLayout assignSceneFiles(const SceneModel& model, const cparser::TypeTable& types) {
    const CommandCodes codes{
        .collision = types.findEnumerator("SCENE_CMD_ID_COLLISION_HEADER"),
        .cutscene = types.findEnumerator("SCENE_CMD_ID_CUTSCENE_DATA"),
        .roomShape = types.findEnumerator("SCENE_CMD_ID_ROOM_SHAPE"),
        .findCondition = makeSceneCommandCondition(types),
    };
    SceneLayout layout;
    for (usize image = 0; image < model.getImages().size(); ++image) {
        ImagePlacer placer(model.getImages()[image], codes, image == 0, layout.cutsceneCount);
        layout.places.push_back(placer.place());
    }
    return layout;
}

WrittenScene writeSceneFiles(const SceneModel& model, const SceneLayout& layout,
                             const SceneWriting& writing) {
    return SceneWriter(model, layout, writing).write();
}

} // namespace toki::scene

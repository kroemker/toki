#include "SceneCommandWriter.h"

#include "app/scene/Types.h"
#include "cparser/CType.h"
#include "cparser/TypeTable.h"
#include "z64/Relocation.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize CommandSize = 8;
constexpr usize WordOffset = 4;

enum class Argument : u8 {
    Data1,
    Pointer,
    Word,
    Byte0,
    Byte1,
    Byte2,
    Byte3,
    ShowInvisibleActors,
    DisableWarpSongs,
};

struct ArgumentSpec {
    Argument argument = Argument::Data1;
    std::string_view enumeration;
};

struct CommandSpec {
    std::string_view id;
    std::string_view macro;
    std::vector<ArgumentSpec> arguments;
};

std::vector<CommandSpec> makeCommandSpecs() {
    using enum Argument;
    return {
        {"PLAYER_ENTRY_LIST", "SCENE_CMD_PLAYER_ENTRY_LIST", {{Data1}, {Pointer}}},
        {"ACTOR_LIST", "SCENE_CMD_ACTOR_LIST", {{Data1}, {Pointer}}},
        {"UNUSED_2", "SCENE_CMD_UNUSED_02", {{Data1}, {Pointer}}},
        {"COLLISION_HEADER", "SCENE_CMD_COL_HEADER", {{Pointer}}},
        {"ROOM_LIST", "SCENE_CMD_ROOM_LIST", {{Data1}, {Pointer}}},
        {"WIND_SETTINGS", "SCENE_CMD_WIND_SETTINGS", {{Byte0}, {Byte1}, {Byte2}, {Byte3}}},
        {"SPAWN_LIST", "SCENE_CMD_SPAWN_LIST", {{Pointer}}},
        {"SPECIAL_FILES",
         "SCENE_CMD_SPECIAL_FILES",
         {{Data1, "NaviQuestHintFileId"}, {Word, "ObjectId"}}},
        {"ROOM_BEHAVIOR",
         "SCENE_CMD_ROOM_BEHAVIOR",
         {{Data1, "RoomType"},
          {Byte3, "RoomEnvironmentType"},
          {ShowInvisibleActors},
          {DisableWarpSongs}}},
        {"UNDEFINED_9", "SCENE_CMD_UNK_09", {}},
        {"ROOM_SHAPE", "SCENE_CMD_ROOM_SHAPE", {{Pointer}}},
        {"OBJECT_LIST", "SCENE_CMD_OBJECT_LIST", {{Data1}, {Pointer}}},
        {"PATH_LIST", "SCENE_CMD_PATH_LIST", {{Pointer}}},
        {"TRANSITION_ACTOR_LIST", "SCENE_CMD_TRANSITION_ACTOR_LIST", {{Data1}, {Pointer}}},
        {"LIGHT_SETTINGS_LIST", "SCENE_CMD_ENV_LIGHT_SETTINGS", {{Data1}, {Pointer}}},
        {"TIME_SETTINGS", "SCENE_CMD_TIME_SETTINGS", {{Byte0}, {Byte1}, {Byte2}}},
        {"SKYBOX_SETTINGS", "SCENE_CMD_SKYBOX_SETTINGS", {{Byte0, "SkyboxId"}, {Byte1}, {Byte2}}},
        {"SKYBOX_DISABLES", "SCENE_CMD_SKYBOX_DISABLES", {{Byte0}, {Byte1}}},
        {"EXIT_LIST", "SCENE_CMD_EXIT_LIST", {{Pointer}}},
        {"END", "SCENE_CMD_END", {}},
        {"SOUND_SETTINGS",
         "SCENE_CMD_SOUND_SETTINGS",
         {{Data1}, {Byte2, "NatureAmbienceId"}, {Byte3}}},
        {"ECHO_SETTINGS", "SCENE_CMD_ECHO_SETTINGS", {{Byte3}}},
        {"CUTSCENE_DATA", "SCENE_CMD_CUTSCENE_DATA", {{Pointer}}},
        {"ALTERNATE_HEADER_LIST", "SCENE_CMD_ALTERNATE_HEADER_LIST", {{Pointer}}},
        {"MISC_SETTINGS", "SCENE_CMD_MISC_SETTINGS", {{Data1}, {Word}}},
        {"OCC_PLANE_CAND_LIST", "SCENE_CMD_OCCLUSION_PLANE_CANDIDATES_LIST", {{Data1}, {Pointer}}},
        {"ANIMATED_MATERIAL_LIST", "SCENE_CMD_ANIMATED_MATERIAL_LIST", {{Pointer}, {Data1}}},
        {"ACTOR_CUTSCENE_LIST", "SCENE_CMD_ACTOR_CUTSCENE_LIST", {{Data1}, {Pointer}}},
        {"ACTOR_CUTSCENE_CAM_LIST", "SCENE_CMD_ACTOR_CUTSCENE_CAM_LIST", {{Data1}, {Pointer}}},
        {"TITLE_CARD", "SCENE_CMD_TITLE_CARD", {{Pointer}}},
    };
}

u32 readWord(const std::span<const u8> bytes) {
    u32 word = 0;
    for (usize i = WordOffset; i < CommandSize; ++i) {
        word = (word << 8) | bytes[i];
    }
    return word;
}

u32 readArgument(const Argument argument, const u8 data1, const u32 word) {
    switch (argument) {
    case Argument::Data1:
        return data1;
    case Argument::Byte0:
        return word >> 24;
    case Argument::Byte1:
        return (word >> 16) & 0xFF;
    case Argument::Byte2:
        return (word >> 8) & 0xFF;
    case Argument::Byte3:
        return word & 0xFF;
    case Argument::ShowInvisibleActors:
        return (word >> 8) & 1;
    case Argument::DisableWarpSongs:
        return (word >> 10) & 1;
    case Argument::Pointer:
    case Argument::Word:
        return word;
    }
    return 0;
}

class SceneCommandWriter {
public:
    explicit SceneCommandWriter(const cparser::TypeTable& types) : types(&types) {
        for (CommandSpec& spec : makeCommandSpecs()) {
            if (const std::optional<s64> code =
                    types.findEnumerator(std::format("SCENE_CMD_ID_{}", spec.id))) {
                specs.emplace(static_cast<u8>(*code), std::move(spec));
            }
        }
    }

    [[nodiscard]] std::optional<std::string> write(const z64::DeclarationWriter& writer,
                                                   const z64::ElementData& element) const;

private:
    [[nodiscard]] std::string writeName(std::string_view enumeration, u32 value) const;

    const cparser::TypeTable* types;
    std::map<u8, CommandSpec> specs;
};

std::string SceneCommandWriter::writeName(const std::string_view enumeration,
                                          const u32 value) const {
    if (const cparser::Enumeration* found =
            enumeration.empty() ? nullptr : types->findEnumeration(enumeration)) {
        const auto constant = std::ranges::find(found->constants, static_cast<s64>(value),
                                                &cparser::Enumerator::value);
        if (constant != found->constants.end()) {
            return constant->name;
        }
    }
    return std::format("{}", value);
}

std::optional<std::string> SceneCommandWriter::write(const z64::DeclarationWriter& writer,
                                                     const z64::ElementData& element) const {
    if (element.bytes.size() != CommandSize) {
        return std::nullopt;
    }
    const auto found = specs.find(element.bytes[0]);
    if (found == specs.end()) {
        return std::nullopt;
    }
    static const cparser::CType VoidType{.kind = cparser::TypeKind::Void};
    static const cparser::CType PointerType{
        .kind = cparser::TypeKind::Pointer, .size = 4, .alignment = 4, .element = &VoidType};
    const u8 data1 = element.bytes[1];
    const u32 word = readWord(element.bytes);
    std::string text = std::format("{}(", found->second.macro);
    for (usize i = 0; i < found->second.arguments.size(); ++i) {
        const ArgumentSpec& spec = found->second.arguments[i];
        text += i == 0 ? "" : ", ";
        if (spec.argument != Argument::Pointer) {
            text += writeName(spec.enumeration, readArgument(spec.argument, data1, word));
        } else if (const z64::Relocation* relocation = element.findRelocation(WordOffset)) {
            text += writer.writeAddress(*relocation, word, PointerType);
        } else {
            text += word == 0 ? "NULL" : std::format("0x{:08X}", word);
        }
    }
    return text + ")";
}

} // namespace

z64::ElementWriter makeSceneCommandWriter(const cparser::TypeTable& types) {
    return [writer = SceneCommandWriter(types)](const z64::DeclarationWriter& declarations,
                                                const z64::ElementData& element) {
        return writer.write(declarations, element);
    };
}

} // namespace toki::scene

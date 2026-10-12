#include "ActorTemplate.h"

#include "app/tools/Types.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Tables.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace toki::tools {

namespace {

constexpr std::string_view ActorTableFile = "include/tables/actor_table.h";
constexpr std::string_view ObjectTableFile = "include/tables/object_table.h";
constexpr std::string_view SpecFile = "spec/spec";
constexpr std::string_view OverlaysFolder = "src/overlays/actors";
constexpr std::string_view OverlayAnchor = "beginseg\n    name \"gameplay_keep\"";
constexpr std::string_view ObjectAnchor = "beginseg\n    name \"object_box\"";
constexpr std::string_view CollisionPlaceholder = "<INSERT_COLLISION_HEADER_HERE>";

constexpr std::array<std::string_view, 17> CommonIncludes{
    "libc64/qrand.h", "array_count.h", "gfx.h",        "gfx_setupdl.h",
    "ichain.h",       "printf.h",      "rand.h",       "segmented_address.h",
    "sfx.h",          "sys_matrix.h",  "terminal.h",   "translation.h",
    "z_lib.h",        "effect.h",      "play_state.h", "player.h",
    "save.h"};

[[nodiscard]] std::string makeUpper(const std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](const char letter) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    });
    return upper;
}

[[nodiscard]] std::string makeLower(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](const char letter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    });
    return lower;
}

[[nodiscard]] bool isIdentifier(const std::string_view name) {
    return !name.empty() && std::isdigit(static_cast<unsigned char>(name.front())) == 0 &&
           std::ranges::all_of(name, [](const char letter) {
               return std::isalnum(static_cast<unsigned char>(letter)) != 0 || letter == '_';
           });
}

[[nodiscard]] std::string makeTypeName(const std::string_view name) {
    std::string type;
    std::ranges::copy_if(name, std::back_inserter(type),
                         [](const char letter) { return letter != '_'; });
    return type;
}

[[nodiscard]] std::string makeActorEnum(const std::string_view name) {
    return "ACTOR_" + makeUpper(name);
}

[[nodiscard]] std::string makeFileStem(const std::string_view name) {
    return "z_" + makeLower(name);
}

[[nodiscard]] std::filesystem::path makeOverlayFolder(const std::string_view name) {
    return std::filesystem::path(OverlaysFolder) / std::format("ovl_{}", name);
}

[[nodiscard]] std::string findObjectName(const ActorDraft& draft) {
    return draft.newObject.value_or(draft.object);
}

[[nodiscard]] std::string findObjectEnum(const ActorDraft& draft, const hackeroot::Tables& tables) {
    if (draft.newObject) {
        return makeUpper(*draft.newObject);
    }
    const auto found =
        std::ranges::find(tables.objects, draft.object, &hackeroot::ObjectEntry::name);
    return found != tables.objects.end() ? found->enumName : std::string("OBJECT_GAMEPLAY_KEEP");
}

[[nodiscard]] std::string makeFlags(const ActorDraft& draft) {
    if (draft.flags.empty()) {
        return "0";
    }
    std::string flags;
    for (const std::string& flag : draft.flags) {
        flags += flags.empty() ? "" : " | ";
        flags += flag;
    }
    return flags;
}

[[nodiscard]] std::string makeOverlaySegment(const std::string_view name) {
    const std::filesystem::path folder = makeOverlayFolder(name);
    return std::format("beginseg\n    name \"ovl_{0}\"\n    compress\n"
                       "    include \"$(BUILD_DIR)/{1}/{2}.o\"\n"
                       "    include \"$(BUILD_DIR)/{1}/ovl_{0}_reloc.o\"\nendseg",
                       name, folder.generic_string(), makeFileStem(name));
}

[[nodiscard]] std::string makeObjectSegment(const std::string_view object) {
    return std::format("beginseg\n    name \"{0}\"\n    compress\n    romalign 0x1000\n"
                       "    include \"$(BUILD_DIR)/assets/objects/{0}/{0}.o\"\n"
                       "    number 6\nendseg",
                       object);
}

[[nodiscard]] std::string makeFunctions(const ActorDraft& draft) {
    const std::string type = makeTypeName(draft.name);
    std::string source = std::format(
        "void {0}_SetupAction({0}* this, PlayState* play, {0}ActionFunc actionFunc) {{\n"
        "    this->actionFunc = actionFunc;\n}}\n\n",
        type);
    source += std::format("void {0}_Init(Actor* thisx, PlayState* play) {{\n"
                          "    {0}* this = ({0}*)thisx;\n",
                          type);
    if (draft.isDynaPoly) {
        std::string transform;
        if (draft.movesCollision) {
            transform = "DYNA_TRANSFORM_POS";
        }
        if (draft.turnsCollision) {
            transform += transform.empty() ? "DYNA_TRANSFORM_ROT_Y" : " | DYNA_TRANSFORM_ROT_Y";
        }
        source += std::format("    CollisionHeader* colHeader = NULL;\n\n"
                              "    DynaPolyActor_Init(&this->dyna, {});\n"
                              "    CollisionHeader_GetVirtual(&{}, &colHeader);\n"
                              "    this->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, "
                              "&this->dyna.actor, colHeader);\n",
                              transform, CollisionPlaceholder);
    }
    source += "}\n\n";
    source += std::format("void {0}_Destroy(Actor* thisx, PlayState* play) {{\n"
                          "    {0}* this = ({0}*)thisx;\n",
                          type);
    if (draft.isDynaPoly) {
        source += "    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, this->dyna.bgId);\n";
    }
    source += "}\n\n";
    source += std::format("void {0}_Update(Actor* thisx, PlayState* play) {{\n"
                          "    {0}* this = ({0}*)thisx;\n}}\n",
                          type);
    if (draft.draws) {
        source += std::format("\nvoid {0}_Draw(Actor* thisx, PlayState* play) {{\n"
                              "    {0}* this = ({0}*)thisx;\n}}\n",
                              type);
    }
    return source;
}

[[nodiscard]] std::expected<hackeroot::FileEdit, std::string>
openFile(const std::filesystem::path& repository, const std::filesystem::path& file) {
    std::expected<std::string, std::string> text = hackeroot::readCheckoutText(repository / file);
    if (!text) {
        return std::unexpected(text.error());
    }
    return hackeroot::FileEdit{.file = file, .before = *text, .after = *text};
}

[[nodiscard]] hackeroot::EditItem
putTableEntry(std::string& table, const std::string& entry, const std::string& call,
              const std::string_view unsetMacro, const std::vector<std::string_view>& kept,
              const bool takesUnsetSlot, const std::string_view title, const std::string& label) {
    if (table.contains(call)) {
        return hackeroot::EditItem{.status = hackeroot::EditStatus::Present, .label = label};
    }
    if (takesUnsetSlot && hackeroot::replaceUnsetEntry(table, unsetMacro, entry, kept)) {
        return hackeroot::EditItem{.status = hackeroot::EditStatus::Add,
                                   .label = label + ", in an unused slot"};
    }
    hackeroot::addToManagedBlock(table, title, entry, "#if TESTSUITE_MODE");
    return hackeroot::EditItem{.status = hackeroot::EditStatus::Add, .label = label};
}

} // namespace

std::expected<ActorChoices, std::string> readActorChoices(const std::filesystem::path& repository) {
    const std::expected<std::string, std::string> actor =
        hackeroot::readCheckoutText(repository / "include/actor.h");
    const std::expected<std::string, std::string> overlays =
        hackeroot::readCheckoutText(repository / "include/z_actor_dlftbls.h");
    if (!actor) {
        return std::unexpected(actor.error());
    }
    if (!overlays) {
        return std::unexpected(overlays.error());
    }
    return ActorChoices{.flags = hackeroot::listDocumentedDefines(*actor, "ACTOR_FLAG_"),
                        .allocations =
                            hackeroot::listDocumentedDefines(*overlays, "ACTOROVL_ALLOC_")};
}

std::optional<std::string> findDraftProblem(const ActorDraft& draft,
                                            const hackeroot::Tables& tables) {
    if (!isIdentifier(draft.name) ||
        std::isupper(static_cast<unsigned char>(draft.name.front())) == 0) {
        return std::format("'{}' is not a name an actor can have: it is a C identifier that "
                           "starts with a capital, as En_Foo",
                           draft.name);
    }
    const bool isTaken =
        std::ranges::any_of(tables.actors, [&](const hackeroot::ActorEntry& actor) {
            return actor.name == draft.name || actor.enumName == makeActorEnum(draft.name);
        });
    if (isTaken) {
        return std::format("the actor table has {} already", draft.name);
    }
    if (draft.newObject) {
        if (!isIdentifier(*draft.newObject) || !draft.newObject->starts_with("object_")) {
            return std::format("'{}' is not a name an object can have, as object_foo",
                               *draft.newObject);
        }
        if (std::ranges::contains(tables.objects, *draft.newObject,
                                  &hackeroot::ObjectEntry::name)) {
            return std::format("the object table has {} already", *draft.newObject);
        }
    } else if (!std::ranges::contains(tables.objects, draft.object,
                                      &hackeroot::ObjectEntry::name)) {
        return std::format("the object table has no {}", draft.object);
    }
    if (draft.description.contains("*/")) {
        return std::string("a description cannot hold */, which would end the comment it is "
                           "written in");
    }
    if (draft.isDynaPoly && !draft.movesCollision && !draft.turnsCollision) {
        return std::string("collision has to move with the actor, turn with it, or both");
    }
    return std::nullopt;
}

std::string makeActorHeader(const ActorDraft& draft) {
    const std::string type = makeTypeName(draft.name);
    const std::string guard = makeUpper(makeFileStem(draft.name)) + "_H";
    return std::format("#ifndef {0}\n#define {0}\n\n"
                       "#include \"ultra64.h\"\n#include \"actor.h\"\n\n"
                       "struct {1};\n\n"
                       "typedef void (*{1}ActionFunc)(struct {1}*, struct PlayState*);\n\n"
                       "typedef struct {1} {{\n"
                       "    {2};\n"
                       "    {1}ActionFunc actionFunc;\n"
                       "}} {1};\n\n#endif\n",
                       guard, type, draft.isDynaPoly ? "DynaPolyActor dyna" : "Actor actor");
}

std::string makeActorSource(const ActorDraft& draft, const hackeroot::Tables& tables) {
    const std::string type = makeTypeName(draft.name);
    const std::string stem = makeFileStem(draft.name);
    const std::string object = findObjectName(draft);
    std::string source =
        std::format("/*\n * File: {}.c\n * Overlay: ovl_{}\n * Description: {}\n */\n\n"
                    "#include \"{}.h\"\n\n",
                    stem, draft.name, draft.description, stem);
    if (draft.includesCommon) {
        for (const std::string_view include : CommonIncludes) {
            source += std::format("#include \"{}\"\n", include);
        }
    } else {
        source += "#include \"play_state.h\"\n";
    }
    source += std::format("\n#include \"assets/objects/{0}/{0}.h\"\n\n#define FLAGS ({1})\n\n",
                          object, makeFlags(draft));
    for (const std::string_view function : {"Init", "Destroy", "Update"}) {
        source += std::format("void {}_{}(Actor* thisx, PlayState* play);\n", type, function);
    }
    if (draft.draws) {
        source += std::format("void {}_Draw(Actor* thisx, PlayState* play);\n", type);
    }
    source += std::format("\nActorProfile {}_Profile = {{\n"
                          "    /**/ {},\n    /**/ {},\n    /**/ FLAGS,\n    /**/ {},\n"
                          "    /**/ sizeof({}),\n    /**/ {}_Init,\n    /**/ {}_Destroy,\n"
                          "    /**/ {}_Update,\n    /**/ {},\n}};\n\n",
                          draft.name, makeActorEnum(draft.name), draft.category,
                          findObjectEnum(draft, tables), type, type, type, type,
                          draft.draws ? type + "_Draw" : std::string("NULL"));
    source += makeFunctions(draft);
    return source;
}

std::expected<hackeroot::EditPlan, std::string>
makeActorPlan(const std::filesystem::path& repository, const ActorDraft& draft,
              const hackeroot::Tables& tables) {
    if (std::optional<std::string> problem = findDraftProblem(draft, tables)) {
        return std::unexpected(std::move(*problem));
    }
    hackeroot::EditPlan plan;
    std::expected<hackeroot::FileEdit, std::string> actors = openFile(repository, ActorTableFile);
    if (!actors) {
        return std::unexpected(actors.error());
    }
    const std::string actorEnum = makeActorEnum(draft.name);
    actors->items.push_back(
        putTableEntry(actors->after,
                      std::format("DEFINE_ACTOR({}, {}, {}, \"{}\")", draft.name, actorEnum,
                                  draft.allocation, draft.name),
                      std::format("DEFINE_ACTOR({},", draft.name), "DEFINE_ACTOR_UNSET(", {},
                      draft.takesUnsetSlot, "custom actors", actorEnum));
    plan.files.push_back(std::move(*actors));

    if (draft.newObject) {
        std::expected<hackeroot::FileEdit, std::string> objects =
            openFile(repository, ObjectTableFile);
        if (!objects) {
            return std::unexpected(objects.error());
        }
        objects->items.push_back(putTableEntry(
            objects->after,
            std::format("DEFINE_OBJECT({}, {})", *draft.newObject, makeUpper(*draft.newObject)),
            std::format("DEFINE_OBJECT({},", *draft.newObject), "DEFINE_OBJECT_UNSET(",
            {"OBJECT_INVALID"}, draft.takesUnsetSlot, "custom objects",
            makeUpper(*draft.newObject)));
        plan.files.push_back(std::move(*objects));
        plan.warnings.push_back(std::format(
            "{0} has no files yet: export it from fast64 to assets/objects/{0}/ before building",
            *draft.newObject));
    }

    std::expected<hackeroot::FileEdit, std::string> spec = openFile(repository, SpecFile);
    if (!spec) {
        return std::unexpected(spec.error());
    }
    spec->items.push_back(
        hackeroot::EditItem{.status = hackeroot::putSpecSegment(spec->after, "ovl_" + draft.name,
                                                                makeOverlaySegment(draft.name),
                                                                "custom actors", OverlayAnchor),
                            .label = std::format("overlay segment \"ovl_{}\"", draft.name)});
    if (draft.newObject) {
        spec->items.push_back(hackeroot::EditItem{
            .status = hackeroot::putSpecSegment(spec->after, *draft.newObject,
                                                makeObjectSegment(*draft.newObject),
                                                "custom objects", ObjectAnchor),
            .label = std::format("object segment \"{}\"", *draft.newObject)});
    }
    plan.files.push_back(std::move(*spec));

    const std::filesystem::path folder = makeOverlayFolder(draft.name);
    std::error_code status;
    if (std::filesystem::exists(repository / folder, status)) {
        return std::unexpected(std::format("{} exists already", folder.generic_string()));
    }
    const std::string stem = makeFileStem(draft.name);
    plan.files.push_back(hackeroot::FileEdit{
        .file = folder / (stem + ".h"),
        .after = makeActorHeader(draft),
        .items = {{.status = hackeroot::EditStatus::Add, .label = "the actor's struct"}}});
    plan.files.push_back(
        hackeroot::FileEdit{.file = folder / (stem + ".c"),
                            .after = makeActorSource(draft, tables),
                            .items = {{.status = hackeroot::EditStatus::Add,
                                       .label = "its profile, Init, Destroy, Update" +
                                                std::string(draft.draws ? " and Draw" : "")}}});
    if (draft.isDynaPoly) {
        plan.warnings.push_back(std::format(
            "{}.c names {} where the collision header of its object goes; put it there before "
            "building",
            stem, CollisionPlaceholder));
    }
    return plan;
}

} // namespace toki::tools

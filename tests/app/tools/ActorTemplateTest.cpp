#include "app/tools/createActor/domain/ActorTemplate.h"

#include "base/Diagnostic.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

void writeText(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream << text;
}

[[nodiscard]] std::string readText(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

[[nodiscard]] std::filesystem::path makeCheckout() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "toki_tests" / "create_actor";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    writeText(root / "include/tables/actor_table.h",
              "/* 0x0000 */ DEFINE_ACTOR_INTERNAL(Player, ACTOR_PLAYER, ACTOROVL_ALLOC_NORMAL, "
              "\"Player\")\n"
              "/* 0x0001 */ DEFINE_ACTOR_UNSET(ACTOR_UNSET_1)\n");
    writeText(root / "include/tables/object_table.h",
              "/* 0x0000 */ DEFINE_OBJECT_UNSET(OBJECT_INVALID)\n"
              "/* 0x0001 */ DEFINE_OBJECT(gameplay_keep, OBJECT_GAMEPLAY_KEEP)\n");
    writeText(root / "spec/spec", "beginseg\n    name \"code\"\nendseg\n\n"
                                  "beginseg\n    name \"gameplay_keep\"\nendseg\n\n"
                                  "beginseg\n    name \"object_box\"\nendseg\n");
    return root;
}

[[nodiscard]] hackeroot::Tables makeTables() {
    hackeroot::Tables tables;
    tables.actors.push_back({.name = "Player", .enumName = "ACTOR_PLAYER"});
    tables.objects.push_back(
        {.id = 1, .name = "gameplay_keep", .enumName = "OBJECT_GAMEPLAY_KEEP"});
    return tables;
}

} // namespace

TEST_CASE("a header's documented defines are listed with their comments", "[tools][actor]") {
    const std::vector<hackeroot::DocumentedDefine> flags = hackeroot::listDocumentedDefines(
        "// Can be targeted.\n// Navi hovers.\n#define ACTOR_FLAG_ATTENTION (1 << 0)\n\n"
        "#define ACTOR_FLAG_HOSTILE (1 << 2)\n/** Talks. */\n  #define ACTOR_FLAG_TALK (1 << 8)\n"
        "#define ACTOR_FLAGS_ALL(x) x\n#define OTHER 1\n",
        "ACTOR_FLAG_");

    REQUIRE(flags.size() == 3);
    REQUIRE(flags[0].name == "ACTOR_FLAG_ATTENTION");
    REQUIRE(flags[0].comment == "Can be targeted. Navi hovers.");
    REQUIRE(flags[1].comment.empty());
    REQUIRE(flags[2].comment == "Talks.");
}

TEST_CASE("a new actor's files follow the decomp's template", "[tools][actor]") {
    const toki::tools::ActorDraft draft{.name = "En_Foo",
                                        .description = "A foo",
                                        .flags = {"ACTOR_FLAG_A", "ACTOR_FLAG_B"},
                                        .isDynaPoly = true,
                                        .turnsCollision = true};

    const std::string header = toki::tools::makeActorHeader(draft);
    REQUIRE(header.starts_with("#ifndef Z_EN_FOO_H\n#define Z_EN_FOO_H\n"));
    REQUIRE(header.contains("typedef struct EnFoo {\n    DynaPolyActor dyna;\n"
                            "    EnFooActionFunc actionFunc;\n} EnFoo;"));
    const std::string source = toki::tools::makeActorSource(draft, makeTables());
    REQUIRE(source.contains("#include \"z_en_foo.h\"\n"));
    REQUIRE(source.contains("#include \"assets/objects/gameplay_keep/gameplay_keep.h\"\n"));
    REQUIRE(source.contains("#define FLAGS (ACTOR_FLAG_A | ACTOR_FLAG_B)\n"));
    REQUIRE(source.contains("ActorProfile En_Foo_Profile = {\n    /**/ ACTOR_EN_FOO,\n"
                            "    /**/ ACTORCAT_MISC,\n    /**/ FLAGS,\n"
                            "    /**/ OBJECT_GAMEPLAY_KEEP,\n    /**/ sizeof(EnFoo),\n"));
    REQUIRE(source.contains(
        "DynaPolyActor_Init(&this->dyna, DYNA_TRANSFORM_POS | DYNA_TRANSFORM_ROT_Y);"));
    REQUIRE(source.contains("void EnFoo_Draw(Actor* thisx, PlayState* play) {"));
}

TEST_CASE("creating an actor registers it and writes its overlay, once", "[tools][actor]") {
    const std::filesystem::path root = makeCheckout();
    const toki::tools::ActorDraft draft{.name = "En_Foo", .newObject = "object_foo"};

    const std::expected<hackeroot::EditPlan, std::string> plan =
        toki::tools::makeActorPlan(root, draft, makeTables());
    REQUIRE(plan.has_value());
    REQUIRE(plan->files.size() == 5);
    REQUIRE(plan->warnings.size() == 1);
    REQUIRE(hackeroot::applyEditPlan(root, *plan).has_value());

    const std::string actors = readText(root / "include/tables/actor_table.h");
    REQUIRE(
        actors.contains("/* 0x0001 */ DEFINE_ACTOR(En_Foo, ACTOR_EN_FOO, ACTOROVL_ALLOC_NORMAL, "
                        "\"En_Foo\")\n"));
    REQUIRE(readText(root / "include/tables/object_table.h")
                .contains("// Toki64 custom objects — managed block\n"
                          "DEFINE_OBJECT(object_foo, OBJECT_FOO)\n"));
    const std::string spec = readText(root / "spec/spec");
    REQUIRE(spec.contains("name \"ovl_En_Foo\""));
    REQUIRE(spec.find("name \"ovl_En_Foo\"") < spec.find("name \"gameplay_keep\""));
    REQUIRE(spec.find("name \"object_foo\"") < spec.find("name \"object_box\""));
    REQUIRE(spec.contains("include \"$(BUILD_DIR)/src/overlays/actors/ovl_En_Foo/z_en_foo.o\""));
    REQUIRE(std::filesystem::exists(root / "src/overlays/actors/ovl_En_Foo/z_en_foo.c"));
    REQUIRE(std::filesystem::exists(root / "src/overlays/actors/ovl_En_Foo/z_en_foo.h"));

    REQUIRE_FALSE(toki::tools::makeActorPlan(root, draft, makeTables()).has_value());
    hackeroot::Tables registered = makeTables();
    registered.actors.push_back({.name = "En_Foo", .enumName = "ACTOR_EN_FOO"});
    REQUIRE(toki::tools::findDraftProblem({.name = "En_Foo"}, registered).has_value());
    REQUIRE(toki::tools::findDraftProblem({.name = "en foo"}, registered).has_value());
    REQUIRE(toki::tools::findDraftProblem({.name = "En_Bar", .object = "object_none"}, registered)
                .has_value());
    REQUIRE_FALSE(toki::tools::findDraftProblem({.name = "En_Bar"}, registered).has_value());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("a real checkout has every place a new actor is registered in",
          "[tools][actor][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const std::expected<toki::tools::ActorChoices, std::string> choices =
        toki::tools::readActorChoices(repository);
    REQUIRE(choices.has_value());
    CHECK(choices->flags.size() > 20);
    CHECK(choices->allocations.size() == 3);
    CHECK_FALSE(choices->flags.front().comment.empty());

    const std::expected<hackeroot::EditPlan, std::string> plan = toki::tools::makeActorPlan(
        repository, {.name = "En_Toki_Test", .newObject = "object_toki_test"},
        workspace->getTables());
    REQUIRE(plan.has_value());
    const hackeroot::FileEdit& actors = plan->files[0];
    CHECK(actors.items.front().label.contains("unused slot"));
    const hackeroot::FileEdit& spec = plan->files[2];
    REQUIRE(spec.file == std::filesystem::path("spec/spec"));
    CHECK(spec.after.find("name \"ovl_En_Toki_Test\"") <
          spec.after.find("beginseg\n    name \"gameplay_keep\""));
    CHECK(spec.after.find("name \"object_toki_test\"") <
          spec.after.find("beginseg\n    name \"object_box\""));
}

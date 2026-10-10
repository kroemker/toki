#include "app/actor/domain/ValueCollection.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/CollectionCatalog.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] hackeroot::Workspace loadFixture() {
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded = hackeroot::Workspace::load(
        std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout", {});
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

[[nodiscard]] const toki::actor::ValueCollection&
requireCollection(const toki::actor::CollectionsRead& read, const std::string_view name) {
    const auto found =
        std::ranges::find(read.collections, name, &toki::actor::ValueCollection::name);
    REQUIRE(found != read.collections.end());
    return *found;
}

[[nodiscard]] bool hasActor(const hackeroot::Tables& tables, const std::string_view actor) {
    return std::ranges::contains(tables.actors, actor, &hackeroot::ActorEntry::enumName);
}

} // namespace

TEST_CASE("shared collections are read from the checkout's headers and tables",
          "[actor][collections]") {
    const hackeroot::Workspace workspace = loadFixture();

    const toki::actor::CollectionsRead read = toki::actor::readSharedCollections(workspace);

    REQUIRE(read.diagnostics.empty());
    REQUIRE(read.collections.size() == toki::actor::getSharedCollections().size());
    const toki::actor::ValueCollection& items = requireCollection(read, "GetItemID");
    const base::s64 heartPiece = hasActor(workspace.getTables(), "ACTOR_EN_TEST") ? 3 : 2;
    const auto found =
        std::ranges::find(items.values, "GI_HEART_PIECE", &toki::actor::ChoiceValue::label);
    REQUIRE(found != items.values.end());
    REQUIRE(found->value == heartPiece);
    REQUIRE(requireCollection(read, "ItemID").values.back().value == 0xFF);
    REQUIRE(requireCollection(read, "Item00Type").values.size() == 2);
    REQUIRE_FALSE(std::ranges::contains(items.values, "GI_MAX", &toki::actor::ChoiceValue::label));

    const toki::actor::ValueCollection& sounds = requireCollection(read, "SfxId");
    REQUIRE(sounds.values.size() == 3);
    REQUIRE(sounds.values[1].label == "NA_SE_EN_DODO_J_WALK");
    REQUIRE(sounds.values[1].value == 0x3800);
    const toki::actor::ValueCollection& music = requireCollection(read, "NA_BGM");
    REQUIRE(music.values.size() == 5);
    REQUIRE(music.values[2].label == "NA_BGM_FIELD_LOGIC");
    REQUIRE(music.values.back().value == 0xFFFF);

    const toki::actor::ValueCollection& actors = requireCollection(read, "ActorID");
    REQUIRE(actors.values.front().label == "ACTOR_PLAYER");
    REQUIRE_FALSE(
        std::ranges::contains(actors.values, "ACTOR_UNSET_1", &toki::actor::ChoiceValue::label));
    REQUIRE(std::ranges::any_of(
        read.files, [](const std::filesystem::path& file) { return file.filename() == "item.h"; }));
}

TEST_CASE("an actor's collections are the enumerations its own headers define",
          "[actor][collections]") {
    const hackeroot::Workspace workspace = loadFixture();
    const std::vector<hackeroot::ActorEntry>& actors = workspace.getTables().actors;
    const auto box = std::ranges::find(actors, "ACTOR_EN_BOX", &hackeroot::ActorEntry::enumName);
    REQUIRE(box != actors.end());

    const toki::actor::CollectionsRead read = toki::actor::readActorCollections(workspace, *box);

    REQUIRE(read.collections.size() == 1);
    const toki::actor::ValueCollection& types = read.collections.front();
    REQUIRE(types.name == "EnBoxType");
    REQUIRE(types.values.size() == 2);
    REQUIRE(types.values[1].label == "ENBOX_TYPE_SMALL");
    REQUIRE(types.values[1].value == 5);
    REQUIRE(toki::actor::readActorCollections(workspace, actors.front()).collections.empty());
}

TEST_CASE("the catalog finds an actor's own collection before a shared one",
          "[actor][collections]") {
    const auto workspace = std::make_shared<const hackeroot::Workspace>(loadFixture());
    toki::actor::CollectionCatalog catalog;
    catalog.open(workspace);

    const toki::actor::ValueCollection* own = nullptr;
    const toki::actor::ValueCollection* shared = nullptr;
    for (int attempt = 0; attempt < 2000 && (own == nullptr || shared == nullptr); ++attempt) {
        catalog.update();
        own = catalog.findCollection("ACTOR_EN_BOX", "EnBoxType");
        shared = catalog.findCollection("ACTOR_EN_BOX", "GetItemID");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(own != nullptr);
    REQUIRE(shared != nullptr);
    REQUIRE(catalog.findCollection("ACTOR_PLAYER", "EnBoxType") == nullptr);
    const std::vector<std::string> names = catalog.listCollectionNames("ACTOR_EN_BOX");
    REQUIRE(names.front() == "EnBoxType");
    REQUIRE(std::ranges::contains(names, "SceneID"));
    REQUIRE(catalog.takeDiagnostics().empty());
}

TEST_CASE("a real checkout's collections are read without a diagnostic",
          "[actor][collections][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const toki::actor::CollectionsRead shared = toki::actor::readSharedCollections(*workspace);
    for (const base::Diagnostic& diagnostic : shared.diagnostics) {
        UNSCOPED_INFO(base::describe(diagnostic));
    }
    REQUIRE(shared.diagnostics.empty());
    const toki::actor::ValueCollection& items = requireCollection(shared, "GetItemID");
    REQUIRE(items.values.size() > 100);
    REQUIRE(
        std::ranges::contains(items.values, "GI_HEART_PIECE", &toki::actor::ChoiceValue::label));
    REQUIRE(requireCollection(shared, "Item00Type").values.size() > 10);
    REQUIRE(requireCollection(shared, "SfxId").values.size() > 1000);
    REQUIRE(requireCollection(shared, "NA_BGM").values.size() > 100);

    const std::vector<hackeroot::ActorEntry>& actors = workspace->getTables().actors;
    const auto box = std::ranges::find(actors, "ACTOR_EN_BOX", &hackeroot::ActorEntry::enumName);
    REQUIRE(box != actors.end());
    const toki::actor::CollectionsRead own = toki::actor::readActorCollections(*workspace, *box);
    REQUIRE(own.collections.size() == 1);
    REQUIRE(requireCollection(own, "EnBoxType").values.size() > 5);

    const toki::actor::ActorDatabase shipped = toki::actor::readActorDatabase(TOKI_SHIPPED_ACTORS);
    for (const auto& [name, entry] : shipped.entries) {
        const auto actor = std::ranges::find(actors, name, &hackeroot::ActorEntry::enumName);
        const toki::actor::CollectionsRead entryOwn =
            actor != actors.end() ? toki::actor::readActorCollections(*workspace, *actor)
                                  : toki::actor::CollectionsRead{};
        const std::vector<base::Diagnostic> problems = toki::actor::checkCollections(
            entry, [&](const std::string_view wanted) -> const toki::actor::ValueCollection* {
                for (const toki::actor::CollectionsRead* read : {&entryOwn, &shared}) {
                    const auto found = std::ranges::find(read->collections, wanted,
                                                         &toki::actor::ValueCollection::name);
                    if (found != read->collections.end()) {
                        return &*found;
                    }
                }
                return nullptr;
            });
        for (const base::Diagnostic& problem : problems) {
            UNSCOPED_INFO(problem.message);
        }
        CHECK(problems.empty());
    }
}

TEST_CASE("how an entry uses collections is checked against the checkout's",
          "[actor][collections]") {
    const toki::actor::ValueCollection items{.name = "GetItemID",
                                             .values = {{.value = 0, .label = "GI_NONE"},
                                                        {.value = 0x300, .label = "GI_SIGN"},
                                                        {.value = 0x400, .label = "GI_TOO_FAR"}}};
    const toki::actor::ActorEntry entry{
        .actor = "ACTOR_A",
        .params = {toki::actor::ActorParam{.name = "Item",
                                           .kind = toki::actor::ParamKind::Choice,
                                           .mask = 0x00FF,
                                           .collection = "GetItemID",
                                           .offset = 0x300},
                   toki::actor::ActorParam{.name = "Other",
                                           .kind = toki::actor::ParamKind::Choice,
                                           .mask = 0x0F00,
                                           .collection = "Missing"}},
        .variants = {toki::actor::ActorVariant{
            .match = {toki::actor::ParamMatch{.param = "Item",
                                              .values = {base::s64{0}, base::s64{1}}}}}}};

    const std::vector<base::Diagnostic> problems = toki::actor::checkCollections(
        entry, [&items](const std::string_view name) -> const toki::actor::ValueCollection* {
            return name == items.name ? &items : nullptr;
        });

    std::string described;
    for (const base::Diagnostic& problem : problems) {
        described += problem.message + "\n";
    }
    INFO(described);
    REQUIRE(problems.size() == 3);
    REQUIRE(described.contains("2 of the 3 values of GetItemID do not fit in 8 bits"));
    REQUIRE(described.contains("match.Item: 1 is not one of GetItemID"));
    REQUIRE(described.contains("params.Other: the checkout has no collection Missing"));
}

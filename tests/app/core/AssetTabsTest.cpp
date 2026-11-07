#include "app/core/domain/AssetTabs.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

using toki::core::AssetTabEntry;
using toki::core::AssetTabs;
using toki::core::TabOpening;

[[nodiscard]] std::vector<std::string> listKeys(const AssetTabs& tabs) {
    std::vector<std::string> keys;
    for (const AssetTabEntry& tab : tabs.getTabs()) {
        keys.push_back(tab.key + (tab.isPreview ? "?" : ""));
    }
    return keys;
}

} // namespace

TEST_CASE("a previewed asset takes the preview tab's place", "[core][tabs]") {
    AssetTabs tabs;
    REQUIRE(tabs.open("a", TabOpening::Preview).isNew);
    const toki::core::OpenedTab opened = tabs.open("b", TabOpening::Preview);
    REQUIRE(opened.isNew);
    REQUIRE(opened.replaced == "a");
    REQUIRE(listKeys(tabs) == std::vector<std::string>{"b?"});
    REQUIRE(tabs.getActive() == "b");
}

TEST_CASE("a pinned tab is kept when another asset is previewed", "[core][tabs]") {
    AssetTabs tabs;
    tabs.open("a", TabOpening::Preview);
    tabs.pin("a");
    const toki::core::OpenedTab opened = tabs.open("b", TabOpening::Preview);
    REQUIRE_FALSE(opened.replaced);
    REQUIRE(listKeys(tabs) == std::vector<std::string>{"a", "b?"});
}

TEST_CASE("an asset already open is made active, and pinned when opened pinned", "[core][tabs]") {
    AssetTabs tabs;
    tabs.open("a", TabOpening::Pinned);
    tabs.open("b", TabOpening::Preview);
    REQUIRE_FALSE(tabs.open("a", TabOpening::Preview).isNew);
    REQUIRE(tabs.getActive() == "a");
    REQUIRE_FALSE(tabs.open("b", TabOpening::Pinned).isNew);
    REQUIRE(listKeys(tabs) == std::vector<std::string>{"a", "b"});
}

TEST_CASE("a new tab is put last, beside the active one", "[core][tabs]") {
    AssetTabs tabs;
    REQUIRE_FALSE(tabs.open("a", TabOpening::Pinned).beside);
    tabs.open("b", TabOpening::Pinned);
    tabs.activate("a");
    const toki::core::OpenedTab opened = tabs.open("c", TabOpening::Pinned);
    REQUIRE(opened.beside == "a");
    REQUIRE(listKeys(tabs) == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("a preview taking another's place is not opened beside a tab", "[core][tabs]") {
    AssetTabs tabs;
    tabs.open("a", TabOpening::Pinned);
    tabs.open("b", TabOpening::Preview);
    const toki::core::OpenedTab opened = tabs.open("c", TabOpening::Preview);
    REQUIRE(opened.replaced == "b");
    REQUIRE_FALSE(opened.beside);
}

TEST_CASE("closing the active tab activates the one active before it", "[core][tabs]") {
    AssetTabs tabs;
    tabs.open("a", TabOpening::Pinned);
    tabs.open("b", TabOpening::Pinned);
    tabs.open("c", TabOpening::Pinned);
    tabs.activate("a");
    tabs.activate("b");
    REQUIRE(tabs.close("b"));
    REQUIRE(tabs.getActive() == "a");
    REQUIRE(tabs.close("a"));
    REQUIRE(tabs.getActive() == "c");
    REQUIRE(tabs.close("c"));
    REQUIRE_FALSE(tabs.getActive());
    REQUIRE_FALSE(tabs.close("a"));
}

TEST_CASE("a replaced preview is never activated again", "[core][tabs]") {
    AssetTabs tabs;
    tabs.open("a", TabOpening::Pinned);
    tabs.open("b", TabOpening::Preview);
    tabs.open("c", TabOpening::Preview);
    REQUIRE(tabs.close("c"));
    REQUIRE(tabs.getActive() == "a");
}

TEST_CASE("closing another tab keeps the active one", "[core][tabs]") {
    AssetTabs tabs;
    tabs.open("a", TabOpening::Pinned);
    tabs.open("b", TabOpening::Pinned);
    REQUIRE(tabs.close("a"));
    REQUIRE(tabs.getActive() == "b");
}

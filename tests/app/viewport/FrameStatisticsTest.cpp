#include "app/viewport/domain/FrameStatistics.h"

#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ReferenceGrid.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <vector>

using base::usize;
using toki::viewport::FrameStatistics;
using toki::viewport::ImageKind;
using toki::viewport::makeFrameStatistics;

TEST_CASE("a frame counts what the RSP and RDP did and the memory it filled",
          "[viewport][statistics]") {
    toki::viewport::ViewportContent content = toki::viewport::makeReferenceGrid();
    content.labels.push_back({.name = "grid", .kind = ImageKind::Generated});
    const toki::viewport::Frame frame = toki::viewport::runFrame(
        content, toki::viewport::OrbitCamera{}, {.width = 320, .height = 240});

    const FrameStatistics statistics = makeFrameStatistics(frame, content);

    REQUIRE(statistics.rsp.commands > 0);
    REQUIRE(statistics.rsp.vertices > 0);
    REQUIRE(statistics.rsp.triangles > 0);
    REQUIRE(statistics.codeRegion == toki::viewport::FirstImageAddress);
    REQUIRE(statistics.frameListSize == frame.frameListSize);
    REQUIRE(statistics.used == frame.frameListAddress + frame.frameListSize);
    REQUIRE(statistics.images.size() == 1);
    REQUIRE(statistics.images[0].name == "grid");
    REQUIRE(statistics.images[0].isMapped);
    REQUIRE(statistics.images[0].size > 0);
}

TEST_CASE("an image left out of the frame is counted at the size it would have taken",
          "[viewport][statistics]") {
    z64::MemoryImage image;
    image.segments.push_back(z64::ImageSegment{.number = 6, .bytes = std::vector<base::u8>(100)});
    toki::viewport::ViewportContent content;
    content.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(image)));
    toki::viewport::Frame frame;
    frame.mappedImages.emplace_back(std::nullopt);
    frame.frameListAddress = toki::viewport::FirstImageAddress;
    frame.frameListSize = 64;

    const FrameStatistics statistics = makeFrameStatistics(frame, content);

    REQUIRE(statistics.images.size() == 1);
    REQUIRE(statistics.images[0].name == "Image 0");
    REQUIRE_FALSE(statistics.images[0].isMapped);
    REQUIRE(statistics.images[0].size == usize{112});
    REQUIRE(statistics.used == toki::viewport::FirstImageAddress + 64);
}

TEST_CASE("a frame that was not run fills no memory", "[viewport][statistics]") {
    const FrameStatistics statistics =
        makeFrameStatistics(toki::viewport::Frame{}, toki::viewport::ViewportContent{});

    REQUIRE(statistics.used == 0);
    REQUIRE(statistics.images.empty());
}

#include "app/message/domain/BoxFrameWorker.h"

#include "app/message/domain/MessageCatalog.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <thread>

using base::u64;
using toki::message::BoxFrameWorker;
using toki::message::BoxSlot;
using toki::message::FinishedFrame;

namespace {

[[nodiscard]] std::optional<FinishedFrame> waitForFrame(BoxFrameWorker& worker,
                                                        const BoxSlot slot) {
    constexpr int Tries = 500;
    for (int attempt = 0; attempt < Tries; ++attempt) {
        if (std::optional<FinishedFrame> finished = worker.takeFinished(slot)) {
            return finished;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("a box's frame is run on the worker and taken once for its slot", "[message]") {
    BoxFrameWorker worker;
    const u64 owner = worker.makeOwner();
    const auto catalog = std::make_shared<const toki::message::MessageCatalog>();
    constexpr u64 Key = 7;

    worker.request(BoxSlot{.owner = owner, .index = 2}, Key, toki::message::LaidBox{}, catalog);

    const std::optional<FinishedFrame> finished =
        waitForFrame(worker, BoxSlot{.owner = owner, .index = 2});
    REQUIRE(finished.has_value());
    CHECK(finished->key == Key);
    CHECK_FALSE(finished->frame.diagnostics.empty());
    CHECK_FALSE(worker.takeFinished(BoxSlot{.owner = owner, .index = 2}).has_value());
    CHECK_FALSE(worker.takeFinished(BoxSlot{.owner = owner, .index = 1}).has_value());
}

TEST_CASE("asking again for what a slot was asked for runs nothing more", "[message]") {
    BoxFrameWorker worker;
    const BoxSlot slot{.owner = worker.makeOwner(), .index = 0};
    const auto catalog = std::make_shared<const toki::message::MessageCatalog>();

    worker.request(slot, 1, toki::message::LaidBox{}, catalog);
    REQUIRE(waitForFrame(worker, slot).has_value());
    worker.request(slot, 1, toki::message::LaidBox{}, catalog);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CHECK_FALSE(worker.takeFinished(slot).has_value());
    worker.request(slot, 2, toki::message::LaidBox{}, catalog);
    const std::optional<FinishedFrame> again = waitForFrame(worker, slot);
    REQUIRE(again.has_value());
    CHECK(again->key == 2);
}

TEST_CASE("a slot asked for again keeps only the frame asked for last", "[message]") {
    BoxFrameWorker worker;
    const BoxSlot slot{.owner = worker.makeOwner(), .index = 0};
    const auto catalog = std::make_shared<const toki::message::MessageCatalog>();

    worker.request(slot, 1, toki::message::LaidBox{}, catalog);
    worker.request(slot, 2, toki::message::LaidBox{}, catalog);
    worker.request(slot, 3, toki::message::LaidBox{}, catalog);

    std::optional<FinishedFrame> last;
    for (int attempt = 0; attempt < 3 && (!last || last->key != 3); ++attempt) {
        last = waitForFrame(worker, slot);
        REQUIRE(last.has_value());
    }
    CHECK(last->key == 3);
}

TEST_CASE("the slots of two owners are kept apart, and a forgotten owner's are dropped",
          "[message]") {
    BoxFrameWorker worker;
    const u64 first = worker.makeOwner();
    const u64 second = worker.makeOwner();
    REQUIRE(first != second);
    const auto catalog = std::make_shared<const toki::message::MessageCatalog>();

    worker.request(BoxSlot{.owner = first, .index = 0}, 1, toki::message::LaidBox{}, catalog);
    worker.request(BoxSlot{.owner = second, .index = 0}, 2, toki::message::LaidBox{}, catalog);

    const std::optional<FinishedFrame> secondFrame =
        waitForFrame(worker, BoxSlot{.owner = second, .index = 0});
    REQUIRE(secondFrame.has_value());
    CHECK(secondFrame->key == 2);

    worker.forget(first);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(worker.takeFinished(BoxSlot{.owner = first, .index = 0}).has_value());
}

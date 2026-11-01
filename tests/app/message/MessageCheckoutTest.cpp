#include "app/message/domain/Message.h"
#include "app/message/domain/MessageEncoder.h"
#include "app/message/domain/MessageGeometry.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageReader.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using base::usize;

TEST_CASE("every message of a real checkout reads without a diagnostic", "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const toki::message::MessageTable table = toki::message::readMessages(*workspace);

    std::string described;
    for (usize index = 0; index < std::min<usize>(table.diagnostics.size(), 20); ++index) {
        const base::Diagnostic& diagnostic = table.diagnostics[index];
        described += diagnostic.location.file.filename().string() + ":" +
                     std::to_string(diagnostic.location.line) + ": " + diagnostic.message + "\n";
    }
    INFO(described);
    CHECK(table.diagnostics.empty());

    const usize vanilla = static_cast<usize>(
        std::ranges::count_if(table.messages, &toki::message::Message::isVanilla));
    const usize codes = static_cast<usize>(
        std::ranges::count_if(table.messages, [](const toki::message::Message& message) {
            const auto& english =
                message.texts[static_cast<usize>(toki::message::Language::English)];
            return std::ranges::any_of(english.pieces, [](const toki::message::Piece& piece) {
                return std::holds_alternative<toki::message::CodePiece>(piece);
            });
        }));
    WARN(table.messages.size() << " messages, " << vanilla << " vanilla, " << codes
                               << " with control codes");
    CHECK(vanilla > 2000);
    REQUIRE_FALSE(table.messages.empty());
    CHECK(table.messages.back().id == 0xFFFD);
}

TEST_CASE("every English message encodes to the bytes the build compiled", "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const std::filesystem::path built = repository / "build" /
                                        workspace->getBuildDefines().version /
                                        "assets/text/nes_message_data_static.o.bin";
    std::ifstream stream(built, std::ios::binary);
    if (!stream) {
        SKIP("build the checkout first: " << built.string() << " is missing");
    }
    const std::vector<base::u8> expected((std::istreambuf_iterator<char>(stream)),
                                         std::istreambuf_iterator<char>());

    const toki::message::MessageTable table = toki::message::readMessages(*workspace);
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readMessageFormat(*workspace);
    REQUIRE(format.has_value());
    const toki::message::EncodedData data = toki::message::encodeMessageData(
        table, format->makeEncoder(), toki::message::Language::English,
        toki::message::isFontMessageEnglish(workspace->getMacros()));

    std::string described;
    for (usize index = 0; index < std::min<usize>(data.diagnostics.size(), 10); ++index) {
        described += data.diagnostics[index].message + "\n";
    }
    INFO(described);
    CHECK(data.diagnostics.empty());

    usize mismatches = 0;
    std::string firstMismatches;
    for (usize index = 0; index < table.messages.size(); ++index) {
        const std::optional<usize> offset = data.offsets[index];
        if (!offset) {
            continue;
        }
        const usize end = index + 1 < table.messages.size() && data.offsets[index + 1]
                              ? *data.offsets[index + 1]
                              : data.bytes.size();
        const bool isSame = end <= expected.size() &&
                            std::equal(data.bytes.begin() + static_cast<std::ptrdiff_t>(*offset),
                                       data.bytes.begin() + static_cast<std::ptrdiff_t>(end),
                                       expected.begin() + static_cast<std::ptrdiff_t>(*offset));
        if (!isSame && ++mismatches <= 5) {
            firstMismatches +=
                std::format("0x{:04X} at 0x{:X}\n", table.messages[index].id, *offset);
        }
    }
    INFO(firstMismatches);
    CHECK(mismatches == 0);
    REQUIRE(data.bytes.size() <= expected.size());
    CHECK(expected.size() - data.bytes.size() < 16);
    CHECK(std::all_of(expected.begin() + static_cast<std::ptrdiff_t>(data.bytes.size()),
                      expected.end(), [](const base::u8 byte) { return byte == 0; }));
}

TEST_CASE("every English message of a real checkout lays out from its own tables",
          "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const toki::message::MessageGeometry geometry = toki::message::readMessageGeometry(*workspace);
    REQUIRE(geometry.diagnostics.empty());
    CHECK(geometry.fontWidths[0] == 8.0F);
    CHECK(geometry.boxes[0].x == 34);
    CHECK(geometry.boxes[4].lowerY == 174);

    const toki::message::MessageTable table = toki::message::readMessages(*workspace);
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readMessageFormat(*workspace);
    REQUIRE(format.has_value());
    const toki::message::MessageEncoder encoder = format->makeEncoder();
    const bool isFontEnglish = toki::message::isFontMessageEnglish(workspace->getMacros());

    usize boxes = 0;
    std::string problems;
    for (const toki::message::Message& message : table.messages) {
        if (!toki::message::isInLanguage(message, toki::message::Language::English,
                                         isFontEnglish) ||
            message.macro == toki::message::MessageMacro::FontMessage) {
            continue;
        }
        const std::expected<toki::message::LayoutRequest, std::string> request =
            format->makeLayoutRequest(message);
        if (!request) {
            problems += std::format("0x{:04X}: {}\n", message.id, request.error());
            continue;
        }
        const toki::message::EncodedText text =
            encoder.encode(message.texts[static_cast<usize>(toki::message::Language::English)]);
        const toki::message::MessageLayout layout =
            toki::message::layoutMessage(text.bytes, geometry, *request);
        boxes += layout.boxes.size();
        for (const base::Diagnostic& diagnostic : layout.diagnostics) {
            problems += std::format("0x{:04X}: {}\n", message.id, diagnostic.message);
        }
        if (message.id == 0x0006) {
            REQUIRE(layout.boxes.size() == 1);
            CHECK(layout.boxes[0].endIcon == toki::message::EndIcon::Arrow);
            CHECK(layout.boxes[0].characters.front().y == layout.boxes[0].y + 8);
        }
    }
    WARN(boxes << " boxes");
    INFO(problems.substr(0, 2000));
    CHECK(problems.empty());
}

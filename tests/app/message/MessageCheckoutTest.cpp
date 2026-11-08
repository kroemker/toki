#include "app/message/domain/EditableBox.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageCatalog.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageEncoder.h"
#include "app/message/domain/MessageGeometry.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageMarkup.h"
#include "app/message/domain/MessageReader.h"
#include "app/message/domain/ShiftJis.h"
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
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
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

namespace {

void checkEncodedData(const hackeroot::Workspace& workspace, const std::filesystem::path& built,
                      const toki::message::MessageFormat& format,
                      const toki::message::Language language) {
    std::ifstream stream(built, std::ios::binary);
    if (!stream) {
        SKIP("build the checkout first: " << built.string() << " is missing");
    }
    const std::vector<base::u8> expected((std::istreambuf_iterator<char>(stream)),
                                         std::istreambuf_iterator<char>());

    const toki::message::MessageTable table = toki::message::readMessages(workspace);
    const toki::message::EncodedData data = toki::message::encodeMessageData(
        table, format.makeEncoder(), language,
        toki::message::isFontMessageEnglish(workspace.getMacros()));

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
        usize end = data.bytes.size();
        for (usize next = index + 1; next < table.messages.size(); ++next) {
            if (data.offsets[next]) {
                end = *data.offsets[next];
                break;
            }
        }
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

} // namespace

TEST_CASE("every English message encodes to the bytes the build compiled", "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readMessageFormat(*workspace);
    REQUIRE(format.has_value());
    checkEncodedData(*workspace,
                     repository / "build" / workspace->getBuildDefines().version /
                         "assets/text/nes_message_data_static.o.bin",
                     *format, toki::message::Language::English);
}

TEST_CASE("every Japanese message encodes to the bytes the build compiled", "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    std::expected<toki::message::ShiftJis, std::string> shiftJis =
        toki::message::ShiftJis::read(TOKI_SHIPPED_SHIFT_JIS);
    REQUIRE(shiftJis.has_value());
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readWideMessageFormat(*workspace, std::move(*shiftJis));
    REQUIRE(format.has_value());
    checkEncodedData(*workspace,
                     repository / "build" / workspace->getBuildDefines().version /
                         "assets/text/jpn_message_data_static.o.bin",
                     *format, toki::message::Language::Japanese);
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
    usize lines = 0;
    usize pastMargin = 0;
    base::s32 longestEnd = 0;
    std::vector<base::s32> ends;
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
        for (const toki::message::LaidBox& box : layout.boxes) {
            for (const toki::message::LaidLine& line : box.lines) {
                lines += 1;
                ends.push_back(line.endX - box.x);
                longestEnd = std::max(longestEnd, line.endX - box.x);
                pastMargin += line.endX > toki::message::findLineLimit(box) ? 1 : 0;
            }
        }
        for (const base::Diagnostic& diagnostic : layout.diagnostics) {
            problems += std::format("0x{:04X}: {}\n", message.id, diagnostic.message);
        }
        if (message.id == 0x0006) {
            REQUIRE(layout.boxes.size() == 1);
            CHECK(layout.boxes[0].endIcon == toki::message::EndIcon::Arrow);
            CHECK(layout.boxes[0].characters.front().y == layout.boxes[0].y + 8);
        }
    }
    WARN(boxes << " boxes, " << lines << " lines, " << pastMargin
               << " past the margin, the longest ending " << longestEnd << " from the box's left");
    std::ranges::sort(ends);
    if (!ends.empty()) {
        WARN("lines end by the box's left at " << ends[ends.size() * 99 / 100] << " (99%), "
                                               << ends[ends.size() * 999 / 1000] << " (99.9%)");
    }
    INFO(problems.substr(0, 2000));
    CHECK(problems.empty());
}

TEST_CASE("every unit of every English box of a real checkout has a stop to put the caret at",
          "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const toki::message::MessageGeometry geometry = toki::message::readMessageGeometry(*workspace);
    const toki::message::MessageTable table = toki::message::readMessages(*workspace);
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readMessageFormat(*workspace);
    REQUIRE(format.has_value());
    const toki::message::MessageEncoder encoder = format->makeEncoder();
    const bool isFontEnglish = toki::message::isFontMessageEnglish(workspace->getMacros());

    usize units = 0;
    std::string problems;
    for (const toki::message::Message& message : table.messages) {
        if (!toki::message::isInLanguage(message, toki::message::Language::English,
                                         isFontEnglish) ||
            message.macro == toki::message::MessageMacro::FontMessage) {
            continue;
        }
        const std::expected<toki::message::LayoutRequest, std::string> request =
            format->makeLayoutRequest(message);
        REQUIRE(request.has_value());
        const toki::message::MessageText& english =
            message.texts[static_cast<usize>(toki::message::Language::English)];
        const toki::message::EncodedText text = encoder.encode(english);
        REQUIRE(text.sources.size() == text.bytes.size());
        const toki::message::MessageLayout layout =
            toki::message::layoutMessage(text.bytes, geometry, *request);
        const std::vector<toki::message::TextPosition> sources =
            toki::message::localizeSources(english, text.sources);
        const std::vector<toki::message::BoxText> boxes = toki::message::splitBoxes(english);
        for (usize box = 0; box < std::min(boxes.size(), layout.boxes.size()); ++box) {
            const toki::message::EditableBox editable(boxes[box].pieces, &format->charmap);
            std::vector<bool> isStopped(editable.getUnits().size() + 1, false);
            for (const toki::message::LaidStop& stop : layout.boxes[box].stops) {
                if (const std::optional<usize> unit = editable.findUnit(sources[stop.byte])) {
                    isStopped[*unit] = true;
                }
            }
            const auto lastStopped = std::ranges::find(std::views::reverse(isStopped), true);
            const usize stoppedUntil =
                static_cast<usize>(std::ranges::distance(lastStopped, isStopped.rend()));
            units += editable.getUnits().size();
            for (usize unit = 0; unit < stoppedUntil; ++unit) {
                if (!isStopped[unit]) {
                    problems += std::format("0x{:04X} box {} unit {}\n", message.id, box + 1, unit);
                }
            }
            if (!isStopped.back() && stoppedUntil == isStopped.size()) {
                problems += std::format("0x{:04X} box {}: no stop at its end\n", message.id, box);
            }
        }
    }
    WARN(units << " units");
    INFO(problems.substr(0, 2000));
    CHECK(problems.empty());
}

TEST_CASE("every box of a real checkout reads back as it was from its boxes and its typed text",
          "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const toki::message::MessageTable table = toki::message::readMessages(*workspace);
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readMessageFormat(*workspace);
    REQUIRE(format.has_value());

    std::string problems;
    for (const toki::message::Message& message : table.messages) {
        for (usize language = 0; language < toki::message::LanguageCount; ++language) {
            const toki::message::MessageText& text = message.texts[language];
            const std::string read = toki::message::formatMarkup(text.pieces);
            std::vector<toki::message::BoxText> boxes = toki::message::splitBoxes(text);
            if (toki::message::formatMarkup(toki::message::joinBoxes(boxes)) != read) {
                problems += std::format("0x{:04X} language {}: boxes\n", message.id, language);
            }
            for (toki::message::BoxText& box : boxes) {
                const std::string boxRead = toki::message::formatMarkup(box.pieces);
                box.pieces = toki::message::EditableBox(box.pieces, &format->charmap).makePieces();
                const std::string typed = toki::message::formatMarkup(box.pieces);
                if (typed != boxRead) {
                    problems += std::format("0x{:04X} language {}: {:?} typed as {:?}\n",
                                            message.id, language, boxRead, typed);
                }
            }
        }
    }
    INFO(problems.substr(0, 3000));
    CHECK(problems.empty());
}

TEST_CASE("a message's boxes draw through the RSP and RDP from the checkout's own art",
          "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const toki::message::MessageArt art = toki::message::readMessageArt(*workspace, false);
    std::string described;
    for (const base::Diagnostic& diagnostic : art.diagnostics) {
        if (diagnostic.severity == base::Severity::Error) {
            described += diagnostic.message + "\n";
        }
    }
    INFO(described.substr(0, 2000));
    CHECK(described.empty());
    REQUIRE(art.setupList.has_value());

    const std::optional<std::vector<base::u8>> glyph = toki::message::decodeFontGlyph(art, 'A');
    REQUIRE(glyph.has_value());
    CHECK(glyph->size() == std::size_t{16 * 16 * 4});
    CHECK(
        std::ranges::any_of(*glyph, [](const base::u8 byte) { return byte != 0xFF && byte != 0; }));
    CHECK_FALSE(toki::message::decodeFontGlyph(art, 0x10).has_value());
    CHECK(toki::message::hasFontGlyph(art, 0xAA));
    CHECK_FALSE(toki::message::hasFontGlyph(art, 0xAB));

    const toki::message::MessageGeometry geometry = toki::message::readMessageGeometry(*workspace);
    const toki::message::MessageTable table = toki::message::readMessages(*workspace);
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readMessageFormat(*workspace);
    REQUIRE(format.has_value());
    const toki::message::MessageEncoder encoder = format->makeEncoder();

    for (const base::u16 id : {base::u16{0x0001}, base::u16{0x0006}, base::u16{0x1035}}) {
        const auto message = std::ranges::find(table.messages, id, &toki::message::Message::id);
        REQUIRE(message != table.messages.end());
        const toki::message::EncodedText text =
            encoder.encode(message->texts[static_cast<usize>(toki::message::Language::English)]);
        const toki::message::MessageLayout layout = toki::message::layoutMessage(
            text.bytes, geometry, *format->makeLayoutRequest(*message));
        for (const toki::message::LaidBox& box : layout.boxes) {
            const toki::message::BoxFrame frame = toki::message::runBoxFrame(box, art);
            std::string problems;
            for (const base::Diagnostic& diagnostic : frame.diagnostics) {
                problems += diagnostic.message + "\n";
            }
            INFO(std::format("0x{:04X}:\n{}", id, problems));
            CHECK(frame.diagnostics.empty());
            const usize shadows = box.boxType == 5 ? 0 : box.characters.size();
            const usize expected = (box.boxType < 4 ? 1 : 0) + (box.icon ? 1 : 0) +
                                   box.characters.size() + shadows +
                                   (box.endIcon == toki::message::EndIcon::None ? 0 : 1);
            CHECK(frame.rdpResult.draws.size() == expected);
        }
    }
}

TEST_CASE("every Japanese message lays out and draws from the kanji font", "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::shared_ptr<const hackeroot::Workspace> workspace = [&] {
        std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
            hackeroot::Workspace::load(repository, {});
        REQUIRE(loaded.has_value());
        return std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    }();
    const toki::message::MessageCatalog catalog =
        toki::message::readMessageCatalog(workspace, TOKI_SHIPPED_SHIFT_JIS, true);
    REQUIRE(catalog.art.hasKanji);
    CHECK(catalog.geometry.nonKanjiIndices.size() == 0xBC * 7);
    CHECK(std::ranges::contains(catalog.items, "ITEM_POCKET_EGG"));
    CHECK(std::ranges::contains(catalog.highScores, "HS_HORSE_RACE"));
    CHECK(std::ranges::contains(catalog.sounds, "NA_SE_SY_DECIDE"));

    usize boxes = 0;
    std::string problems;
    for (const toki::message::Message& message : catalog.table.messages) {
        if (!toki::message::isInLanguage(message, toki::message::Language::Japanese,
                                         catalog.isFontMessageEnglish) ||
            message.macro == toki::message::MessageMacro::FontMessage) {
            continue;
        }
        const toki::message::MessageLayout layout =
            catalog.layOut(message, toki::message::Language::Japanese);
        boxes += layout.boxes.size();
        for (const base::Diagnostic& diagnostic : layout.diagnostics) {
            problems += std::format("0x{:04X}: {}\n", message.id, diagnostic.message);
        }
    }
    WARN(boxes << " boxes");
    INFO(problems.substr(0, 2000));
    CHECK(problems.empty());

    const auto message =
        std::ranges::find(catalog.table.messages, base::u16{0x0001}, &toki::message::Message::id);
    REQUIRE(message != catalog.table.messages.end());
    const toki::message::MessageLayout layout =
        catalog.layOut(*message, toki::message::Language::Japanese);
    REQUIRE_FALSE(layout.boxes.empty());
    const toki::message::LaidBox& box = layout.boxes.front();
    CHECK(box.isWide);
    const toki::message::BoxFrame frame = toki::message::runBoxFrame(box, catalog.art);
    std::string drawn;
    for (const base::Diagnostic& diagnostic : frame.diagnostics) {
        drawn += diagnostic.message + "\n";
    }
    INFO(drawn);
    CHECK(frame.diagnostics.empty());
    CHECK(frame.rdpResult.draws.size() >= 2 * box.characters.size());
}

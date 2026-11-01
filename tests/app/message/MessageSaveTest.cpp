#include "app/message/domain/MessageSave.h"

#include "app/message/domain/Message.h"
#include "app/message/domain/MessageReader.h"
#include "app/message/domain/MessageWriter.h"
#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/SourceSet.h"
#include "hackeroot/EditPlan.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

using base::usize;
using toki::message::CodePiece;
using toki::message::Message;
using toki::message::MessageTable;
using toki::message::MessageText;
using toki::message::NewlinePiece;
using toki::message::TextPiece;

namespace {

[[nodiscard]] toki::message::MessageFiles findFixtureFiles() {
    return toki::message::findMessageFiles(
        std::filesystem::path(TOKI_TEST_FIXTURES) / "app/message/fixtures/checkout", "test");
}

} // namespace

TEST_CASE("a text is written a line of C for each line of its box, as the extracted files are",
          "[message]") {
    MessageText text{.isPresent = true};
    text.pieces = {CodePiece{.name = "UNSKIPPABLE"},
                   CodePiece{.name = "ITEM_ICON", .arguments = {"ITEM_POCKET_EGG"}},
                   TextPiece{.text = "You borrowed a "},
                   CodePiece{.name = "COLOR", .arguments = {"RED"}},
                   TextPiece{.text = "Pocket Egg"},
                   CodePiece{.name = "COLOR", .arguments = {"DEFAULT"}},
                   TextPiece{.text = "!"},
                   CodePiece{.name = "QUICKTEXT_DISABLE"},
                   NewlinePiece{},
                   TextPiece{.text = "A Pocket Cucco will hatch from"},
                   NewlinePiece{},
                   TextPiece{.text = "it overnight."}};

    CHECK(toki::message::writeMessageText(text) ==
          "MSG(\n"
          "UNSKIPPABLE ITEM_ICON(ITEM_POCKET_EGG) \"You borrowed a \" COLOR(RED) \"Pocket Egg\" "
          "COLOR(DEFAULT) \"!\" QUICKTEXT_DISABLE \"\\n\"\n"
          "\"A Pocket Cucco will hatch from\\n\"\n"
          "\"it overnight.\"\n"
          ")");
}

TEST_CASE("a text escapes what a C string cannot hold as it is", "[message]") {
    MessageText text{.isPresent = true};
    text.pieces = {TextPiece{.text = "say \"hi\" \\ \x7F"
                                     "ab"}};

    CHECK(toki::message::writeMessageText(text) == "MSG(\n\"say \\\"hi\\\" \\\\ \\x7F\" \"ab\"\n)");
    CHECK(toki::message::writeMessageText(MessageText{.placeholder = "UNUSED"}) ==
          "MSG(/* UNUSED */)");
    CHECK(toki::message::writeMessageText(MessageText{.isPresent = true}) == "MSG(\n\"\"\n)");
    CHECK(toki::message::writeMessageText(MessageText{}) == "MSG()");
}

TEST_CASE("a message file is scanned for the switches its rows stand under", "[message]") {
    constexpr std::string_view Text = "#include \"text/message_data.h\"\n"
                                      "#include \"config.h\"\n"
                                      "#if ENABLE_MM_TITLE_CARDS\n"
                                      "DEFINE_MESSAGE(0x8000, A, B, MSG(), MSG(), MSG(), MSG())\n"
                                      "#endif\n"
                                      "#if DEBUG_FEATURES\n"
                                      "#endif\n";

    const toki::message::MessageSourceScan on =
        toki::message::scanMessageSource(Text, [](std::string_view) { return true; });
    REQUIRE(on.switches.size() >= 7);
    CHECK(on.switches[3] == "ENABLE_MM_TITLE_CARDS");
    CHECK(on.switches[5].empty());
    CHECK(on.refusals.empty());
    CHECK(on.warnings.size() == 1);
    CHECK_FALSE(on.isTokiFile);

    const toki::message::MessageSourceScan off =
        toki::message::scanMessageSource(Text, [](std::string_view) { return false; });
    CHECK(off.refusals.empty());
    REQUIRE(off.keptBlocks.size() == 1);
    CHECK(off.keptBlocks[0].featureSwitch == "ENABLE_MM_TITLE_CARDS");
    CHECK(off.keptBlocks[0].text == "#if ENABLE_MM_TITLE_CARDS\n"
                                    "DEFINE_MESSAGE(0x8000, A, B, MSG(), MSG(), MSG(), MSG())\n"
                                    "#endif\n");
    CHECK(off.keptBlocks[0].endLine == 5);
}

TEST_CASE("a switch that is off with an #else cannot be kept as it stands", "[message]") {
    constexpr std::string_view Text = "#if ENABLE_MM_TITLE_CARDS\n"
                                      "#if 1\n#else\n#endif\n"
                                      "#else\n"
                                      "#endif\n";
    const toki::message::MessageSourceScan off =
        toki::message::scanMessageSource(Text, [](std::string_view) { return false; });
    CHECK(off.refusals.size() == 1);
    CHECK(off.keptBlocks.size() == 1);
}

TEST_CASE("text kept as it stands is written before the row it stood before", "[message]") {
    const cparser::MacroTable macros;
    const toki::message::MessageFiles files = findFixtureFiles();
    const MessageTable table = toki::message::readMessageTable(macros, files);
    REQUIRE(table.messages.size() > 1);
    const std::vector<toki::message::KeptText> kept = {{.beforeIndex = 1, .text = "// one\n"},
                                                       {.beforeIndex = 99, .text = "// end\n"}};
    const std::string written = toki::message::writeMessageFile(table, {}, kept);
    const usize one = written.find("// one");
    REQUIRE(one != std::string::npos);
    CHECK(written.find(toki::message::writeMessageRow(table.messages[0])) < one);
    CHECK(written.find(toki::message::writeMessageRow(table.messages[1])) > one);
    CHECK(written.ends_with("// end\n"));
}

TEST_CASE("a message file written whole reads back as the rows it was written from", "[message]") {
    const cparser::MacroTable macros;
    const toki::message::MessageFiles files = findFixtureFiles();
    const MessageTable table = toki::message::readMessageTable(macros, files);
    REQUIRE(table.messages.size() > 3);

    const std::string written = toki::message::writeMessageFile(table, {});
    CHECK_FALSE(written.contains("text/message_data.h"));

    const cparser::PendingFiles pending{{cparser::canonicalize(files.table), written}};
    const MessageTable read = toki::message::readMessageTable(macros, files, &pending);
    REQUIRE(read.messages.size() == table.messages.size());
    for (usize index = 0; index < table.messages.size(); ++index) {
        CHECK(toki::message::writeMessageRow(read.messages[index]) ==
              toki::message::writeMessageRow(table.messages[index]));
        CHECK_FALSE(read.messages[index].isVanilla);
    }
}

TEST_CASE("saving a real checkout's messages plans its message file written whole",
          "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const MessageTable table = toki::message::readMessages(*workspace);

    const std::expected<hackeroot::EditPlan, std::string> plan =
        toki::message::planMessageSave(*workspace, table);

    INFO((plan ? std::string() : plan.error()));
    REQUIRE(plan.has_value());
    REQUIRE(plan->files.size() == 1);
    const std::string& after = plan->files[0].after;
    CHECK(after.starts_with("// toki: messages\n"));
    CHECK_FALSE(after.contains("#include \"text/message_data.h\""));
    CHECK(after.contains("#if ENABLE_MM_TITLE_CARDS\n"));
    CHECK(after.contains("DEFINE_MESSAGE(0x8000"));
    CHECK(after.find("DEFINE_MESSAGE(0x8000") < after.find("DEFINE_MESSAGE(0xFFFC"));
}

TEST_CASE("every message of a real checkout written whole reads back as it was",
          "[message][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const toki::message::MessageFiles files =
        toki::message::findMessageFiles(workspace->getRoot(), workspace->getBuildDefines().version);
    const MessageTable table = toki::message::readMessages(*workspace);

    const std::string written = toki::message::writeMessageFile(table, {});
    const cparser::PendingFiles pending{{cparser::canonicalize(files.table), written}};
    const MessageTable read =
        toki::message::readMessageTable(workspace->getMacros(), files, &pending);

    CHECK(read.diagnostics.empty());
    REQUIRE(read.messages.size() == table.messages.size());
    for (usize index = 0; index < table.messages.size(); ++index) {
        INFO(index);
        REQUIRE(toki::message::writeMessageRow(read.messages[index]) ==
                toki::message::writeMessageRow(table.messages[index]));
    }
}

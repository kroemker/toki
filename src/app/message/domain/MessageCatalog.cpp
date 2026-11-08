#include "MessageCatalog.h"

#include "app/message/domain/Charmap.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageReader.h"
#include "app/message/domain/ShiftJis.h"
#include "cparser/TypeTable.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::message {

EncodedText MessageCatalog::encode(const Message& message, const Language language) const {
    const std::optional<MessageFormat>& used = language == Language::Japanese ? wideFormat : format;
    if (!used) {
        return {};
    }
    return used->makeEncoder().encode(message.texts[static_cast<usize>(language)]);
}

namespace {

void reportMissingGlyphs(MessageLayout& layout, const Message& message, const MessageArt& art,
                         const Charmap& charmap) {
    std::vector<u16> reported;
    for (const LaidBox& box : layout.boxes) {
        for (const LaidCharacter& character : box.characters) {
            if (hasFontGlyph(art, character.character) ||
                std::ranges::contains(reported, character.character)) {
                continue;
            }
            reported.push_back(character.character);
            const auto entry =
                std::ranges::find(charmap.entries, character.character, &CharmapEntry::code);
            const std::string name = entry != charmap.entries.end()
                                         ? entry->text
                                         : std::format("0x{:02X}", character.character);
            layout.diagnostics.push_back(base::makeWarning(
                message.location,
                std::format("{} has no glyph in nes_font_static; the game draws whatever "
                            "follows the font, drawn here as a red ?",
                            name)));
        }
    }
}

} // namespace

MessageLayout MessageCatalog::layOut(const Message& message, const Language language) const {
    if (!format) {
        MessageLayout failed;
        failed.diagnostics.push_back(
            base::makeError(message.location, "the message format could not be read"));
        return failed;
    }
    const std::expected<LayoutRequest, std::string> request = format->makeLayoutRequest(message);
    if (!request) {
        MessageLayout failed;
        failed.diagnostics.push_back(base::makeError(message.location, request.error()));
        return failed;
    }
    EncodedText text = encode(message, language);
    MessageLayout layout = language == Language::Japanese
                               ? layoutWideMessage(text.bytes, geometry, *request)
                               : layoutMessage(text.bytes, geometry, *request);
    std::ranges::move(text.diagnostics, std::back_inserter(layout.diagnostics));
    layout.sources = std::move(text.sources);
    if (language != Language::Japanese && hasFont(art)) {
        reportMissingGlyphs(layout, message, art, format->charmap);
    }
    return layout;
}

const Charmap* MessageCatalog::findCharmap(const Language language) const {
    const std::optional<MessageFormat>& used = language == Language::Japanese ? wideFormat : format;
    return used ? &used->charmap : nullptr;
}

MessageCatalog readMessageCatalog(const std::shared_ptr<const hackeroot::Workspace>& workspace,
                                  const std::filesystem::path& shiftJisTable,
                                  const bool readsKanji) {
    MessageCatalog catalog{.workspace = workspace};
    const MessageFiles files =
        findMessageFiles(workspace->getRoot(), workspace->getBuildDefines().version);
    catalog.files = {files.table, files.vanillaDirectory / "message_data.h",
                     workspace->getRoot() / "assets" / "text" / "charmap.nes.txt"};
    catalog.table = readMessages(*workspace);
    std::ranges::copy(catalog.table.diagnostics, std::back_inserter(catalog.diagnostics));

    std::expected<MessageFormat, base::Diagnostic> format = readMessageFormat(*workspace);
    if (format) {
        catalog.format = std::move(*format);
        constexpr std::string_view ColorPrefix = "TEXT_COLOR_";
        if (const cparser::Enumeration* colors =
                catalog.format->prelude->types.findEnumeration("TextColor")) {
            for (const cparser::Enumerator& color : colors->constants) {
                if (color.name.starts_with(ColorPrefix)) {
                    catalog.colors.push_back(color.name.substr(ColorPrefix.size()));
                }
            }
        }
    } else {
        catalog.diagnostics.push_back(std::move(format.error()));
    }
    std::expected<ShiftJis, std::string> shiftJis = ShiftJis::read(shiftJisTable);
    if (shiftJis) {
        std::expected<MessageFormat, base::Diagnostic> wide =
            readWideMessageFormat(*workspace, std::move(*shiftJis));
        if (wide) {
            catalog.wideFormat = std::move(*wide);
        } else {
            catalog.diagnostics.push_back(std::move(wide.error()));
        }
    } else {
        catalog.diagnostics.push_back(base::makeError({.file = shiftJisTable}, shiftJis.error()));
    }
    catalog.files.push_back(workspace->getRoot() / "assets" / "text" / "charmap.jpn.txt");
    catalog.geometry = readMessageGeometry(*workspace);
    std::ranges::copy(catalog.geometry.diagnostics, std::back_inserter(catalog.diagnostics));
    catalog.files.insert(catalog.files.end(), catalog.geometry.files.begin(),
                         catalog.geometry.files.end());
    catalog.art = readMessageArt(*workspace, readsKanji);
    std::ranges::copy(catalog.art.diagnostics, std::back_inserter(catalog.diagnostics));
    catalog.files.insert(catalog.files.end(), catalog.art.files.begin(), catalog.art.files.end());
    catalog.isFontMessageEnglish = isFontMessageEnglish(workspace->getMacros());
    for (const Language language : ShownLanguages) {
        if (std::ranges::any_of(catalog.table.messages, [&](const Message& message) {
                return message.texts[static_cast<usize>(language)].isPresent;
            })) {
            catalog.languages.push_back(language);
        }
    }
    return catalog;
}

} // namespace toki::message

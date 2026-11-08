#include "MessageDrawing.h"

#include "app/message/domain/MessageEncoder.h"
#include "cparser/Scanner.h"
#include "cparser/SourceFile.h"
#include "cparser/Token.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Workspace.h"
#include "n64/Memory.h"
#include "n64/Rsp.h"
#include "n64/Segment.h"
#include "z64/DisplayListEncoder.h"

#include <array>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::message {

namespace {

constexpr u8 MessageStaticSegment = 0x07;
constexpr u8 IconItemSegment = 0x08;
constexpr u8 QuestIconSegment = 0x09;
constexpr u8 FontSegment = 0x0A;
constexpr u8 KanjiSegment = 0x0B;
constexpr u8 FrameSegment = 0x0E;
constexpr u32 FirstImageAddress = 0x0010'0000;

constexpr usize SetupDisplayList39 = 39;
constexpr u32 MessageStaticTextureSize = 0x1000;
constexpr u32 FontCharacterSize = 16 * 16 / 2;
constexpr u16 FirstFontCharacter = 0x20;
constexpr u16 MissingGlyphStandIn = '?';
constexpr TextColor MissingGlyphColor{.red = 255, .green = 40, .blue = 40};
constexpr u32 ItemIconSize = 32 * 32 * 4;
constexpr u32 QuestIconSize = 24 * 24 * 4;
constexpr std::array MessageStaticIndices = {0U, 1U, 3U, 2U};
constexpr u32 EndIconTextures = 4 * MessageStaticTextureSize;

constexpr u8 BoxTypeBlack = 0;
constexpr u8 BoxTypeWooden = 1;
constexpr u8 BoxTypeBlue = 2;
constexpr u8 BoxTypeOcarina = 3;
constexpr u8 BoxTypeNoneBottom = 4;
constexpr u8 BoxTypeNoneNoShadow = 5;

constexpr s32 TextureWidth = 512;
constexpr s32 TextureHeight = 512;
constexpr s32 CharacterSize = 12;
constexpr s32 CharacterScale = 1365;
constexpr s32 WideCharacterSize = 14;
constexpr s32 WideCharacterScale = 1163;
constexpr s32 ShadowOffset = 1;
constexpr u8 TextAlpha = 255;

struct ArtSegment {
    std::string_view name;
    u8 segment = 0;
};

constexpr std::array ArtSegments = {
    ArtSegment{"message_static", MessageStaticSegment},
    ArtSegment{"icon_item_static", IconItemSegment},
    ArtSegment{"icon_item_24_static", QuestIconSegment},
    ArtSegment{"nes_font_static", FontSegment},
};

[[nodiscard]] std::string makeAddress(const u8 segment, const u32 offset) {
    return std::format("0x{:08X}", (static_cast<u32>(segment) << 24) | offset);
}

/// The box's colour, as `Message_OpenText` sets it by type.
[[nodiscard]] std::array<u32, 4> findBoxColor(const u8 boxType) {
    switch (boxType) {
    case BoxTypeBlack:
        return {0, 0, 0, 170};
    case BoxTypeWooden:
        return {70, 50, 30, 230};
    case BoxTypeBlue:
        return {0, 10, 50, 170};
    default:
        return {255, 0, 0, 180};
    }
}

class ListWriter {
public:
    explicit ListWriter(const MessageArt& art) : art(art) {}

    void writeBox(const LaidBox& box);
    void writeText(const LaidBox& box);
    void writeIcon(const LaidIcon& icon);
    void writeEndIcon(const LaidBox& box);

    [[nodiscard]] std::vector<std::string> takeCommands() {
        commands.emplace_back("gsSPEndDisplayList()");
        return std::move(commands);
    }

private:
    void add(std::string command) {
        commands.push_back(std::move(command));
    }

    void writeSetup();
    void writeRectangle(s32 left, s32 top, s32 right, s32 bottom, s32 scaleS, s32 scaleT);

    const MessageArt& art;
    std::vector<std::string> commands;
};

void ListWriter::writeSetup() {
    if (art.setupList) {
        add(std::format("gsSPDisplayList(0x{:08X})", *art.setupList));
    }
}

void ListWriter::writeRectangle(const s32 left, const s32 top, const s32 right, const s32 bottom,
                                const s32 scaleS, const s32 scaleT) {
    add(std::format("gsSPTextureRectangle({} << 2, {} << 2, {} << 2, {} << 2, G_TX_RENDERTILE, 0, "
                    "0, {}, {})",
                    left, top, right, bottom, scaleS, scaleT));
}

void ListWriter::writeBox(const LaidBox& box) {
    if (box.boxType >= BoxTypeNoneBottom) {
        return;
    }
    writeSetup();
    const std::array<u32, 4> color = findBoxColor(box.boxType);
    const std::string texture = makeAddress(
        MessageStaticSegment, MessageStaticIndices[box.boxType] * MessageStaticTextureSize);
    add("gsDPPipeSync()");
    add(std::format("gsDPSetPrimColor(0, 0, {}, {}, {}, {})", color[0], color[1], color[2],
                    color[3]));
    if (box.boxType == BoxTypeBlack || box.boxType == BoxTypeBlue) {
        add(std::format("gsDPLoadTextureBlock_4b({}, G_IM_FMT_I, 128, 64, 0, G_TX_MIRROR, "
                        "G_TX_NOMIRROR, 7, 0, G_TX_NOLOD, G_TX_NOLOD)",
                        texture));
    } else {
        add(box.boxType == BoxTypeOcarina ? "gsDPSetEnvColor(0, 0, 0, 255)"
                                          : "gsDPSetEnvColor(50, 20, 0, 255)");
        add(std::format("gsDPLoadTextureBlock_4b({}, G_IM_FMT_IA, 128, 64, 0, G_TX_MIRROR, "
                        "G_TX_MIRROR, 7, 0, G_TX_NOLOD, G_TX_NOLOD)",
                        texture));
    }
    writeRectangle(box.x, box.y, box.x + BoxWidth, box.y + BoxHeight, TextureWidth << 1,
                   TextureHeight << 1);
}

void ListWriter::writeText(const LaidBox& box) {
    writeSetup();
    add("gsDPSetAlphaCompare(G_AC_NONE)");
    add("gsDPSetCombineLERP(0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, "
        "TEXEL0, 0, PRIMITIVE, 0)");
    if (box.icon) {
        writeIcon(*box.icon);
    }
    const bool hasFont = !box.isWide || art.hasKanji;
    const u8 font = box.isWide ? KanjiSegment : FontSegment;
    const s32 size = box.isWide ? WideCharacterSize : CharacterSize;
    const s32 scale = box.isWide ? WideCharacterScale : CharacterScale;
    for (const LaidCharacter& laid : hasFont ? box.characters : std::vector<LaidCharacter>()) {
        LaidCharacter character = laid;
        if (!box.isWide && !hasFontGlyph(art, character.character)) {
            character.fontOffset = (MissingGlyphStandIn - FirstFontCharacter) * FontCharacterSize;
            character.color = MissingGlyphColor;
        }
        add("gsDPPipeSync()");
        add(std::format("gsDPLoadTextureBlock_4b({}, G_IM_FMT_I, 16, 16, 0, G_TX_NOMIRROR | "
                        "G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, "
                        "G_TX_NOLOD, G_TX_NOLOD)",
                        makeAddress(font, character.fontOffset)));
        if (box.boxType != BoxTypeNoneNoShadow) {
            add(std::format("gsDPSetPrimColor(0, 0, 0, 0, 0, {})", TextAlpha));
            writeRectangle(character.x + ShadowOffset, character.y + ShadowOffset,
                           character.x + ShadowOffset + size, character.y + ShadowOffset + size,
                           scale, scale);
        }
        add("gsDPPipeSync()");
        add(std::format("gsDPSetPrimColor(0, 0, {}, {}, {}, {})", character.color.red,
                        character.color.green, character.color.blue, TextAlpha));
        writeRectangle(character.x, character.y, character.x + size, character.y + size, scale,
                       scale);
    }
    writeEndIcon(box);
}

void ListWriter::writeIcon(const LaidIcon& icon) {
    add("gsDPPipeSync()");
    add("gsDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM)");
    add(std::format("gsDPSetPrimColor(0, 0, 255, 255, 255, {})", TextAlpha));
    add(std::format("gsDPLoadTextureBlock({}, G_IM_FMT_RGBA, G_IM_SIZ_32b, {}, {}, 0, "
                    "G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, "
                    "G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD)",
                    icon.isQuest ? makeAddress(QuestIconSegment, icon.textureIndex * QuestIconSize)
                                 : makeAddress(IconItemSegment, icon.textureIndex * ItemIconSize),
                    icon.size, icon.size));
    writeRectangle(icon.x, icon.y, icon.x + icon.size, icon.y + icon.size, 1 << 10, 1 << 10);
    add("gsDPPipeSync()");
    add("gsDPSetCombineLERP(0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, "
        "TEXEL0, 0, PRIMITIVE, 0)");
}

void ListWriter::writeEndIcon(const LaidBox& box) {
    if (box.endIcon == EndIcon::None) {
        return;
    }
    const u32 icon = box.endIcon == EndIcon::Triangle ? 0 : box.endIcon == EndIcon::Square ? 1 : 2;
    const std::array<u32, 3> primitive =
        art.hasN64ButtonColors ? std::array<u32, 3>{0, 80, 200} : std::array<u32, 3>{0, 200, 80};
    add("gsDPPipeSync()");
    add("gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, "
        "PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0)");
    add(std::format("gsDPSetPrimColor(0, 0, {}, {}, {}, 255)", primitive[0], primitive[1],
                    primitive[2]));
    add("gsDPSetEnvColor(0, 0, 0, 255)");
    add(std::format("gsDPLoadTextureBlock_4b({}, G_IM_FMT_I, 16, 16, 0, G_TX_NOMIRROR | "
                    "G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, "
                    "G_TX_NOLOD, G_TX_NOLOD)",
                    makeAddress(MessageStaticSegment, EndIconTextures + icon * FontCharacterSize)));
    const s32 size = box.isWide ? WideCharacterSize : CharacterSize;
    const s32 scale = box.isWide ? WideCharacterScale : CharacterScale;
    writeRectangle(box.endIconX, box.endIconY, box.endIconX + size, box.endIconY + size, scale,
                   scale);
}

[[nodiscard]] std::vector<cparser::Token> scanList(const cparser::SourceFile& file) {
    std::vector<cparser::Token> tokens = cparser::scan(file).tokens;
    std::erase_if(tokens, [](const cparser::Token& token) {
        return token.kind == cparser::TokenKind::EndOfFile;
    });
    return tokens;
}

} // namespace

bool hasFont(const MessageArt& art) {
    return art.image.findSegment(FontSegment) != nullptr;
}

bool hasFontGlyph(const MessageArt& art, const u16 character) {
    const z64::ImageSegment* font = art.image.findSegment(FontSegment);
    return font != nullptr && character >= FirstFontCharacter &&
           (static_cast<usize>(character - FirstFontCharacter) + 1) * FontCharacterSize <=
               font->bytes.size();
}

namespace {

[[nodiscard]] std::optional<std::vector<u8>> decodeGlyph(const MessageArt& art, const u8 segment,
                                                         const u32 offset) {
    constexpr usize GlyphPixels = 16 * 16;
    constexpr u8 IntensityScale = 0x11;
    const z64::ImageSegment* font = art.image.findSegment(segment);
    if (font == nullptr || usize{offset} + (GlyphPixels / 2) > font->bytes.size()) {
        return std::nullopt;
    }
    std::vector<u8> pixels;
    pixels.reserve(GlyphPixels * 4);
    for (usize pixel = 0; pixel < GlyphPixels; ++pixel) {
        const u8 packed = font->bytes[offset + (pixel / 2)];
        const u8 intensity = pixel % 2 == 0 ? static_cast<u8>(packed >> 4U) : packed & 0xFU;
        pixels.insert(pixels.end(),
                      {0xFF, 0xFF, 0xFF, static_cast<u8>(intensity * IntensityScale)});
    }
    return pixels;
}

} // namespace

std::optional<std::vector<u8>> decodeFontGlyph(const MessageArt& art, const u16 character) {
    if (!hasFontGlyph(art, character)) {
        return std::nullopt;
    }
    return decodeGlyph(art, FontSegment,
                       static_cast<u32>(character - FirstFontCharacter) * FontCharacterSize);
}

std::optional<std::vector<u8>> decodeKanjiGlyph(const MessageArt& art, const u32 offset) {
    return decodeGlyph(art, KanjiSegment, offset);
}

MessageArt readMessageArt(const hackeroot::Workspace& workspace, const bool readsKanji) {
    MessageArt art;
    std::vector<hackeroot::SegmentFiles> segments = {
        hackeroot::findGameCodeFiles(workspace.getRoot())};
    std::vector<ArtSegment> read(ArtSegments.begin(), ArtSegments.end());
    if (readsKanji) {
        read.push_back(ArtSegment{"kanji", KanjiSegment});
    }
    for (const ArtSegment& segment : read) {
        std::expected<std::vector<std::filesystem::path>, base::Diagnostic> sources =
            hackeroot::findSegmentSources(workspace, segment.name);
        if (!sources) {
            art.diagnostics.push_back(std::move(sources.error()));
            continue;
        }
        art.hasKanji = art.hasKanji || segment.segment == KanjiSegment;
        art.files.insert(art.files.end(), sources->begin(), sources->end());
        segments.push_back(
            hackeroot::SegmentFiles{.segment = segment.segment, .files = std::move(*sources)});
    }
    art.files.insert(art.files.end(), segments.front().files.begin(), segments.front().files.end());
    art.image = hackeroot::readAssetImage(workspace, segments);
    std::ranges::copy(art.image.diagnostics, std::back_inserter(art.diagnostics));
    art.setupList = hackeroot::findSetupDisplayList(art.image, SetupDisplayList39);
    if (!art.setupList) {
        art.diagnostics.push_back(base::makeWarning(
            {}, "src/code/z_rcp.c could not be read for sSetupDL[SETUPDL_39], so boxes are drawn "
                "without it"));
    }
    std::expected<hackeroot::Prelude, base::Diagnostic> gbi = hackeroot::readGbi(workspace);
    if (gbi) {
        art.gbi = std::make_unique<hackeroot::Prelude>(std::move(*gbi));
    } else {
        art.diagnostics.push_back(std::move(gbi.error()));
    }
    art.hasN64ButtonColors = isConditionTrue(workspace.getMacros(), "N64_BTN_COLORS");
    return art;
}

std::string writeBoxDisplayList(const LaidBox& box, const MessageArt& art) {
    ListWriter writer(art);
    writer.writeBox(box);
    writer.writeText(box);
    std::string text;
    for (const std::string& command : writer.takeCommands()) {
        text += command + ",\n";
    }
    return text;
}

z64::DisplayList encodeBoxDisplayList(const LaidBox& box, const MessageArt& art,
                                      GbiCommandCache& cache) {
    z64::DisplayList list;
    if (!art.gbi) {
        list.diagnostics.push_back(
            base::makeError({}, "the checkout's gbi.h could not be read, so no box is drawn"));
        return list;
    }
    if (cache.gbi != art.gbi.get()) {
        cache = GbiCommandCache{.gbi = art.gbi.get()};
    }
    ListWriter writer(art);
    writer.writeBox(box);
    writer.writeText(box);
    z64::DisplayListEncoder encoder(z64::DisplayListContext{
        .macros = &art.gbi->macros,
        .findSymbol = [](std::string_view) { return std::optional<cparser::SymbolAddress>(); },
        .findArrayLength = [](std::string_view) { return std::optional<usize>(); }});
    for (std::string& command : writer.takeCommands()) {
        auto found = cache.commands.find(command);
        if (found == cache.commands.end()) {
            const cparser::SourceFile file{.path = "<message box>", .contents = command + ",\n"};
            z64::DisplayList encoded = encoder.encode(scanList(file));
            if (!encoded.diagnostics.empty()) {
                std::ranges::move(encoded.diagnostics, std::back_inserter(list.diagnostics));
                continue;
            }
            found = cache.commands.emplace(std::move(command), std::move(encoded.bytes)).first;
        }
        list.bytes.insert(list.bytes.end(), found->second.begin(), found->second.end());
    }
    return list;
}

BoxFrame runBoxFrame(const LaidBox& box, const MessageArt& art) {
    GbiCommandCache cache;
    return runBoxFrame(box, art, cache);
}

BoxFrame runBoxFrame(const LaidBox& box, const MessageArt& art, GbiCommandCache& cache) {
    BoxFrame frame;
    z64::DisplayList list = encodeBoxDisplayList(box, art, cache);
    std::ranges::move(list.diagnostics, std::back_inserter(frame.diagnostics));
    if (list.bytes.empty()) {
        return frame;
    }
    n64::Memory memory;
    const std::expected<z64::MappedImage, base::Diagnostic> mapped =
        z64::mapImageAt(art.image, memory, FirstImageAddress);
    if (!mapped) {
        frame.diagnostics.push_back(mapped.error());
        return frame;
    }
    const u32 listAddress = (mapped->end + 0xF) & ~u32{0xF};
    if (const std::expected<void, base::Diagnostic> placed = memory.map(listAddress, list.bytes);
        !placed) {
        frame.diagnostics.push_back(placed.error());
        return frame;
    }
    std::array<u32, n64::SegmentCount> segments = mapped->segments;
    segments[FrameSegment] = listAddress;
    frame.rspResult =
        n64::runTask(memory, n64::RspTask{.displayList = static_cast<u32>(FrameSegment) << 24,
                                          .segments = segments});
    frame.rdpResult = n64::runRdp(frame.rspResult, memory);
    std::ranges::copy(frame.rspResult.diagnostics, std::back_inserter(frame.diagnostics));
    std::ranges::copy(frame.rdpResult.diagnostics, std::back_inserter(frame.diagnostics));
    return frame;
}

} // namespace toki::message

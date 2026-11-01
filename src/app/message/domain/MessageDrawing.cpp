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
constexpr u8 FrameSegment = 0x0E;
constexpr u32 FirstImageAddress = 0x0010'0000;

constexpr usize SetupDisplayList39 = 39;
constexpr u32 MessageStaticTextureSize = 0x1000;
constexpr u32 FontCharacterSize = 16 * 16 / 2;
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

    [[nodiscard]] std::string takeText() {
        return std::move(text);
    }

private:
    void add(std::string_view command) {
        text += command;
        text += ",\n";
    }

    void writeSetup();
    void writeRectangle(s32 left, s32 top, s32 right, s32 bottom, s32 scaleS, s32 scaleT);

    const MessageArt& art;
    std::string text;
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
    for (const LaidCharacter& character : box.characters) {
        add("gsDPPipeSync()");
        add(std::format(
            "gsDPLoadTextureBlock_4b({}, G_IM_FMT_I, 16, 16, 0, G_TX_NOMIRROR | "
            "G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, "
            "G_TX_NOLOD, G_TX_NOLOD)",
            makeAddress(FontSegment, (character.character - u32{' '}) * FontCharacterSize)));
        if (box.boxType != BoxTypeNoneNoShadow) {
            add(std::format("gsDPSetPrimColor(0, 0, 0, 0, 0, {})", TextAlpha));
            writeRectangle(character.x + ShadowOffset, character.y + ShadowOffset,
                           character.x + ShadowOffset + CharacterSize,
                           character.y + ShadowOffset + CharacterSize, CharacterScale,
                           CharacterScale);
        }
        add("gsDPPipeSync()");
        add(std::format("gsDPSetPrimColor(0, 0, {}, {}, {}, {})", character.color.red,
                        character.color.green, character.color.blue, TextAlpha));
        writeRectangle(character.x, character.y, character.x + CharacterSize,
                       character.y + CharacterSize, CharacterScale, CharacterScale);
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
    writeRectangle(box.endIconX, box.endIconY, box.endIconX + CharacterSize,
                   box.endIconY + CharacterSize, CharacterScale, CharacterScale);
}

[[nodiscard]] std::vector<cparser::Token> scanList(const cparser::SourceFile& file) {
    std::vector<cparser::Token> tokens = cparser::scan(file).tokens;
    std::erase_if(tokens, [](const cparser::Token& token) {
        return token.kind == cparser::TokenKind::EndOfFile;
    });
    return tokens;
}

} // namespace

MessageArt readMessageArt(const hackeroot::Workspace& workspace) {
    MessageArt art;
    std::vector<hackeroot::SegmentFiles> segments = {
        hackeroot::findGameCodeFiles(workspace.getRoot())};
    for (const ArtSegment& segment : ArtSegments) {
        std::expected<std::vector<std::filesystem::path>, base::Diagnostic> sources =
            hackeroot::findSegmentSources(workspace, segment.name);
        if (!sources) {
            art.diagnostics.push_back(std::move(sources.error()));
            continue;
        }
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
    return writer.takeText() + "gsSPEndDisplayList(),\n";
}

z64::DisplayList encodeBoxDisplayList(const LaidBox& box, const MessageArt& art) {
    if (!art.gbi) {
        z64::DisplayList failed;
        failed.diagnostics.push_back(
            base::makeError({}, "the checkout's gbi.h could not be read, so no box is drawn"));
        return failed;
    }
    const cparser::SourceFile file{.path = "<message box>",
                                   .contents = writeBoxDisplayList(box, art)};
    const std::vector<cparser::Token> tokens = scanList(file);
    z64::DisplayListEncoder encoder(z64::DisplayListContext{
        .macros = &art.gbi->macros,
        .findSymbol = [](std::string_view) { return std::optional<cparser::SymbolAddress>(); },
        .findArrayLength = [](std::string_view) { return std::optional<usize>(); }});
    return encoder.encode(tokens);
}

BoxFrame runBoxFrame(const LaidBox& box, const MessageArt& art) {
    BoxFrame frame;
    z64::DisplayList list = encodeBoxDisplayList(box, art);
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

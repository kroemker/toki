#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"
#include "n64/Memory.h"
#include "n64/Rsp.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using base::u16;
using base::u32;
using base::u8;
using base::usize;

namespace {

constexpr std::array<std::string_view, 5> ParentNamed{"_PolyList", "_VtxList", "_SurfaceTypes",
                                                      "_BgCamList", "_WaterBoxes"};

std::string readName(const std::vector<u8>& bytes, const usize at) {
    std::string name;
    for (usize i = at; i < bytes.size() && bytes[i] != 0; ++i) {
        name.push_back(static_cast<char>(bytes[i]));
    }
    return name;
}

u32 readBig(const std::vector<u8>& bytes, const usize at, const usize size) {
    u32 value = 0;
    for (usize i = 0; i < size; ++i) {
        value = (value << 8) | bytes[at + i];
    }
    return value;
}

/// The contents of the ELF section named `wanted` in a big-endian 32-bit
/// object file, or nothing.
std::optional<std::vector<u8>> readSection(const std::filesystem::path& file,
                                           const std::string_view wanted) {
    std::ifstream stream(file, std::ios::binary);
    const std::vector<u8> elf((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    if (elf.size() < 0x34) {
        return std::nullopt;
    }
    const u32 headers = readBig(elf, 0x20, 4);
    const u32 headerSize = readBig(elf, 0x2E, 2);
    const u32 count = readBig(elf, 0x30, 2);
    const u32 names = readBig(elf, headers + (readBig(elf, 0x32, 2) * headerSize) + 0x10, 4);
    for (u32 i = 0; i < count; ++i) {
        const usize header = headers + (i * headerSize);
        const std::string name = readName(elf, names + readBig(elf, header, 4));
        if (name == wanted) {
            const u32 offset = readBig(elf, header + 0x10, 4);
            const u32 size = readBig(elf, header + 0x14, 4);
            return std::vector<u8>(elf.begin() + offset, elf.begin() + offset + size);
        }
    }
    return std::nullopt;
}

struct BuiltSymbol {
    u32 offset = 0;
    u32 size = 0;
};

/// The defined data symbols of a big-endian 32-bit object file.
std::map<std::string, BuiltSymbol> readSymbols(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    const std::vector<u8> elf((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    std::map<std::string, BuiltSymbol> symbols;
    if (elf.size() < 0x34) {
        return symbols;
    }
    const u32 headers = readBig(elf, 0x20, 4);
    const u32 headerSize = readBig(elf, 0x2E, 2);
    const u32 count = readBig(elf, 0x30, 2);
    for (u32 i = 0; i < count; ++i) {
        const usize header = headers + (i * headerSize);
        if (readBig(elf, header + 4, 4) != 2) {
            continue;
        }
        const u32 table = readBig(elf, header + 0x10, 4);
        const u32 size = readBig(elf, header + 0x14, 4);
        const u32 link = readBig(elf, header + 0x18, 4);
        const u32 names = readBig(elf, headers + (link * headerSize) + 0x10, 4);
        for (u32 at = table; at + 16 <= table + size; at += 16) {
            const u32 section = readBig(elf, at + 14, 2);
            if (section == 0 || section >= 0xFF00 || (elf[at + 12] & 0xF) != 1) {
                continue;
            }
            const std::string name = readName(elf, names + readBig(elf, at, 4));
            symbols[name] =
                BuiltSymbol{.offset = readBig(elf, at + 4, 4), .size = readBig(elf, at + 8, 4)};
        }
    }
    return symbols;
}

std::string describeLayout(const z64::MemoryImage& image, const u8 segment,
                           const std::filesystem::path& object) {
    std::string first;
    u32 firstOffset = 0xFFFFFFFF;
    for (const auto& [name, built] : readSymbols(object)) {
        const std::optional<z64::ImageSymbol> ours = image.findSymbol(name);
        if (!ours || ours->segment != segment) {
            continue;
        }
        const bool differs = ours->offset != built.offset || ours->size != built.size;
        if (differs && built.offset < firstOffset) {
            firstOffset = built.offset;
            first = std::format("{} is 0x{:X} bytes at 0x{:X}, the build's 0x{:X} at 0x{:X}", name,
                                ours->size, ours->offset, built.size, built.offset);
        }
    }
    return first;
}

/// The offset an extracted symbol's name records: the last address in it,
/// `spot00_room_0_03000520_RoomShapeCullable` is at 0x520.
std::optional<usize> findNamedOffset(const std::string_view name, const u8 segment) {
    std::optional<usize> offset;
    usize start = 0;
    while (start < name.size()) {
        const usize end = std::min(name.find('_', start), name.size());
        const std::string_view part = name.substr(start, end - start);
        const bool isHex = !part.empty() && std::ranges::all_of(part, [](const char c) {
            return std::isxdigit(static_cast<unsigned char>(c)) != 0 &&
                   (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
                    std::isupper(static_cast<unsigned char>(c)) != 0);
        });
        if (isHex && part.size() == 8) {
            const u32 value = std::stoul(std::string(part), nullptr, 16);
            if ((value >> 24) == segment) {
                offset = value & 0xFFFFFF;
            }
        }
        start = end + 1;
    }
    return offset;
}

std::vector<u8> unpatch(const z64::MemoryImage& image, const z64::ImageSegment& segment) {
    std::vector<u8> bytes = segment.bytes;
    for (const z64::Relocation& relocation : segment.relocations) {
        const u32 address = image.findAddress(relocation.symbol).value_or(0);
        const u32 word = readBig(bytes, relocation.offset, 4) - address;
        for (usize i = 0; i < 4; ++i) {
            bytes[relocation.offset + i] = static_cast<u8>(word >> (8 * (3 - i)));
        }
    }
    return bytes;
}

std::string findSymbolAt(const z64::MemoryImage& image, const u8 segment, const usize offset) {
    for (const auto& [name, symbol] : image.symbols) {
        if (symbol.segment == segment && offset >= symbol.offset &&
            offset < symbol.offset + symbol.size) {
            return std::format("{}+0x{:X}", name, offset - symbol.offset);
        }
    }
    return "no symbol";
}

std::string describeDifference(const z64::MemoryImage& image, const u8 segment,
                               const std::vector<u8>& ours, const std::vector<u8>& built) {
    const usize size = std::min(ours.size(), built.size());
    for (usize at = 0; at < size; ++at) {
        if (ours[at] != built[at]) {
            return std::format("first difference at 0x{:X} ({}): 0x{:02X} where the build has "
                               "0x{:02X}",
                               at, findSymbolAt(image, segment, at), ours[at], built[at]);
        }
    }
    return std::format("sizes 0x{:X} and 0x{:X}", ours.size(), built.size());
}

} // namespace

TEST_CASE("a vanilla scene and room lay out and encode as the build compiles them",
          "[hackeroot][image][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const auto spot00 = std::ranges::find(workspace->getAssets().scenes, std::string("spot00"),
                                          &hackeroot::AssetDirectory::name);
    REQUIRE(spot00 != workspace->getAssets().scenes.end());
    const std::filesystem::path scene = spot00->directory / "spot00_scene.c";
    const std::filesystem::path room = spot00->directory / "spot00_room_0.c";
    const std::array segments = {hackeroot::SegmentFiles{.segment = 2, .files = {scene}},
                                 hackeroot::SegmentFiles{.segment = 3, .files = {room}}};

    const z64::MemoryImage image = hackeroot::readAssetImage(*workspace, segments);

    std::string listed;
    usize loud = 0;
    for (const base::Diagnostic& diagnostic : image.diagnostics) {
        if (diagnostic.severity != base::Severity::Note && loud++ < 20) {
            listed += diagnostic.location.file.filename().string() + ":" +
                      std::to_string(diagnostic.location.line) + " " + diagnostic.message + "\n";
        }
    }
    INFO(listed);
    CHECK(loud == 0);

    usize named = 0;
    std::string misplaced;
    for (const auto& [name, symbol] : image.symbols) {
        const std::optional<usize> offset = findNamedOffset(name, symbol.segment);
        const bool namesParent = std::ranges::any_of(
            ParentNamed, [&name](const std::string_view kind) { return name.ends_with(kind); });
        if (!offset || namesParent) {
            continue;
        }
        ++named;
        if (*offset != symbol.offset && misplaced.size() < 2000) {
            misplaced += std::format("{} at 0x{:X}, named 0x{:X}\n", name, symbol.offset, *offset);
        }
    }
    INFO(misplaced);
    CHECK(named > 100);
    CHECK(misplaced.empty());

    const std::filesystem::path extracted =
        repository / "extracted" / workspace->getBuildDefines().version;
    const std::filesystem::path built = repository / "build" / workspace->getBuildDefines().version;
    for (const auto& [number, file] : {std::pair{u8{2}, scene}, std::pair{u8{3}, room}}) {
        const z64::ImageSegment* segment = image.findSegment(number);
        REQUIRE(segment != nullptr);
        std::filesystem::path object = built / file.lexically_relative(extracted);
        object.replace_extension(".o");
        const std::optional<std::vector<u8>> data = readSection(object, ".data");
        REQUIRE(data.has_value());
        const std::vector<u8> ours = unpatch(image, *segment);
        INFO(object.filename().string() << ": " << describeDifference(image, number, ours, *data));
        CHECK(ours == *data);
    }

    const auto dl = std::ranges::find_if(image.symbols, [](const auto& entry) {
        return entry.second.segment == 3 && entry.first.ends_with("_DL");
    });
    REQUIRE(dl != image.symbols.end());
    n64::Memory memory;
    const auto table = z64::mapImage(image, memory);
    REQUIRE(table.has_value());
    const n64::RspResult result = n64::runTask(
        memory, n64::RspTask{.displayList = *image.findAddress(dl->first), .segments = *table});
    WARN(dl->first << ": " << result.outputs.size() << " outputs, " << result.diagnostics.size()
                   << " diagnostics");
    REQUIRE_FALSE(result.outputs.empty());
}

namespace {

struct Sweep {
    usize compared = 0;
    usize problems = 0;
    std::map<std::string, usize> kinds;
    std::string mismatches;

    [[nodiscard]] std::string describe() const {
        std::string listed = mismatches;
        for (const auto& [message, count] : kinds) {
            if (listed.size() < 6000) {
                listed += std::format("{}x {}\n", count, message);
            }
        }
        return listed;
    }
};

/// Reads every C file of `folder` the build compiles into one image, each its
/// own segment so a room may use its scene, and compares each with the build's
/// object. Objects point into other objects, gameplay_keep above all, which a
/// folder of its own cannot define, so they are not `isSelfContained`.
void sweepFolder(const hackeroot::Workspace& workspace, const hackeroot::AssetDirectory& folder,
                 const u8 first, const bool isSelfContained, Sweep& sweep) {
    const std::filesystem::path& repository = workspace.getRoot();
    const std::string& version = workspace.getBuildDefines().version;
    const std::filesystem::path source =
        folder.isExtracted ? repository / "extracted" / version : repository;
    std::vector<hackeroot::SegmentFiles> segments;
    std::error_code failure;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(folder.directory, failure)) {
        const std::string name = entry.path().filename().string();
        if (!name.ends_with(".c") || name.ends_with(".inc.c")) {
            continue;
        }
        std::filesystem::path object =
            repository / "build" / version / entry.path().lexically_relative(source);
        object.replace_extension(".o");
        if (!std::filesystem::exists(object)) {
            continue;
        }
        const bool isScene = name.ends_with("_scene.c");
        const u8 number = isScene ? u8{2} : static_cast<u8>(first + segments.size());
        segments.push_back(hackeroot::SegmentFiles{.segment = number, .files = {entry.path()}});
    }
    const z64::MemoryImage image = hackeroot::readAssetImage(workspace, segments);
    for (const base::Diagnostic& diagnostic : image.diagnostics) {
        const bool isElsewhere = diagnostic.message.contains("defined nowhere");
        if (diagnostic.severity != base::Severity::Note && (isSelfContained || !isElsewhere)) {
            ++sweep.problems;
            ++sweep.kinds[diagnostic.message];
        }
    }
    for (const hackeroot::SegmentFiles& segment : segments) {
        std::filesystem::path object =
            repository / "build" / version / segment.files.front().lexically_relative(source);
        object.replace_extension(".o");
        const std::optional<std::vector<u8>> data = readSection(object, ".data");
        const z64::ImageSegment* ours = image.findSegment(segment.segment);
        ++sweep.compared;
        if (!data || ours == nullptr) {
            ++sweep.problems;
            sweep.mismatches += object.filename().string() + ": nothing to compare\n";
            continue;
        }
        const std::vector<u8> bytes = unpatch(image, *ours);
        if (bytes != *data) {
            ++sweep.problems;
            if (sweep.mismatches.size() < 3000) {
                sweep.mismatches +=
                    std::format("{}: {}; {}\n", object.filename().string(),
                                describeDifference(image, segment.segment, bytes, *data),
                                describeLayout(image, segment.segment, object));
            }
        }
    }
}

std::optional<hackeroot::Workspace> loadCheckout() {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        return std::nullopt;
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    return std::move(*workspace);
}

} // namespace

TEST_CASE("every scene and room encodes byte for byte as the build compiles it",
          "[hackeroot][image][checkout]") {
    const std::optional<hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    Sweep sweep;
    for (const hackeroot::AssetDirectory& folder : workspace->getAssets().scenes) {
        sweepFolder(*workspace, folder, 3, true, sweep);
    }
    WARN(sweep.compared << " scene and room files compared, " << sweep.problems << " problems");
    INFO(sweep.describe());
    REQUIRE(sweep.compared > 300);
    REQUIRE(sweep.problems == 0);
}

TEST_CASE("every object encodes byte for byte as the build compiles it",
          "[hackeroot][image][checkout]") {
    const std::optional<hackeroot::Workspace> workspace = loadCheckout();
    if (!workspace) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    Sweep sweep;
    for (const hackeroot::AssetDirectory& folder : workspace->getAssets().objects) {
        sweepFolder(*workspace, folder, 6, false, sweep);
    }
    WARN(sweep.compared << " object files compared, " << sweep.problems << " problems");
    INFO(sweep.describe());
    REQUIRE(sweep.compared > 300);
    REQUIRE(sweep.problems == 0);
}

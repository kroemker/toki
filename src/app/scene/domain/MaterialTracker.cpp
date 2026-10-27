#include "MaterialTracker.h"

#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr u8 SetTileOpcode = 0xF5;
constexpr u8 SetTileSizeOpcode = 0xF2;
constexpr u8 LoadBlockOpcode = 0xF3;
constexpr u8 LoadTileOpcode = 0xF4;
constexpr u8 LoadTlutOpcode = 0xF0;
constexpr u8 SetTextureImageOpcode = 0xFD;
constexpr u8 LoadTile = 7;
constexpr u64 HashStart = 0xCBF2'9CE4'8422'2325ULL;
constexpr u64 HashPrime = 0x0000'0100'0000'01B3ULL;

[[nodiscard]] bool isStateOpcode(const u8 opcode) {
    switch (opcode) {
    case 0xEF:
    case 0xF8:
    case 0xF9:
    case 0xFA:
    case 0xFB:
    case 0xFC:
    case SetTileOpcode:
    case SetTileSizeOpcode:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isSyncOpcode(const u8 opcode) {
    return opcode >= 0xE6 && opcode <= 0xE9;
}

void addToHash(u64& hash, const u64 value) {
    for (usize byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xFFU;
        hash *= HashPrime;
    }
}

[[nodiscard]] bool isMaterialList(const std::string& name) {
    return name.starts_with("mat_") && !name.starts_with("mat_revert_");
}

} // namespace

MaterialTracker::MaterialTracker(const Scene& scene, const viewport::Frame& frame) : scene(scene) {
    for (usize image = SceneImage; image < scene.images.size(); ++image) {
        if (image >= frame.mappedImages.size() || !frame.mappedImages[image]) {
            continue;
        }
        for (const auto& [name, symbol] : scene.images[image]->symbols) {
            const u32 start = frame.mappedImages[image]->segments[symbol.segment] +
                              static_cast<u32>(symbol.offset);
            symbols.push_back(MappedSymbol{.start = start,
                                           .end = start + static_cast<u32>(symbol.size),
                                           .image = image,
                                           .name = &name});
        }
    }
    std::ranges::sort(symbols, {}, &MappedSymbol::start);
}

const MaterialTracker::MappedSymbol* MaterialTracker::findSymbol(const u32 address) const {
    const auto after = std::ranges::upper_bound(symbols, address, {}, &MappedSymbol::start);
    if (after == symbols.begin()) {
        return nullptr;
    }
    const MappedSymbol& symbol = *std::prev(after);
    return address < symbol.end ? &symbol : nullptr;
}

u64 MaterialTracker::findContent(const MappedSymbol& symbol, const u32 address) const {
    auto found = contents.find(*symbol.name);
    if (found == contents.end()) {
        u64 hash = HashStart;
        const z64::MemoryImage& image = *scene.images[symbol.image];
        const auto described = image.symbols.find(*symbol.name);
        if (described != image.symbols.end()) {
            const u32 segmented =
                (u32{described->second.segment} << 24) | static_cast<u32>(described->second.offset);
            if (const std::optional<std::span<const u8>> bytes =
                    image.findBytes(segmented, described->second.size)) {
                for (const u8 byte : *bytes) {
                    hash ^= byte;
                    hash *= HashPrime;
                }
            }
        }
        found = contents.emplace(*symbol.name, hash).first;
    }
    u64 hash = found->second;
    addToHash(hash, address - symbol.start);
    return hash;
}

void MaterialTracker::read(const n64::RdpCommand& command) {
    const u8 opcode = static_cast<u8>(command.word0 >> 24);
    if (!isSyncOpcode(opcode)) {
        const MappedSymbol* source = findSymbol(command.address);
        stateList = source != nullptr ? *source->name : std::string();
    }
    if (opcode == SetTextureImageOpcode) {
        textureImage = command.word1;
        return;
    }
    if (opcode == SetTileOpcode) {
        tileMemory[static_cast<u8>((command.word1 >> 24) & 7U)] = command.word0 & 0x1FFU;
    }
    if (opcode == LoadBlockOpcode || opcode == LoadTileOpcode || opcode == LoadTlutOpcode) {
        const u8 tile = static_cast<u8>((command.word1 >> 24) & 7U);
        const auto memory = tileMemory.find(tile);
        const MappedSymbol* texture = findSymbol(textureImage);
        if (memory != tileMemory.end()) {
            loaded[memory->second] =
                texture != nullptr ? std::pair{findContent(*texture, textureImage), *texture->name}
                                   : std::pair{u64{textureImage}, std::string()};
        }
        return;
    }
    if (!isStateOpcode(opcode)) {
        return;
    }
    u32 key = u32{opcode} << 8;
    if (opcode == SetTileOpcode || opcode == SetTileSizeOpcode) {
        const u32 tile = (command.word1 >> 24) & 7U;
        if (tile == LoadTile) {
            return;
        }
        key |= tile;
    }
    words[key] = {command.word0, command.word1};
}

MeshMaterial MaterialTracker::makeMaterial(const n64::Triangle& triangle) const {
    u64 hash = HashStart;
    for (const auto& [key, pair] : words) {
        addToHash(hash, key);
        addToHash(hash, (u64{pair.first} << 32) | pair.second);
    }
    for (const auto& [memory, texture] : loaded) {
        addToHash(hash, memory);
        addToHash(hash, texture.first);
    }
    addToHash(hash, (u64{triangle.textured} << 0) | (u64{triangle.shade} << 1) |
                        (u64{triangle.lit} << 2) | (u64{triangle.depth} << 3) |
                        (u64{triangle.tile} << 8) | (u64{triangle.levels} << 16));
    MeshMaterial material{.content = hash};
    if (isMaterialList(stateList)) {
        material.list = stateList;
    }
    if (triangle.textured) {
        const auto tile = tileMemory.find(triangle.tile);
        const auto texture = tile != tileMemory.end() ? loaded.find(tile->second) : loaded.end();
        if (texture != loaded.end()) {
            material.texture = texture->second.second;
        } else if (!loaded.empty()) {
            material.texture = loaded.begin()->second.second;
        }
    }
    return material;
}

} // namespace toki::scene

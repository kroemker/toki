#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/RoomMesh.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace n64 {
struct RdpCommand;
struct Triangle;
} // namespace n64

namespace toki::viewport {
struct Frame;
} // namespace toki::viewport

namespace toki::scene {

struct Scene;

/// Follows the RDP state a frame's commands set, to tell what each triangle is
/// drawn under: the commands that set its combiner, render mode, colours and
/// tiles, and what was loaded into texture memory, by the bytes it was loaded
/// from, so that two rooms' copies of one texture are the same.
class MaterialTracker {
public:
    MaterialTracker(const Scene& scene, const viewport::Frame& frame);

    void read(const n64::RdpCommand& command);

    /// What `triangle`, drawn now, is drawn under.
    [[nodiscard]] MeshMaterial makeMaterial(const n64::Triangle& triangle) const;

private:
    /// A symbol of the scene's images, where the frame mapped it.
    struct MappedSymbol {
        u32 start = 0;
        u32 end = 0;
        usize image = 0;
        const std::string* name = nullptr;
    };

    [[nodiscard]] const MappedSymbol* findSymbol(u32 address) const;
    [[nodiscard]] u64 findContent(const MappedSymbol& symbol, u32 address) const;

    const Scene& scene;
    std::vector<MappedSymbol> symbols;
    std::map<u32, std::pair<u32, u32>> words;
    std::map<u8, u32> tileMemory;
    u32 textureImage = 0;
    /// What each address of texture memory was loaded from: the bytes'
    /// content, and the symbol's name.
    std::map<u32, std::pair<u64, std::string>> loaded;
    std::string stateList;
    mutable std::map<std::string, u64> contents;
};

} // namespace toki::scene

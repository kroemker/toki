#include "MeshRebuild.h"

#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize CommandSize = 8;
constexpr usize VtxSize = 16;
constexpr usize NormalOffset = 12;
constexpr usize VertexBufferSize = 32;
constexpr u8 VertexOpcode = 0x01;
constexpr u8 Triangle1Opcode = 0x05;
constexpr u8 Triangle2Opcode = 0x06;
constexpr u8 QuadOpcode = 0x07;
constexpr f32 PlaceSteps = 8.0F;
constexpr f32 Flat = 1.0e-6F;

[[nodiscard]] bool isRunCommand(const u8 opcode) {
    return opcode == VertexOpcode || opcode == Triangle1Opcode || opcode == Triangle2Opcode ||
           opcode == QuadOpcode;
}

/// Commands of a list that only load vertices and draw triangles, from
/// `first` to before `end`.
struct Run {
    usize first = 0;
    usize end = 0;
    usize triangles = 0;
    /// Whether every triangle draws vertices the run itself loaded.
    bool isSelfContained = true;
};

[[nodiscard]] bool hasLoaded(const std::array<bool, VertexBufferSize>& loaded, const u32 word) {
    for (const u32 shift : {16U, 8U, 0U}) {
        const u32 slot = ((word >> shift) & 0xFFU) / 2;
        if (slot >= VertexBufferSize || !loaded[slot]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<Run> findRuns(const std::vector<u8>& bytes) {
    std::vector<Run> runs;
    const usize count = bytes.size() / CommandSize;
    std::array<bool, VertexBufferSize> loaded{};
    for (usize command = 0; command < count; ++command) {
        const u8 opcode = bytes[command * CommandSize];
        if (!isRunCommand(opcode)) {
            continue;
        }
        if (runs.empty() || runs.back().end != command) {
            runs.push_back(Run{.first = command, .end = command});
            loaded = {};
        }
        Run& run = runs.back();
        run.end = command + 1;
        const u32 word0 = n64::readU32(bytes, command * CommandSize);
        const u32 word1 = n64::readU32(bytes, (command * CommandSize) + 4);
        if (opcode == VertexOpcode) {
            const u32 loadedCount = (word0 >> 12) & 0xFFU;
            const u32 end = (word0 >> 1) & 0x7FU;
            for (u32 slot = end - std::min(loadedCount, end); slot < end; ++slot) {
                if (slot < VertexBufferSize) {
                    loaded[slot] = true;
                }
            }
        } else if (opcode == Triangle1Opcode) {
            run.triangles += 1;
            run.isSelfContained = run.isSelfContained && hasLoaded(loaded, word0);
        } else {
            run.triangles += 2;
            run.isSelfContained =
                run.isSelfContained && hasLoaded(loaded, word0) && hasLoaded(loaded, word1);
        }
    }
    return runs;
}

[[nodiscard]] std::optional<usize> findRun(const std::vector<Run>& runs, const usize command) {
    for (usize run = 0; run < runs.size(); ++run) {
        if (runs[run].first <= command && command < runs[run].end) {
            return run;
        }
    }
    return std::nullopt;
}

void addWord(std::vector<u8>& bytes, const u32 value) {
    for (const u32 shift : {24U, 16U, 8U, 0U}) {
        bytes.push_back(static_cast<u8>(value >> shift));
    }
}

[[nodiscard]] u32 packTriangle(const std::array<usize, 3>& slots) {
    return (static_cast<u32>(slots[0] * 2) << 16) | (static_cast<u32>(slots[1] * 2) << 8) |
           static_cast<u32>(slots[2] * 2);
}

/// A face of a draft or of the mesh, by its draft and index.
struct FaceAt {
    usize draft = 0;
    usize face = 0;
};

/// A run as it is written again: its commands, the words of them that load
/// from its vertex array, and that array.
struct RunOutput {
    std::vector<u8> commands;
    std::vector<usize> loads;
    std::vector<u8> vertices;
    std::string array;
    std::vector<std::pair<FaceAt, std::array<usize, 3>>> faceEntries;
};

struct ListState {
    z64::SymbolData symbol;
    std::vector<Run> runs;
    std::vector<std::vector<FaceAt>> original;
    std::vector<std::vector<FaceAt>> drafted;
    std::vector<bool> isRebuilt;
    std::vector<RunOutput> outputs;
    /// Where each command of the old list starts in the new one; a command
    /// of a rebuilt run but its first has none.
    std::vector<std::optional<usize>> moved;
};

using PlaceKey = std::array<s64, 3>;

[[nodiscard]] PlaceKey makePlaceKey(const glm::vec3& position) {
    return {std::llround(position.x * PlaceSteps), std::llround(position.y * PlaceSteps),
            std::llround(position.z * PlaceSteps)};
}

[[nodiscard]] glm::vec3 findVtxWorld(const glm::mat4& transform, const std::array<u8, 16>& vtx) {
    return glm::vec3(transform * glm::vec4(static_cast<f32>(n64::readS16(vtx, 0)),
                                           static_cast<f32>(n64::readS16(vtx, 2)),
                                           static_cast<f32>(n64::readS16(vtx, 4)), 1.0F));
}

using FaceSignature = std::array<PlaceKey, 3>;

class RebuildPlanner {
public:
    RebuildPlanner(const Scene& scene, const SceneModel& model, const SceneMesh& mesh,
                   const std::span<const RoomDraft> drafts)
        : scene(scene), model(model), mesh(mesh), drafts(drafts) {}

    [[nodiscard]] std::expected<MeshRebuild, base::Diagnostic> plan() {
        if (std::expected<void, base::Diagnostic> sorted = sortFaces(); !sorted) {
            return std::unexpected(sorted.error());
        }
        findChanges();
        if (std::expected<void, base::Diagnostic> marked = markRebuilt(); !marked) {
            return std::unexpected(marked.error());
        }
        findTurned();
        for (auto& [name, list] : lists) {
            for (usize run = 0; run < list.runs.size(); ++run) {
                if (list.isRebuilt[run]) {
                    writeRun(list, run);
                }
            }
        }
        nameArrays();
        for (auto& [name, list] : lists) {
            if (std::ranges::contains(list.isRebuilt, true)) {
                assembleList(list);
                rebuild.lists.push_back(name);
            }
        }
        if (std::expected<void, base::Diagnostic> patched = patchPointers(); !patched) {
            return std::unexpected(patched.error());
        }
        writeArrays();
        writeNormals();
        writeBounds();
        makeKept();
        for (auto& [name, symbol] : changedSymbols) {
            rebuild.changed.push_back(std::move(symbol));
        }
        return std::move(rebuild);
    }

private:
    [[nodiscard]] const RoomMesh& getRoom(const usize draft) const {
        return *findRoomMesh(mesh, drafts[draft].room);
    }

    [[nodiscard]] std::expected<ListState*, base::Diagnostic> findList(const MeshDraw& draw) {
        if (draw.list.empty()) {
            return std::unexpected(
                base::makeError({}, "a face is drawn by no display list of the scene"));
        }
        auto found = lists.find(draw.list);
        if (found == lists.end()) {
            const z64::SymbolData* symbol = model.findSymbol(draw.list);
            if (symbol == nullptr) {
                return std::unexpected(base::makeError(
                    {}, std::format("the scene has no display list '{}'", draw.list)));
            }
            ListState state{.symbol = *symbol, .runs = findRuns(symbol->bytes)};
            state.original.resize(state.runs.size());
            state.drafted.resize(state.runs.size());
            state.isRebuilt.assign(state.runs.size(), false);
            state.outputs.resize(state.runs.size());
            found = lists.emplace(draw.list, std::move(state)).first;
        }
        return &found->second;
    }

    [[nodiscard]] std::expected<usize, base::Diagnostic> findRunOf(const ListState& list,
                                                                   const MeshDraw& draw) const {
        const std::optional<usize> run = findRun(list.runs, draw.command);
        if (!run) {
            return std::unexpected(base::makeError(
                {}, std::format("command {} of '{}' draws no triangle", draw.command, draw.list)));
        }
        return *run;
    }

    [[nodiscard]] std::expected<void, base::Diagnostic> sortFaces() {
        for (usize draft = 0; draft < drafts.size(); ++draft) {
            const RoomMesh* room = findRoomMesh(mesh, drafts[draft].room);
            if (room == nullptr) {
                return std::unexpected(base::makeError(
                    {}, std::format("the mesh has no room {}", drafts[draft].room)));
            }
            for (usize face = 0; face < room->faces.size(); ++face) {
                if (room->faces[face].draw.list.empty()) {
                    continue;
                }
                std::expected<ListState*, base::Diagnostic> list =
                    findList(room->faces[face].draw);
                if (!list) {
                    return std::unexpected(list.error());
                }
                std::expected<usize, base::Diagnostic> run =
                    findRunOf(**list, room->faces[face].draw);
                if (!run) {
                    return std::unexpected(run.error());
                }
                (*list)->original[*run].push_back({.draft = draft, .face = face});
            }
            for (usize face = 0; face < drafts[draft].faces.size(); ++face) {
                std::expected<ListState*, base::Diagnostic> list =
                    findList(drafts[draft].faces[face].draw);
                if (!list) {
                    return std::unexpected(list.error());
                }
                std::expected<usize, base::Diagnostic> run =
                    findRunOf(**list, drafts[draft].faces[face].draw);
                if (!run) {
                    return std::unexpected(run.error());
                }
                (*list)->drafted[*run].push_back({.draft = draft, .face = face});
            }
        }
        return {};
    }

    [[nodiscard]] bool isFaceChanged(const usize draft, const DraftFace& face) const {
        if (!face.original) {
            return true;
        }
        const RoomMesh& room = getRoom(draft);
        const MeshFace& original = room.faces[*face.original];
        for (usize corner = 0; corner < 3; ++corner) {
            if (face.corners[corner].vertex != original.vertices[corner] ||
                makeCornerVtx(drafts[draft], face, corner) !=
                    room.entries[original.corners[corner]].vtx) {
                return true;
            }
        }
        return false;
    }

    void findChanges() {
        changedFaces.resize(drafts.size());
        keptOriginals.resize(drafts.size());
        for (usize draft = 0; draft < drafts.size(); ++draft) {
            const RoomDraft& drafted = drafts[draft];
            keptOriginals[draft].assign(getRoom(draft).faces.size(), false);
            for (const DraftFace& face : drafted.faces) {
                changedFaces[draft].push_back(isFaceChanged(draft, face));
                if (face.original && *face.original < keptOriginals[draft].size()) {
                    keptOriginals[draft][*face.original] = true;
                }
            }
        }
        for (auto& [name, list] : lists) {
            for (usize run = 0; run < list.runs.size(); ++run) {
                const std::vector<FaceAt>& before = list.original[run];
                const std::vector<FaceAt>& after = list.drafted[run];
                bool isChanged = before.size() != after.size();
                for (usize i = 0; i < after.size() && !isChanged; ++i) {
                    const DraftFace& face = drafts[after[i].draft].faces[after[i].face];
                    isChanged = after[i].draft != before[i].draft ||
                                face.original != before[i].face ||
                                changedFaces[after[i].draft][after[i].face];
                }
                list.isRebuilt[run] = isChanged;
            }
        }
    }

    [[nodiscard]] std::expected<void, base::Diagnostic> markRebuilt() {
        for (auto& [name, list] : lists) {
            for (usize run = 0; run < list.runs.size(); ++run) {
                if (!list.isRebuilt[run]) {
                    continue;
                }
                for (usize next = run + 1;
                     next < list.runs.size() && !list.runs[next].isSelfContained; ++next) {
                    list.isRebuilt[next] = true;
                }
            }
            for (usize run = 0; run < list.runs.size(); ++run) {
                if (!list.isRebuilt[run]) {
                    continue;
                }
                const std::vector<FaceAt>& before = list.original[run];
                const bool isOneRoom = std::ranges::all_of(before, [&before](const FaceAt& at) {
                    return at.draft == before.front().draft;
                });
                if (before.size() != list.runs[run].triangles || !isOneRoom) {
                    return std::unexpected(base::makeError(
                        {}, std::format("'{}' cannot be rebuilt: commands {} to {} draw {} "
                                        "triangles, of which the mesh reads {} where they are "
                                        "drawn once",
                                        name, list.runs[run].first, list.runs[run].end - 1,
                                        list.runs[run].triangles, before.size())));
                }
            }
        }
        return {};
    }

    void findTurned() {
        turned.resize(drafts.size());
        normals.resize(drafts.size());
        for (usize draft = 0; draft < drafts.size(); ++draft) {
            const RoomDraft& drafted = drafts[draft];
            const RoomMesh& room = getRoom(draft);
            std::vector<bool>& isTurned = turned[draft];
            isTurned.assign(drafted.vertices.size(), false);
            for (usize face = 0; face < drafted.faces.size(); ++face) {
                if (!changedFaces[draft][face]) {
                    continue;
                }
                for (const DraftCorner& corner : drafted.faces[face].corners) {
                    isTurned[corner.vertex] = true;
                }
                if (const std::optional<usize> original = drafted.faces[face].original) {
                    for (const usize vertex : room.faces[*original].vertices) {
                        isTurned[vertex] = true;
                    }
                }
            }
            for (usize face = 0; face < room.faces.size(); ++face) {
                if (!keptOriginals[draft][face]) {
                    for (const usize vertex : room.faces[face].vertices) {
                        isTurned[vertex] = true;
                    }
                }
            }
            std::vector<glm::vec3>& sums = normals[draft];
            sums.assign(drafted.vertices.size(), glm::vec3(0.0F));
            for (const DraftFace& face : drafted.faces) {
                const glm::vec3 first = findCornerPosition(drafted, face, 0);
                const glm::vec3 facing =
                    glm::cross(findCornerPosition(drafted, face, 1) - first,
                               findCornerPosition(drafted, face, 2) - first);
                for (const DraftCorner& corner : face.corners) {
                    if (isTurned[corner.vertex]) {
                        sums[corner.vertex] += facing;
                    }
                }
            }
        }
    }

    [[nodiscard]] std::array<u8, 16> makeFinalVtx(const usize draft, const DraftFace& face,
                                                  const usize corner) const {
        std::array<u8, 16> vtx = makeCornerVtx(drafts[draft], face, corner);
        const usize vertex = face.corners[corner].vertex;
        if (face.isLit && turned[draft][vertex] &&
            glm::length(normals[draft][vertex]) >= Flat) {
            const std::array<u8, 3> normal = makeVtxNormal(face.transform, normals[draft][vertex]);
            std::ranges::copy(normal, vtx.begin() + NormalOffset);
        }
        return vtx;
    }

    void writeRun(ListState& list, const usize run) {
        RunOutput& output = list.outputs[run];
        std::vector<std::pair<FaceAt, std::array<usize, 3>>> pending;
        std::map<std::pair<usize, std::array<u8, 16>>, usize> slots;
        std::vector<std::array<u8, 16>> batch;
        const auto flush = [&] {
            if (batch.empty()) {
                return;
            }
            const usize start = output.vertices.size() / VtxSize;
            for (const std::array<u8, 16>& vtx : batch) {
                output.vertices.insert(output.vertices.end(), vtx.begin(), vtx.end());
            }
            output.loads.push_back(output.commands.size());
            addWord(output.commands, (u32{VertexOpcode} << 24) |
                                         (static_cast<u32>(batch.size()) << 12) |
                                         (static_cast<u32>(batch.size()) << 1));
            addWord(output.commands, static_cast<u32>(start * VtxSize));
            for (usize i = 0; i < pending.size(); i += 2) {
                if (i + 1 < pending.size()) {
                    addWord(output.commands, (u32{Triangle2Opcode} << 24) |
                                                 packTriangle(pending[i].second));
                    addWord(output.commands, packTriangle(pending[i + 1].second));
                } else {
                    addWord(output.commands, (u32{Triangle1Opcode} << 24) |
                                                 packTriangle(pending[i].second));
                    addWord(output.commands, 0);
                }
            }
            for (auto& [at, faceSlots] : pending) {
                std::array<usize, 3> entries{};
                for (usize corner = 0; corner < 3; ++corner) {
                    entries[corner] = start + faceSlots[corner];
                }
                output.faceEntries.emplace_back(at, entries);
            }
            pending.clear();
            slots.clear();
            batch.clear();
        };
        for (const FaceAt& at : list.drafted[run]) {
            const DraftFace& face = drafts[at.draft].faces[at.face];
            std::array<std::pair<usize, std::array<u8, 16>>, 3> keys{};
            usize added = 0;
            for (usize corner = 0; corner < 3; ++corner) {
                keys[corner] = {face.corners[corner].vertex, makeFinalVtx(at.draft, face, corner)};
                const bool isRepeat = std::ranges::find(keys.begin(), keys.begin() + corner,
                                                        keys[corner]) != keys.begin() + corner;
                if (!slots.contains(keys[corner]) && !isRepeat) {
                    ++added;
                }
            }
            if (batch.size() + added > VertexBufferSize) {
                flush();
            }
            std::array<usize, 3> faceSlots{};
            for (usize corner = 0; corner < 3; ++corner) {
                const auto [found, isNew] = slots.emplace(keys[corner], batch.size());
                if (isNew) {
                    batch.push_back(keys[corner].second);
                }
                faceSlots[corner] = found->second;
            }
            pending.emplace_back(at, faceSlots);
        }
        flush();
        const usize start = list.runs[run].first * CommandSize;
        const usize end = list.runs[run].end * CommandSize;
        for (const z64::Relocation& relocation : list.symbol.relocations) {
            if (relocation.offset >= start && relocation.offset < end) {
                oldArrays[relocation.symbol].insert(&output);
            }
        }
    }

    [[nodiscard]] bool isInRebuiltRun(const std::string& symbol, const usize offset) const {
        const auto found = lists.find(symbol);
        if (found == lists.end()) {
            return false;
        }
        const ListState& list = found->second;
        for (usize run = 0; run < list.runs.size(); ++run) {
            if (list.isRebuilt[run] && offset >= list.runs[run].first * CommandSize &&
                offset < list.runs[run].end * CommandSize) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool isTaken(const std::string& name) const {
        return model.findSymbol(name) != nullptr ||
               std::ranges::contains(takenNames, name);
    }

    void nameArrays() {
        std::set<std::string> freed;
        for (const auto& [array, users] : oldArrays) {
            bool isPointedAt = false;
            for (const std::vector<z64::SymbolData>& image : model.getImages()) {
                for (const z64::SymbolData& symbol : image) {
                    for (const z64::Relocation& relocation : symbol.relocations) {
                        if (relocation.symbol == array &&
                            !isInRebuiltRun(symbol.name, relocation.offset)) {
                            isPointedAt = true;
                        }
                    }
                }
            }
            if (!isPointedAt) {
                freed.insert(array);
            }
        }
        for (auto& [name, list] : lists) {
            for (usize run = 0; run < list.runs.size(); ++run) {
                RunOutput& output = list.outputs[run];
                if (!list.isRebuilt[run] || output.vertices.empty()) {
                    continue;
                }
                for (const auto& [array, users] : oldArrays) {
                    if (users.contains(&output) && freed.contains(array) &&
                        !std::ranges::contains(reused, array)) {
                        output.array = array;
                        reused.push_back(array);
                        break;
                    }
                }
                if (output.array.empty()) {
                    for (usize number = 0; output.array.empty(); ++number) {
                        const std::string candidate = std::format("{}_vtx_{}", name, number);
                        if (!isTaken(candidate)) {
                            output.array = candidate;
                        }
                    }
                }
                takenNames.push_back(output.array);
            }
        }
        for (const std::string& array : freed) {
            if (!std::ranges::contains(reused, array)) {
                rebuild.removed.push_back(array);
            }
        }
    }

    void assembleList(ListState& list) {
        z64::SymbolData& symbol = list.symbol;
        const usize count = symbol.bytes.size() / CommandSize;
        std::vector<u8> bytes;
        std::vector<z64::Relocation> relocations;
        list.moved.assign(count + 1, std::nullopt);
        for (usize command = 0; command < count;) {
            const std::optional<usize> run = findRun(list.runs, command);
            list.moved[command] = bytes.size();
            if (run && list.isRebuilt[*run]) {
                const RunOutput& output = list.outputs[*run];
                for (const usize load : output.loads) {
                    relocations.push_back(z64::Relocation{.offset = bytes.size() + load + 4,
                                                          .symbol = output.array});
                }
                bytes.insert(bytes.end(), output.commands.begin(), output.commands.end());
                command = list.runs[*run].end;
                continue;
            }
            const usize from = command * CommandSize;
            for (const z64::Relocation& relocation : symbol.relocations) {
                if (relocation.offset >= from && relocation.offset < from + CommandSize) {
                    z64::Relocation moved = relocation;
                    moved.offset = bytes.size() + (relocation.offset - from);
                    relocations.push_back(std::move(moved));
                }
            }
            bytes.insert(bytes.end(), symbol.bytes.begin() + static_cast<std::ptrdiff_t>(from),
                         symbol.bytes.begin() + static_cast<std::ptrdiff_t>(from + CommandSize));
            ++command;
        }
        list.moved[count] = bytes.size();
        symbol.bytes = std::move(bytes);
        symbol.relocations = std::move(relocations);
        changedSymbols[symbol.name] = symbol;
    }

    [[nodiscard]] std::expected<void, base::Diagnostic> patchPointers() {
        std::vector<z64::SymbolData> pointing;
        for (const std::vector<z64::SymbolData>& image : model.getImages()) {
            for (const z64::SymbolData& symbol : image) {
                const auto changed = changedSymbols.find(symbol.name);
                pointing.push_back(changed != changedSymbols.end() ? changed->second : symbol);
            }
        }
        for (z64::SymbolData& symbol : pointing) {
            bool isPatched = false;
            for (const z64::Relocation& relocation : symbol.relocations) {
                const auto list = lists.find(relocation.symbol);
                if (list == lists.end() || list->second.moved.empty() ||
                    relocation.offset + 4 > symbol.bytes.size()) {
                    continue;
                }
                const u32 addend = n64::readU32(symbol.bytes, relocation.offset);
                const std::vector<std::optional<usize>>& moved = list->second.moved;
                const usize command = addend / CommandSize;
                if (addend % CommandSize != 0 || command >= moved.size() || !moved[command]) {
                    return std::unexpected(base::makeError(
                        {}, std::format("'{}' points into the middle of '{}', past what is rebuilt",
                                        symbol.name, relocation.symbol)));
                }
                if (*moved[command] != addend) {
                    n64::writeU32(symbol.bytes, relocation.offset, static_cast<u32>(*moved[command]));
                    isPatched = true;
                }
            }
            if (isPatched) {
                changedSymbols[symbol.name] = symbol;
            }
        }
        return {};
    }

    void writeArrays() {
        for (auto& [name, list] : lists) {
            const std::optional<usize> image = model.findImage(name);
            for (usize run = 0; run < list.runs.size(); ++run) {
                const RunOutput& output = list.outputs[run];
                if (!list.isRebuilt[run] || output.vertices.empty()) {
                    continue;
                }
                if (const z64::SymbolData* old = model.findSymbol(output.array)) {
                    z64::SymbolData array = *old;
                    array.bytes = output.vertices;
                    array.relocations.clear();
                    changedSymbols[array.name] = std::move(array);
                    continue;
                }
                z64::SymbolData array{.name = output.array,
                                      .type = "Vtx",
                                      .isArray = true,
                                      .alignment = 8,
                                      .bytes = output.vertices};
                for (const auto& [old, users] : oldArrays) {
                    if (const z64::SymbolData* found = model.findSymbol(old)) {
                        array.alignment = found->alignment;
                        array.innerLengths = found->innerLengths;
                        break;
                    }
                }
                rebuild.added.push_back(AddedSymbol{.image = image.value_or(0), .symbol = array});
            }
        }
    }

    void writeNormals() {
        std::set<std::pair<MeshEntryKey, usize>> written;
        for (usize draft = 0; draft < drafts.size(); ++draft) {
            const RoomMesh& room = getRoom(draft);
            for (const MeshFace& face : room.faces) {
                if (!face.isLit || isRunRebuilt(face.draw)) {
                    continue;
                }
                for (usize corner = 0; corner < 3; ++corner) {
                    const usize vertex = face.vertices[corner];
                    const MeshEntry& entry = room.entries[face.corners[corner]];
                    if (vertex >= turned[draft].size() || !turned[draft][vertex] ||
                        glm::length(normals[draft][vertex]) < Flat ||
                        !written.emplace(entry.key, draft).second) {
                        continue;
                    }
                    const std::array<u8, 3> normal =
                        makeVtxNormal(entry.transform, normals[draft][vertex]);
                    rebuild.writes.push_back(
                        SymbolWrite{.symbol = entry.key.symbol,
                                    .offset = (entry.key.index * VtxSize) + NormalOffset,
                                    .bytes = {normal.begin(), normal.end()}});
                }
            }
        }
    }

    [[nodiscard]] bool isRunRebuilt(const MeshDraw& draw) const {
        const auto found = lists.find(draw.list);
        if (found == lists.end()) {
            return false;
        }
        const std::optional<usize> run = findRun(found->second.runs, draw.command);
        return run && found->second.isRebuilt[*run];
    }

    void writeBounds() {
        for (usize draft = 0; draft < drafts.size(); ++draft) {
            const RoomDraft& drafted = drafts[draft];
            const RoomMesh& room = getRoom(draft);
            std::set<usize> parts;
            for (usize face = 0; face < drafted.faces.size(); ++face) {
                if (changedFaces[draft][face]) {
                    parts.insert(drafted.faces[face].part);
                }
            }
            for (usize face = 0; face < room.faces.size(); ++face) {
                if (!keptOriginals[draft][face]) {
                    parts.insert(room.faces[face].part);
                }
            }
            for (const usize part : parts) {
                std::vector<glm::vec3> points;
                for (const DraftFace& face : drafted.faces) {
                    if (face.part == part) {
                        for (usize corner = 0; corner < 3; ++corner) {
                            points.push_back(findCornerPosition(drafted, face, corner));
                        }
                    }
                }
                if (std::optional<SymbolWrite> bounds =
                        makePartBoundsWrite(scene, mesh.layer, drafted.room, part, points)) {
                    rebuild.writes.push_back(std::move(*bounds));
                }
            }
        }
    }

    void makeKept() {
        rebuild.kept = SceneMesh{.layer = mesh.layer};
        for (const RoomMesh& room : mesh.rooms) {
            const auto draft = std::ranges::find(drafts, room.room, &RoomDraft::room);
            if (draft == drafts.end()) {
                rebuild.kept.rooms.push_back(room);
            }
        }
        rebuild.faces.resize(drafts.size());
        std::vector<std::map<MeshEntryKey, usize>> groups(drafts.size());
        for (usize draft = 0; draft < drafts.size(); ++draft) {
            const RoomMesh& room = getRoom(draft);
            rebuild.faces[draft].resize(drafts[draft].faces.size());
            for (usize face = 0; face < drafts[draft].faces.size(); ++face) {
                const DraftFace& drafted = drafts[draft].faces[face];
                if (drafted.original && !isRunRebuilt(drafted.draw)) {
                    const MeshFace& original = room.faces[*drafted.original];
                    for (usize corner = 0; corner < 3; ++corner) {
                        const MeshEntryKey& key = room.entries[original.corners[corner]].key;
                        rebuild.faces[draft][face][corner] = key;
                        groups[draft].emplace(key, original.vertices[corner]);
                    }
                }
            }
            for (usize face = 0; face < room.faces.size(); ++face) {
                if (room.faces[face].draw.list.empty() || !isRunRebuilt(room.faces[face].draw)) {
                    for (usize corner = 0; corner < 3; ++corner) {
                        groups[draft].emplace(room.entries[room.faces[face].corners[corner]].key,
                                              room.faces[face].vertices[corner]);
                    }
                }
            }
        }
        for (const auto& [name, list] : lists) {
            for (usize run = 0; run < list.runs.size(); ++run) {
                if (!list.isRebuilt[run]) {
                    continue;
                }
                const RunOutput& output = list.outputs[run];
                for (const auto& [at, entries] : output.faceEntries) {
                    const DraftFace& face = drafts[at.draft].faces[at.face];
                    for (usize corner = 0; corner < 3; ++corner) {
                        MeshEntryKey key{.symbol = output.array, .index = entries[corner]};
                        groups[at.draft].emplace(key, face.corners[corner].vertex);
                        rebuild.faces[at.draft][at.face][corner] = std::move(key);
                    }
                }
            }
        }
        for (usize draft = 0; draft < drafts.size(); ++draft) {
            RoomMesh kept{.room = drafts[draft].room};
            kept.vertices.resize(drafts[draft].vertices.size());
            for (const auto& [key, vertex] : groups[draft]) {
                kept.entries.push_back(MeshEntry{.key = key});
                if (vertex < kept.vertices.size()) {
                    kept.vertices[vertex].entries.push_back(kept.entries.size() - 1);
                }
            }
            rebuild.kept.rooms.push_back(std::move(kept));
        }
        std::ranges::sort(rebuild.kept.rooms, {}, &RoomMesh::room);
    }

    const Scene& scene;
    const SceneModel& model;
    const SceneMesh& mesh;
    std::span<const RoomDraft> drafts;
    std::map<std::string, ListState> lists;
    std::vector<std::vector<bool>> changedFaces;
    std::vector<std::vector<bool>> keptOriginals;
    std::vector<std::vector<bool>> turned;
    std::vector<std::vector<glm::vec3>> normals;
    std::map<std::string, std::set<const RunOutput*>> oldArrays;
    std::vector<std::string> reused;
    std::vector<std::string> takenNames;
    std::map<std::string, z64::SymbolData> changedSymbols;
    MeshRebuild rebuild;
};

[[nodiscard]] std::vector<FaceSignature> listSignatures(const RoomMesh& room) {
    std::vector<FaceSignature> signatures;
    for (const MeshFace& face : room.faces) {
        FaceSignature signature{};
        for (usize corner = 0; corner < 3; ++corner) {
            const MeshEntry& entry = room.entries[face.corners[corner]];
            signature[corner] = makePlaceKey(findVtxWorld(entry.transform, entry.vtx));
        }
        signatures.push_back(signature);
    }
    std::ranges::sort(signatures);
    return signatures;
}

[[nodiscard]] std::vector<FaceSignature> listSignatures(const RoomDraft& draft) {
    std::vector<FaceSignature> signatures;
    for (const DraftFace& face : draft.faces) {
        FaceSignature signature{};
        for (usize corner = 0; corner < 3; ++corner) {
            signature[corner] =
                makePlaceKey(findVtxWorld(face.transform, makeCornerVtx(draft, face, corner)));
        }
        signatures.push_back(signature);
    }
    std::ranges::sort(signatures);
    return signatures;
}

/// Makes `rebuild` in a copy of `model` and reads the drafts' rooms again
/// out of what it lays out.
[[nodiscard]] std::expected<void, base::Diagnostic>
checkRebuild(const Scene& scene, const SceneModel& model, const SceneMesh& mesh,
             const std::span<const RoomDraft> drafts, const MeshRebuild& rebuild) {
    SceneModel checked = model;
    if (std::expected<void, base::Diagnostic> applied = applyMeshRebuild(checked, rebuild);
        !applied) {
        return applied;
    }
    Scene written;
    written.images = checked.layOut();
    written.layers = scene.layers;
    if (mesh.layer < written.layers.size()) {
        written.layers[mesh.layer].rooms = findMovedRooms(scene, mesh.layer, written.images);
    }
    const SceneMesh read = readSceneMesh(written, mesh.layer, &rebuild.kept);
    for (const RoomDraft& draft : drafts) {
        const RoomMesh* room = findRoomMesh(read, draft.room);
        const std::vector<FaceSignature> wanted = listSignatures(draft);
        const std::vector<FaceSignature> drawn =
            room != nullptr ? listSignatures(*room) : std::vector<FaceSignature>{};
        if (wanted != drawn) {
            std::string listed;
            for (const std::string& list : rebuild.lists) {
                listed += listed.empty() ? list : ", " + list;
            }
            return std::unexpected(base::makeError(
                {}, std::format("rebuilding {} does not draw what room {} was edited to: {} "
                                "faces wanted, {} drawn",
                                listed, draft.room, wanted.size(), drawn.size())));
        }
    }
    return {};
}

/// The symbol of `image` that holds `address`, and how far into it that is.
[[nodiscard]] std::optional<std::pair<std::string, u32>> findHolder(const z64::MemoryImage& image,
                                                                    const u32 address) {
    for (const auto& [name, symbol] : image.symbols) {
        const u32 start = (u32{symbol.segment} << 24) | static_cast<u32>(symbol.offset);
        if (address >= start && address < start + static_cast<u32>(std::max<usize>(symbol.size, 1))) {
            return std::pair{name, address - start};
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<RoomHeader>
findMovedRooms(const Scene& scene, const usize layer,
               const std::vector<std::shared_ptr<const z64::MemoryImage>>& images) {
    std::vector<RoomHeader> rooms = scene.layers[layer].rooms;
    const auto move = [&](const usize room, u32& address) {
        if (address == 0) {
            return;
        }
        for (const usize index : {FirstRoomImage + room, SceneImage}) {
            if (index >= scene.images.size() || index >= images.size()) {
                continue;
            }
            const std::optional<std::pair<std::string, u32>> holder =
                findHolder(*scene.images[index], address);
            if (!holder) {
                continue;
            }
            if (const std::optional<u32> moved = images[index]->findAddress(holder->first)) {
                address = *moved + holder->second;
            }
            return;
        }
    };
    for (usize room = 0; room < rooms.size(); ++room) {
        for (RoomDisplayLists& lists : rooms[room].displayLists) {
            move(room, lists.opaque);
            move(room, lists.translucent);
        }
    }
    return rooms;
}

std::expected<MeshRebuild, base::Diagnostic> planMeshRebuild(const Scene& scene,
                                                             const SceneModel& model,
                                                             const SceneMesh& mesh,
                                                             const std::span<const RoomDraft> drafts) {
    RebuildPlanner planner(scene, model, mesh, drafts);
    std::expected<MeshRebuild, base::Diagnostic> rebuild = planner.plan();
    if (!rebuild || rebuild->isEmpty()) {
        return rebuild;
    }
    if (std::expected<void, base::Diagnostic> checked =
            checkRebuild(scene, model, mesh, drafts, *rebuild);
        !checked) {
        return std::unexpected(checked.error());
    }
    return rebuild;
}

std::expected<void, base::Diagnostic> applyMeshRebuild(SceneModel& model,
                                                       const MeshRebuild& rebuild) {
    for (const z64::SymbolData& symbol : rebuild.changed) {
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(symbol); !set) {
            return set;
        }
    }
    for (const AddedSymbol& added : rebuild.added) {
        if (std::expected<void, base::Diagnostic> set = model.addSymbol(added.image, added.symbol);
            !set) {
            return set;
        }
    }
    if (std::expected<void, base::Diagnostic> written = applySymbolWrites(model, rebuild.writes);
        !written) {
        return written;
    }
    for (const std::string& name : rebuild.removed) {
        if (std::expected<void, base::Diagnostic> removed = model.removeSymbol(name); !removed) {
            return removed;
        }
    }
    return {};
}

} // namespace toki::scene

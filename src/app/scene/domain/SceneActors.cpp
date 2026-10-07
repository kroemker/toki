#include "SceneActors.h"

#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/ActorModelReader.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Pose.h"
#include "app/object/domain/Skeleton.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
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

constexpr u8 SceneKeepSegment = 0x05;

[[nodiscard]] std::set<s16> collectActorIds(const std::vector<SceneLayer>& layers) {
    std::set<s16> ids;
    for (const SceneLayer& layer : layers) {
        for (const RoomHeader& room : layer.rooms) {
            for (const ActorPlacement& actor : room.actors) {
                ids.insert(actor.id);
            }
        }
        for (const TransitionActorPlacement& actor : layer.header.transitionActors) {
            ids.insert(actor.id);
        }
    }
    return ids;
}

class ActorReader {
public:
    ActorReader(hackeroot::AssetImageReader& reader, const std::vector<SceneLayer>& layers)
        : reader(reader), workspace(reader.getWorkspace()), layers(layers) {
        if (!layers.empty()) {
            sceneKeep = findObjectName(layers.front().header.keepObjectId);
        }
    }

    [[nodiscard]] SceneActors read() &&;

private:
    [[nodiscard]] std::string findObjectName(s16 id) const;

    [[nodiscard]] std::optional<usize> findObject(const std::string& name,
                                                  std::vector<base::Diagnostic>& diagnostics);

    [[nodiscard]] std::optional<u32> findAddress(usize object, const actor::ObjectSymbol& symbol,
                                                 std::vector<base::Diagnostic>& diagnostics) const;

    void prepareSkeleton(SceneActorModel& actor);

    void prepareDisplayLists(SceneActorModel& actor);

    [[nodiscard]] std::vector<ActorSegment>
    prepareSegments(std::span<const actor::SegmentSetting> settings,
                    std::vector<base::Diagnostic>& diagnostics);

    hackeroot::AssetImageReader& reader;
    const hackeroot::Workspace& workspace;
    const std::vector<SceneLayer>& layers;
    std::string sceneKeep;
    std::map<std::string, std::optional<usize>> objectIndices;
    SceneActors actors;
};

std::string ActorReader::findObjectName(const s16 id) const {
    if (id <= 0) {
        return {};
    }
    const std::vector<hackeroot::ObjectEntry>& objects = workspace.getTables().objects;
    const auto found =
        std::ranges::find(objects, static_cast<usize>(id), &hackeroot::ObjectEntry::id);
    return found == objects.end() ? std::string() : found->name;
}

std::optional<usize> ActorReader::findObject(const std::string& name,
                                             std::vector<base::Diagnostic>& diagnostics) {
    if (const auto known = objectIndices.find(name); known != objectIndices.end()) {
        return known->second;
    }
    std::optional<usize>& index = objectIndices[name];
    const std::optional<hackeroot::AssetDirectory> directory =
        hackeroot::findObjectDirectory(workspace.getAssets(), name);
    if (!directory) {
        diagnostics.push_back(base::makeWarning(
            {}, std::format("the checkout has no object '{}' to draw the actor from", name)));
        return index;
    }
    const bool isKeep = name == hackeroot::KeepObjectName;
    const u8 segment = isKeep              ? hackeroot::KeepSegment
                       : name == sceneKeep ? SceneKeepSegment
                                           : hackeroot::ObjectSegment;
    std::vector<hackeroot::SharedImageFiles> shared{
        hackeroot::makeSharedGameCode(workspace.getRoot())};
    if (!isKeep) {
        if (const std::optional<hackeroot::AssetDirectory> keep =
                hackeroot::findObjectDirectory(workspace.getAssets(), hackeroot::KeepObjectName)) {
            shared.push_back(hackeroot::makeSharedObject(*keep, hackeroot::KeepSegment));
        }
    }
    shared.push_back(hackeroot::makeSharedObject(*directory, segment));
    std::ranges::copy(shared.back().watched, std::back_inserter(actors.watched));
    const hackeroot::ImageSet images = reader.read(shared, {});
    index = actors.objects.size();
    actors.objects.push_back(
        ActorObject{.name = name, .segment = segment, .image = images.shared.back()});
    return index;
}

std::optional<u32> ActorReader::findAddress(const usize object, const actor::ObjectSymbol& symbol,
                                            std::vector<base::Diagnostic>& diagnostics) const {
    const std::optional<u32> address = actors.objects[object].image->findAddress(symbol.name);
    if (!address) {
        diagnostics.push_back(base::makeWarning(
            symbol.location, std::format("'{}' is not in the image of '{}'", symbol.name,
                                         actors.objects[object].name)));
    }
    return address;
}

void ActorReader::prepareSkeleton(SceneActorModel& actor) {
    const actor::SkeletonModel& model = *actor.model.skeleton;
    const std::optional<usize> object = findObject(model.skeleton.object, actor.diagnostics);
    const std::optional<u32> address =
        object ? findAddress(*object, model.skeleton, actor.diagnostics) : std::nullopt;
    if (!address) {
        return;
    }
    const z64::MemoryImage& image = *actors.objects[*object].image;
    const object::Skeleton skeleton = object::readSkeleton(image, *address);
    std::ranges::copy(skeleton.diagnostics, std::back_inserter(actor.diagnostics));
    if (skeleton.limbs.empty()) {
        return;
    }
    object::JointTable joints = object::makeRestJointTable(skeleton);
    std::optional<ActorAnimation> animated;
    if (model.animation) {
        const std::optional<usize> animationObject =
            findObject(model.animation->object, actor.diagnostics);
        const std::optional<u32> animationAddress =
            animationObject ? findAddress(*animationObject, *model.animation, actor.diagnostics)
                            : std::nullopt;
        if (animationAddress) {
            const std::expected<object::Animation, base::Diagnostic> animation =
                object::readAnimation(*actors.objects[*animationObject].image, *animationAddress);
            if (!animation) {
                actor.diagnostics.push_back(animation.error());
            } else if (animation->jointIndices.size() != skeleton.limbs.size() + 1) {
                actor.diagnostics.push_back(base::makeWarning(
                    model.animation->location,
                    std::format("'{}' moves {} joints, '{}' has {} limbs, so it is drawn at rest",
                                model.animation->name, animation->jointIndices.size(),
                                model.skeleton.name, skeleton.limbs.size())));
            } else {
                joints = object::makeJointTable(*animation, 0);
                animated = ActorAnimation{.skeleton = skeleton, .animation = *animation};
            }
        }
    }
    actor.drawing = ActorDrawing{.skeletonObject = *object,
                                 .limbs = object::poseSkeleton(skeleton, joints).drawn,
                                 .animation = std::move(animated)};
}

void ActorReader::prepareDisplayLists(SceneActorModel& actor) {
    ActorDrawing drawing;
    for (const actor::DisplayListDraw& draw : actor.model.displayLists) {
        const std::optional<usize> object = findObject(draw.displayList.object, actor.diagnostics);
        const std::optional<u32> address =
            object ? findAddress(*object, draw.displayList, actor.diagnostics) : std::nullopt;
        if (address) {
            drawing.displayLists.push_back(
                ActorDisplayList{.object = *object,
                                 .address = *address,
                                 .pass = draw.pass,
                                 .segments = prepareSegments(draw.segments, actor.diagnostics)});
        }
    }
    if (!drawing.displayLists.empty()) {
        actor.drawing = std::move(drawing);
    }
}

std::vector<ActorSegment>
ActorReader::prepareSegments(const std::span<const actor::SegmentSetting> settings,
                             std::vector<base::Diagnostic>& diagnostics) {
    std::vector<ActorSegment> segments;
    for (const actor::SegmentSetting& setting : settings) {
        const std::optional<usize> object = findObject(setting.symbol.object, diagnostics);
        const std::optional<u32> address =
            object ? findAddress(*object, setting.symbol, diagnostics) : std::nullopt;
        if (address) {
            segments.push_back(ActorSegment{.segment = setting.segment,
                                            .pass = setting.pass,
                                            .object = *object,
                                            .address = *address});
        }
    }
    return segments;
}

SceneActors ActorReader::read() && {
    const std::vector<hackeroot::ActorEntry>& table = workspace.getTables().actors;
    for (const s16 id : collectActorIds(layers)) {
        const auto entry =
            std::ranges::find(table, static_cast<usize>(id), &hackeroot::ActorEntry::id);
        if (entry == table.end() || entry->isUnset) {
            continue;
        }
        SceneActorModel& actor = actors.models[id];
        actor.model = actor::readActorModel(workspace, *entry);
        if (const auto overlay = hackeroot::findOverlayFiles(workspace.getRoot(), entry->name)) {
            std::ranges::copy(overlay->getAllFiles(), std::back_inserter(actors.watched));
        }
        if (actor.model.skeleton) {
            prepareSkeleton(actor);
        } else if (!actor.model.displayLists.empty()) {
            prepareDisplayLists(actor);
        }
        if (actor.drawing && actor.model.skeleton) {
            actor.drawing->segments = prepareSegments(actor.model.segments, actor.diagnostics);
        }
    }
    return std::move(actors);
}

} // namespace

bool hasAnimatedActors(const SceneActors& actors) {
    return std::ranges::any_of(actors.models, [](const auto& entry) {
        return entry.second.drawing && entry.second.drawing->animation;
    });
}

SceneActors readSceneActors(hackeroot::AssetImageReader& reader,
                            const std::vector<SceneLayer>& layers) {
    const auto started = std::chrono::steady_clock::now();
    SceneActors actors = ActorReader(reader, layers).read();
    actors.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return actors;
}

} // namespace toki::scene

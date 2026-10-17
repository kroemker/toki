#include "FrameStatistics.h"

#include "app/viewport/Types.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "z64/MemoryImage.h"

#include <array>
#include <format>
#include <optional>
#include <string_view>

namespace toki::viewport {

namespace {

constexpr auto ImageKindNames =
    std::to_array<std::string_view>({"Code", "Scene", "Rooms", "Objects", "Actors", "Generated"});

static_assert(ImageKindNames.size() == ImageKindCount,
              "an image kind was added to the enum without a name");

[[nodiscard]] ImageUse makeImageUse(const Frame& frame, const ViewportContent& content,
                                    const usize index) {
    ImageUse use = index < content.labels.size() ? ImageUse{.name = content.labels[index].name,
                                                            .kind = content.labels[index].kind}
                                                 : ImageUse{.name = std::format("Image {}", index)};
    const std::optional<z64::MappedImage>& mapped =
        index < frame.mappedImages.size() ? frame.mappedImages[index] : std::nullopt;
    if (mapped) {
        use.size = mapped->end - mapped->start;
    } else {
        use.size = static_cast<usize>(
            z64::findMappedEnd(*content.images[index], FirstImageAddress) - FirstImageAddress);
        use.isMapped = false;
    }
    return use;
}

} // namespace

FrameStatistics makeFrameStatistics(const Frame& frame, const ViewportContent& content) {
    FrameStatistics statistics{.rsp = frame.rspResult.statistics,
                               .textureLoads = frame.rdpResult.textureLoads,
                               .timings = frame.timings};
    if (frame.frameListSize == 0) {
        return statistics;
    }
    statistics.codeRegion = FirstImageAddress;
    statistics.frameListSize = frame.frameListSize;
    statistics.used = frame.frameListAddress + frame.frameListSize;
    for (usize i = 0; i < content.images.size(); ++i) {
        if (content.images[i] != nullptr) {
            statistics.images.push_back(makeImageUse(frame, content, i));
        }
    }
    return statistics;
}

std::string_view getImageKindName(const ImageKind kind) {
    return ImageKindNames[static_cast<usize>(kind)];
}

} // namespace toki::viewport

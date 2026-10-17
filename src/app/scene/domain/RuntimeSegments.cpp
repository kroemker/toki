#include "RuntimeSegments.h"

#include "app/scene/Types.h"
#include "n64/DisplayListWriter.h"
#include "n64/FixedMatrix.h"
#include "z64/MemoryImage.h"

namespace toki::scene {

RuntimeSegments makeRuntimeSegments() {
    n64::DisplayListWriter writer(StandInSegment);
    const u32 standIn = writer.addMatrix(n64::makeIdentityMatrix());
    writer.endDisplayList();
    RuntimeSegments segments{.standIn = standIn};
    segments.image.segments.push_back(
        z64::ImageSegment{.number = StandInSegment, .bytes = writer.getBytes()});
    return segments;
}

} // namespace toki::scene

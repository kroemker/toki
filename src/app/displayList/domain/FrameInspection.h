#pragma once

#include "app/displayList/Types.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/CommandDecoder.h"
#include "n64/Rdp.h"
#include "n64/Segment.h"
#include "z64/GbiNames.h"

#include <glm/vec2.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::displayList {

/// A triangle's corners on the screen, in pixels.
using ScreenTriangle = std::array<glm::vec2, 3>;

/// One command a frame ran, as the viewer shows it: the trace entry it is, its
/// decoded command, how many calls deep it ran, and, for a call, the row one
/// past the last command of the list it called.
struct InspectedCommand {
    usize entry = 0;
    n64::DecodedCommand command;
    usize depth = 0;
    usize end = 0;
    usize segments = 0;
};

/// A run of commands that loads one texture or palette into texture memory,
/// as `gsDPLoadTextureBlock` writes seven: its rows, from `first` to before
/// `end`, and what it loads.
struct LoadGroup {
    usize first = 0;
    usize end = 0;
    std::string label;
};

/// A frame's trace read for a person: every command it ran, in order, with
/// the addresses it names called by the symbols of the content's images,
/// `gDekuTreeRoom0DL_001234 + 0x40`, as the segment table stood when it ran,
/// what each did, and the RDP state each drawing command drew with. The frame
/// and its content must outlive it.
class FrameInspection {
public:
    FrameInspection(const viewport::Frame& frame, const viewport::ViewportContent& content);

    [[nodiscard]] const std::vector<InspectedCommand>& getRows() const {
        return rows;
    }

    [[nodiscard]] const std::vector<LoadGroup>& getLoadGroups() const {
        return loadGroups;
    }

    /// What a segmented address of the command at `row` is called, as the
    /// segment table stood when it ran.
    [[nodiscard]] std::optional<std::string> findAddressName(usize row, u32 segmented) const;

    /// What a physical address is called.
    [[nodiscard]] std::optional<std::string> findPhysicalName(u32 physical) const;

    /// What the command at `row` did in this frame, in a sentence, or nothing
    /// for a command that only sets state.
    [[nodiscard]] std::string describeEffect(usize row, const z64::GbiNames& names) const;

    /// The triangles and rectangles the command at `row` drew, by their index
    /// into the frame's RDP draws.
    [[nodiscard]] std::vector<usize> listDraws(usize row) const;

    /// Where on the screen the command at `row` drew, and for a call every
    /// command it ran: each triangle, and each rectangle as two, in pixels of
    /// the frame from its top left. A triangle with a corner behind the eye is
    /// left out.
    [[nodiscard]] std::vector<ScreenTriangle> listScreenTriangles(usize row) const;

    /// The RDP state the command at `row` drew its first primitive with.
    [[nodiscard]] const n64::RdpState* findDrawState(usize row) const;

    /// The row of the first command that runs the display list at physical
    /// address `physical`, as a call's first command.
    [[nodiscard]] std::optional<usize> findFirstRun(u32 physical) const;

    /// The row of the first command that runs the display list named `symbol`
    /// in any of the frame's images.
    [[nodiscard]] std::optional<usize> findSymbolRun(std::string_view symbol) const;

private:
    struct NamedRange {
        u32 begin = 0;
        u32 end = 0;
        std::string name;
    };

    void indexSymbols(const viewport::ViewportContent& content);
    void readRows();
    void findLoadGroups();
    [[nodiscard]] std::string makeLoadLabel(usize first, usize end) const;
    void appendScreenTriangles(const n64::RdpPrimitive& primitive,
                               std::vector<ScreenTriangle>& triangles) const;

    const viewport::Frame& frame;
    std::vector<NamedRange> symbols;
    std::vector<std::array<u32, n64::SegmentCount>> segmentTables;
    std::vector<InspectedCommand> rows;
    std::vector<LoadGroup> loadGroups;
    std::vector<std::optional<usize>> outputDraws;
};

} // namespace toki::displayList

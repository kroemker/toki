#pragma once

#include "cparser/MacroTable.h"
#include "n64/CommandDecoder.h"
#include "z64/Types.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace z64 {

/// What a segmented address is called, as `gDekuTreeRoom0DL_001234 + 0x40`, or
/// nothing for an address no symbol holds.
using FindAddressName = std::function<std::optional<std::string>(u32 segmented)>;

/// The names `gbi.h` gives the values a display list's fields hold, as the
/// checkout's own header defines them, so a decoded command reads as it is
/// written: geometry mode bits as `G_ZBUFFER | G_SHADE`, an other mode as
/// `G_CYC_2CYCLE` or a render mode as `G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2`,
/// a combine mode by its `G_CC_` presets, image formats, tile wrapping, matrix
/// flags and the indices of `G_MOVEWORD` and `G_MOVEMEM`. A value no name has
/// reads as its number.
class GbiNames {
public:
    /// Reads the names out of `gbi`, the checkout's `gbi.h` as `hackeroot::readGbi`
    /// reads it.
    explicit GbiNames(const cparser::MacroTable& gbi);

    /// `field`'s value as `gbi.h` would write it, addresses named by
    /// `findAddressName`.
    [[nodiscard]] std::string makeValueText(const n64::CommandField& field,
                                            const FindAddressName& findAddressName) const;

    /// `command` as one call of its macro, `gsSPVertex(gFooVtx + 0x40, 3, 0)`.
    [[nodiscard]] std::string makeCommandText(const n64::DecodedCommand& command,
                                              const FindAddressName& findAddressName) const;

    /// The `G_CC_` preset one cycle of a combine mode is, if one is: `inputs` are
    /// the names of its colour inputs, then of its alpha inputs.
    [[nodiscard]] std::optional<std::string_view>
    findCombinePreset(const std::vector<std::string_view>& inputs) const;

private:
    struct Named {
        std::string name;
        u32 value = 0;
    };

    void readFamily(std::string_view prefix);
    [[nodiscard]] std::optional<std::string_view> findName(std::string_view family,
                                                           u32 value) const;
    [[nodiscard]] std::string makeBitsText(const std::vector<std::string_view>& names,
                                           u32 value) const;
    [[nodiscard]] std::string makeOtherModeText(const n64::CommandField& field) const;
    [[nodiscard]] std::string makeRenderModeText(u32 value) const;
    [[nodiscard]] std::string makeCombineText(const n64::DecodedCommand& command) const;

    std::map<std::string, std::vector<Named>, std::less<>> families;
    std::map<std::string, u32, std::less<>> values;
    std::vector<std::pair<std::string, std::vector<std::string>>> combinePresets;
};

} // namespace z64

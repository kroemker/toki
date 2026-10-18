#pragma once

#include "z64/DataEncoder.h"
#include "z64/DeclarationWriter.h"

namespace z64 {

/// The check a `DeclarationWriter` runs on what its element writers write:
/// the text is encoded as `DataEncoder` encodes an initializer, against
/// `context`, and must come out as the element's bytes and the same symbols
/// at the same offsets.
[[nodiscard]] ElementCheck makeEncodingCheck(DataContext context);

} // namespace z64

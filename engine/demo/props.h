#pragma once

#include "core/bit_reader.h"
#include "core/error.h"
#include "demo/send_tables.h"
#include "demo/types.h"

#include <vector>

namespace gmdr::demo {

// Decodes one property value. Stream-level truncation shows up as r.overflowed(); a structurally invalid
// value (unknown NW2 type, oversized array) returns an error because the stream cannot be resynchronised.
Result<void> decodeProp(BitReader& r, const FlatProp& fp, PropValue& out);

// Reads a property list ("has more" bit + ubitVar index delta + value) into `state` (sized to the class's
// flat props) and appends the changed indices to `changed`.
Result<void> readPropList(BitReader& r, const std::vector<FlatProp>& flat, std::vector<PropValue>& state,
                          std::vector<int>& changed);

// Coordinate encodings (Source SDK 2013 bf_read), exposed for tests.
float readBitCoord(BitReader& r);
float readBitCoordMp(BitReader& r, bool integral, bool lowPrecision);
float readBitNormal(BitReader& r);

} // namespace gmdr::demo

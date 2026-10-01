#pragma once

#include "core/bit_reader.h"
#include "core/error.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gmdr::demo {

struct StringTableEntry {
    std::string string;
    std::vector<std::uint8_t> userData;
    bool present = false;
};

class StringTable {
public:
    StringTable(std::string name, int maxEntriesBits, bool fixedUserData, int userDataSize,
                int userDataSizeBits);

    // Parses `count` entries in the SDK 2013 layout (with GMod's userdata length width) and records the
    // indices that changed.
    Result<void> parseEntries(BitReader& r, int count, int userDataLengthBits, std::vector<int>& changed);

    // dem_stringtables: the table's whole content as the Source client replaces it (DeleteAllStrings, then
    // AddString for each entry). Indices whose string or userdata differ from before are reported.
    Result<void> applySnapshot(std::vector<StringTableEntry> entries, std::vector<int>& changed);

    const std::string& name() const { return name_; }
    int maxEntriesBits() const { return maxEntriesBits_; }
    std::size_t maxEntries() const { return std::size_t{1} << maxEntriesBits_; }
    const std::vector<StringTableEntry>& entries() const { return entries_; }
    const StringTableEntry* entry(std::size_t index) const;
    // Index of the entry whose string equals `s`, or -1.
    int find(std::string_view s) const;

private:
    std::string name_;
    int maxEntriesBits_;
    bool fixedUserData_;
    int userDataSize_;
    int userDataSizeBits_;
    std::vector<StringTableEntry> entries_;
    std::unordered_map<std::string, int> byString_;
};

} // namespace gmdr::demo

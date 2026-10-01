#include "demo/string_tables.h"

#include "core/limits.h"

#include <algorithm>
#include <deque>

namespace gmdr::demo {

StringTable::StringTable(std::string name, int maxEntriesBits, bool fixedUserData, int userDataSize,
                         int userDataSizeBits)
    : name_(std::move(name)), maxEntriesBits_(maxEntriesBits), fixedUserData_(fixedUserData),
      userDataSize_(userDataSize), userDataSizeBits_(userDataSizeBits) {}

const StringTableEntry* StringTable::entry(std::size_t index) const {
    if (index >= entries_.size() || !entries_[index].present)
        return nullptr;
    return &entries_[index];
}

Result<void> StringTable::applySnapshot(std::vector<StringTableEntry> snapshot, std::vector<int>& changed) {
    if (snapshot.size() > maxEntries() || snapshot.size() > limits::kMaxStringTableEntries)
        return makeError("demo.stringtable_count", "string table snapshot has too many entries", name_);
    // Entries past the snapshot are gone (DeleteAllStrings); every snapshot entry is present.
    std::vector<StringTableEntry> next(std::max(snapshot.size(), entries_.size()));
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        next[i] = std::move(snapshot[i]);
        next[i].present = true;
    }
    for (std::size_t i = 0; i < next.size(); ++i) {
        const StringTableEntry* prev = i < entries_.size() ? &entries_[i] : nullptr;
        const bool same = prev && prev->present == next[i].present && prev->string == next[i].string &&
                          prev->userData == next[i].userData;
        if (!same)
            changed.push_back(static_cast<int>(i));
    }
    entries_ = std::move(next);
    byString_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].present)
            byString_[entries_[i].string] = static_cast<int>(i);
    return {};
}

int StringTable::find(std::string_view s) const {
    auto it = byString_.find(std::string(s));
    return it == byString_.end() ? -1 : it->second;
}

Result<void> StringTable::parseEntries(BitReader& r, int count, int userDataLengthBits,
                                       std::vector<int>& changed) {
    if (count < 0 || static_cast<std::size_t>(count) > limits::kMaxStringTableEntries)
        return makeError("demo.stringtable_count", "string table entry count out of range", name_);
    int last = -1;
    std::deque<std::string> history;
    for (int i = 0; i < count; ++i) {
        int index = last + 1;
        if (!r.bit())
            index = static_cast<int>(r.ubit(maxEntriesBits_));
        last = index;
        if (r.overflowed())
            return makeError("demo.stringtable_truncated", "string table data ends early", name_);
        if (static_cast<std::size_t>(index) >= maxEntries() ||
            static_cast<std::size_t>(index) >= limits::kMaxStringTableEntries)
            return makeError("demo.stringtable_index", "string table index out of range", name_);
        if (static_cast<std::size_t>(index) >= entries_.size())
            entries_.resize(static_cast<std::size_t>(index) + 1);
        StringTableEntry& e = entries_[static_cast<std::size_t>(index)];

        if (r.bit()) {
            std::string s;
            if (r.bit()) {
                const std::size_t h = r.ubit(5);
                const std::size_t take = r.ubit(5);
                if (h >= history.size())
                    return makeError("demo.stringtable_history",
                                     "string table substring reference is invalid", name_);
                s = history[h].substr(0, take);
                s += r.string(limits::kMaxStringBytes);
            } else {
                s = r.string(limits::kMaxStringBytes);
            }
            if (e.present && e.string != s)
                byString_.erase(e.string);
            e.string = std::move(s);
            e.present = true;
            byString_[e.string] = index;
        }
        if (r.bit()) {
            if (fixedUserData_) {
                const int nbits = userDataSizeBits_;
                e.userData.assign(static_cast<std::size_t>((nbits + 7) / 8), 0);
                for (int b = 0; b < nbits; b += 8) {
                    const int take = std::min(8, nbits - b);
                    e.userData[static_cast<std::size_t>(b / 8)] = static_cast<std::uint8_t>(r.ubit(take));
                }
            } else {
                const std::size_t n = r.ubit(userDataLengthBits);
                if (n > limits::kMaxUserDataBytes || n * 8 > r.remaining())
                    return makeError("demo.stringtable_userdata", "string table userdata length is invalid",
                                     name_);
                e.userData.resize(n);
                r.bytes(e.userData.data(), n);
            }
            e.present = true;
        }
        history.push_back(e.string);
        if (history.size() > 32)
            history.pop_front();
        changed.push_back(index);
        if (r.overflowed())
            return makeError("demo.stringtable_truncated", "string table data ends early", name_);
    }
    (void)userDataSize_;
    return {};
}

} // namespace gmdr::demo

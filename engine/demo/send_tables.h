#pragma once

#include "core/bit_reader.h"
#include "core/error.h"
#include "demo/types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gmdr::demo {

struct ProtocolVariant;

struct SendProp {
    PropType type = PropType::Int;
    std::string name;
    std::uint32_t flags = 0;
    std::string dataTable;    // DataTable props
    std::string excludeTable; // Exclude props: the table the excluded prop lives in
    int elements = 0;         // arrays
    float low = 0, high = 0;
    int bits = 0;
    int elementIndex = -1;    // arrays: index of the element prop in the same table
};

struct SendTable {
    std::string name;
    bool needsDecoder = false;
    std::vector<SendProp> props;
};

struct ServerClass {
    int id = 0;
    std::string name;
    std::string tableName;
};

// One property in a class's flattened, network-ordered list.
struct FlatProp {
    const SendProp* prop = nullptr;
    const SendProp* element = nullptr; // arrays only
    const SendTable* table = nullptr;  // the table the prop comes from (for display)
};

struct DataTables {
    std::vector<SendTable> tables;
    std::unordered_map<std::string, int> tableIndex;
    std::vector<ServerClass> classes;

    const SendTable* table(const std::string& name) const;
    int classBits() const; // bits of a class id in svc_PacketEntities
};

// Parses a dem_datatables payload (SendTables followed by the class list).
Result<std::unique_ptr<DataTables>> parseDataTables(std::span<const std::uint8_t> payload,
                                                    const ProtocolVariant& variant);

// Source SDK 2013 flattening: excludes, collapsible base classes inlined, child tables before the table's
// own props, then a single-pass swap of SPROP_CHANGES_OFTEN props to the front.
Result<std::vector<FlatProp>> flattenClass(const DataTables& tables, const std::string& tableName);

} // namespace gmdr::demo

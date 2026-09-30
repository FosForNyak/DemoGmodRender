#include "demo/send_tables.h"

#include "core/limits.h"
#include "demo/protocol.h"

#include <algorithm>
#include <set>
#include <utility>

namespace gmdr::demo {

const SendTable* DataTables::table(const std::string& name) const {
    auto it = tableIndex.find(name);
    return it == tableIndex.end() ? nullptr : &tables[static_cast<std::size_t>(it->second)];
}

int DataTables::classBits() const {
    int bits = 0;
    std::size_t n = classes.size();
    while (n > 1) {
        n >>= 1;
        ++bits;
    }
    return bits + 1;
}

Result<std::unique_ptr<DataTables>> parseDataTables(std::span<const std::uint8_t> payload,
                                                    const ProtocolVariant& variant) {
    auto dt = std::make_unique<DataTables>();
    BitReader r(payload);
    while (r.bit()) {
        if (dt->tables.size() >= limits::kMaxSendTables)
            return makeError("demo.datatables_limit", "too many SendTables");
        SendTable t;
        t.needsDecoder = r.bit();
        t.name = r.string(limits::kMaxStringBytes);
        const std::size_t count = r.ubit(10);
        if (count > limits::kMaxPropsPerTable)
            return makeError("demo.datatables_limit", "too many props in a SendTable", t.name);
        t.props.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            SendProp p;
            const std::uint32_t type = r.ubit(5);
            if (type > 7)
                return makeError("demo.datatables_prop_type", "unknown SendProp type", t.name);
            p.type = static_cast<PropType>(type);
            p.name = r.string(limits::kMaxStringBytes);
            p.flags = r.ubit(variant.sendPropFlagBits);
            if (p.type == PropType::DataTable) {
                p.dataTable = r.string(limits::kMaxStringBytes);
            } else if (p.flags & prop_flags::Exclude) {
                p.excludeTable = r.string(limits::kMaxStringBytes);
            } else if (p.type == PropType::Array) {
                p.elements = static_cast<int>(r.ubit(10));
                if (i == 0)
                    return makeError("demo.datatables_array", "array prop without an element prop", t.name);
                p.elementIndex = static_cast<int>(i) - 1;
            } else {
                p.low = r.float32();
                p.high = r.float32();
                p.bits = static_cast<int>(r.ubit(7));
            }
            t.props.push_back(std::move(p));
            if (r.overflowed())
                return makeError("demo.datatables_truncated", "dem_datatables ends early", t.name);
        }
        dt->tableIndex[t.name] = static_cast<int>(dt->tables.size());
        dt->tables.push_back(std::move(t));
    }
    const std::size_t classes = r.ubit(16);
    if (classes > limits::kMaxServerClasses)
        return makeError("demo.datatables_limit", "too many server classes");
    dt->classes.reserve(classes);
    for (std::size_t i = 0; i < classes; ++i) {
        ServerClass c;
        c.id = static_cast<int>(r.ubit(16));
        c.name = r.string(limits::kMaxStringBytes);
        c.tableName = r.string(limits::kMaxStringBytes);
        if (c.id != static_cast<int>(i))
            return makeError("demo.datatables_class_order", "server class ids are not sequential");
        dt->classes.push_back(std::move(c));
    }
    if (r.overflowed())
        return makeError("demo.datatables_truncated", "dem_datatables ends early");
    // Array element props must be scalar props of the same table.
    for (auto& t : dt->tables)
        for (auto& p : t.props)
            if (p.type == PropType::Array) {
                const auto& el = t.props[static_cast<std::size_t>(p.elementIndex)];
                if (el.type == PropType::Array || el.type == PropType::DataTable)
                    return makeError("demo.datatables_array", "array element has an invalid type", t.name);
            }
    return dt;
}

namespace {

class Flattener {
public:
    explicit Flattener(const DataTables& dt) : dt_(dt) {}

    // Tables form a DAG; a crafted file could make naive recursion exponential. Every visit is counted.
    Result<void> step(const SendTable& t, std::size_t depth) {
        if (depth > limits::kMaxTableDepth)
            return makeError("demo.datatables_depth", "SendTable nesting too deep", t.name);
        if (++steps_ > 200000)
            return makeError("demo.datatables_limit", "SendTable graph is too large", t.name);
        return {};
    }

    Result<void> gatherExcludes(const SendTable& t, std::size_t depth) {
        GMDR_TRY(step(t, depth));
        for (const auto& p : t.props) {
            if (p.flags & prop_flags::Exclude) {
                excludes_.emplace(p.excludeTable, p.name);
            } else if (p.type == PropType::DataTable) {
                const SendTable* sub = dt_.table(p.dataTable);
                if (!sub)
                    return makeError("demo.datatables_missing", "SendTable references an unknown table", p.dataTable);
                GMDR_TRY(gatherExcludes(*sub, depth + 1));
            }
        }
        return {};
    }

    Result<void> build(const SendTable& t, std::size_t depth) {
        std::vector<FlatProp> own;
        GMDR_TRY(iterate(t, own, depth));
        for (auto& fp : own)
            GMDR_TRY(push(fp));
        return {};
    }

    std::vector<FlatProp> take() { return std::move(flat_); }

private:
    Result<void> iterate(const SendTable& t, std::vector<FlatProp>& own, std::size_t depth) {
        GMDR_TRY(step(t, depth));
        for (const auto& p : t.props) {
            if (p.flags & (prop_flags::Exclude | prop_flags::InsideArray))
                continue;
            if (excludes_.count({t.name, p.name}))
                continue;
            if (p.type == PropType::DataTable) {
                const SendTable* sub = dt_.table(p.dataTable);
                if (!sub)
                    return makeError("demo.datatables_missing", "SendTable references an unknown table", p.dataTable);
                if (p.flags & prop_flags::Collapsible)
                    GMDR_TRY(iterate(*sub, own, depth + 1));
                else
                    GMDR_TRY(build(*sub, depth + 1));
            } else {
                FlatProp fp{&p, nullptr, &t};
                if (p.type == PropType::Array)
                    fp.element = &t.props[static_cast<std::size_t>(p.elementIndex)];
                own.push_back(fp);
            }
        }
        return {};
    }

    Result<void> push(const FlatProp& fp) {
        if (flat_.size() >= limits::kMaxFlatProps)
            return makeError("demo.datatables_limit", "too many props in a class");
        flat_.push_back(fp);
        return {};
    }

    const DataTables& dt_;
    std::set<std::pair<std::string, std::string>> excludes_;
    std::vector<FlatProp> flat_;
    std::size_t steps_ = 0;
};

} // namespace

Result<std::vector<FlatProp>> flattenClass(const DataTables& tables, const std::string& tableName) {
    const SendTable* root = tables.table(tableName);
    if (!root)
        return makeError("demo.datatables_missing", "class table not found", tableName);
    Flattener f(tables);
    GMDR_TRY(f.gatherExcludes(*root, 0));
    GMDR_TRY(f.build(*root, 0));
    std::vector<FlatProp> flat = f.take();
    std::size_t start = 0;
    for (std::size_t i = 0; i < flat.size(); ++i) {
        if (flat[i].prop->flags & prop_flags::ChangesOften) {
            std::swap(flat[i], flat[start]);
            ++start;
        }
    }
    return flat;
}

} // namespace gmdr::demo

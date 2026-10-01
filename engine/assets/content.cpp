#include "assets/content.h"

#include "core/text.h"

#include <charconv>
#include <set>

namespace gmdr::assets {

namespace {

// Source prefixes a sound name may carry (stream, spatial, dry mix, ...); they are not part of the path.
bool isSoundChar(char c) {
    switch (c) {
    case '*':
    case '#':
    case '@':
    case '>':
    case '<':
    case '^':
    case ')':
    case '(':
    case '}':
    case '$':
    case '&':
    case '~':
    case '`':
        return true;
    default:
        return false;
    }
}

std::string extension(std::string_view p) {
    const auto slash = p.find_last_of('/');
    const auto dot = p.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return {};
    return toLowerAscii(p.substr(dot));
}

std::optional<std::uint64_t> workshopIdOf(std::string_view name) {
    // Workshop addons are listed in "downloadables" as "<id>.gma".
    if (!endsWith(name, ".gma"))
        return std::nullopt;
    const auto digits = name.substr(0, name.size() - 4);
    std::uint64_t v = 0;
    auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), v);
    if (ec != std::errc() || p != digits.data() + digits.size() || v == 0)
        return std::nullopt;
    return v;
}

class Checker {
public:
    Checker(const Vfs& vfs, const ContentOverlay* pak, ContentReport& report)
        : vfs_(vfs), pak_(pak), report_(report) {}

    void file(const std::string& kind, const std::string& name, const std::string& path) {
        const std::string key = kind + "\n" + normalizeGamePath(path);
        if (!seen_.insert(key).second)
            return;
        ContentItem item{kind, name, normalizeGamePath(path)};
        if (auto hit = vfs_.find(item.path)) {
            item.status = ContentStatus::Found;
            item.where = sourceKindName(hit->kind);
            item.source = hit->source;
            item.workshopId = hit->workshopId;
            item.title = hit->addonTitle;
            ++report_.found;
        } else if (pak_ && pak_->files.count(item.path)) {
            item.status = ContentStatus::Found;
            item.where = sourceKindName(SourceKind::Pakfile);
            item.source = pak_->name;
            ++report_.found;
        } else {
            item.status = ContentStatus::Missing;
            ++report_.missing;
        }
        report_.items.push_back(std::move(item));
    }

    void fixed(const std::string& kind, const std::string& name, ContentStatus status) {
        if (!seen_.insert(kind + "\n" + name).second)
            return;
        ContentItem item{kind, name, {}};
        item.status = status;
        if (status == ContentStatus::NotChecked)
            ++report_.notChecked;
        report_.items.push_back(std::move(item));
    }

    void workshop(const std::string& name, std::uint64_t id) {
        if (!seen_.insert("workshop\n" + std::to_string(id)).second)
            return;
        ContentItem item{"workshop", name, {}};
        item.workshopId = id;
        const auto w = vfs_.workshopItem(id);
        switch (w.state) {
        case WorkshopState::Installed:
            item.status = ContentStatus::WorkshopInstalled;
            item.where = "gma";
            item.source = pathToUtf8(w.gma.filename());
            item.title = w.title;
            ++report_.workshopInstalled;
            break;
        case WorkshopState::Legacy:
            item.status = ContentStatus::WorkshopLegacy;
            item.where = "legacy";
            item.source = pathToUtf8(w.gma.filename());
            ++report_.workshopMissing;
            break;
        case WorkshopState::Missing:
            item.status = ContentStatus::WorkshopMissing;
            ++report_.workshopMissing;
            break;
        }
        report_.items.push_back(std::move(item));
    }

private:
    const Vfs& vfs_;
    const ContentOverlay* pak_;
    ContentReport& report_;
    std::set<std::string> seen_;
};

std::vector<std::string> strings(const Json& manifest, const char* key) {
    std::vector<std::string> out;
    auto it = manifest.find(key);
    if (it == manifest.end() || !it->is_array())
        return out;
    for (const auto& v : *it)
        if (v.is_string() && !v.get<std::string>().empty())
            out.push_back(v.get<std::string>());
    return out;
}

} // namespace

const char* contentStatusName(ContentStatus s) {
    switch (s) {
    case ContentStatus::Found:
        return "found";
    case ContentStatus::Missing:
        return "missing";
    case ContentStatus::Builtin:
        return "builtin";
    case ContentStatus::NotChecked:
        return "not-checked";
    case ContentStatus::WorkshopInstalled:
        return "workshop-installed";
    case ContentStatus::WorkshopLegacy:
        return "workshop-legacy";
    case ContentStatus::WorkshopMissing:
        return "workshop-missing";
    }
    return "unknown";
}

void ContentOverlay::add(std::string_view path) {
    files.insert(normalizeGamePath(path));
}

ContentReport checkContent(const Json& manifest, const Vfs& vfs, const ContentOverlay* pakfile) {
    ContentReport report;
    Checker c(vfs, pakfile, report);

    if (auto map = manifest.find("map");
        map != manifest.end() && map->is_string() && !map->get<std::string>().empty())
        c.file("map", map->get<std::string>(), "maps/" + map->get<std::string>() + ".bsp");

    for (const auto& m : strings(manifest, "models")) {
        if (m[0] == '*') {
            c.fixed("model", m, ContentStatus::Builtin); // brush model of the map
            continue;
        }
        const std::string ext = extension(m);
        if (ext == ".bsp")
            c.file("map", m, m);
        else if (ext == ".vmt" || ext == ".spr")
            c.file("material", m, startsWith(normalizeGamePath(m), "materials/") ? m : "materials/" + m);
        else
            c.file("model", m, m);
    }
    for (const auto& s : strings(manifest, "sounds")) {
        std::string_view v = s;
        while (!v.empty() && isSoundChar(v.front()))
            v.remove_prefix(1);
        if (v.empty())
            continue;
        if (v.front() == '!') {
            c.fixed("sound", s, ContentStatus::Builtin); // sentence name
            continue;
        }
        if (const std::string ext = extension(v); ext != ".wav" && ext != ".mp3" && ext != ".ogg") {
            c.fixed("soundscript", s, ContentStatus::NotChecked); // "Default.Tile.Standing": a script entry
            continue;
        }
        c.file("sound", s, "sound/" + std::string(v));
    }
    for (const auto& d : strings(manifest, "decals"))
        c.file("decal", d, "materials/" + d + ".vmt");
    for (const auto& g : strings(manifest, "generic"))
        c.file("generic", g, g);
    for (const auto& d : strings(manifest, "downloadables")) {
        if (auto id = workshopIdOf(d))
            c.workshop(d, *id);
        else
            c.file("download", d, d);
    }
    for (const auto& p : strings(manifest, "particles"))
        c.fixed("particle", p, ContentStatus::NotChecked);
    return report;
}

Json ContentReport::toJson() const {
    Json list = Json::array();
    for (const auto& i : this->items) {
        Json j = {{"kind", i.kind}, {"name", sanitizeUtf8(i.name)}, {"status", contentStatusName(i.status)}};
        if (!i.path.empty())
            j["path"] = sanitizeUtf8(i.path);
        if (!i.where.empty())
            j["where"] = i.where;
        if (!i.source.empty())
            j["source"] = sanitizeUtf8(i.source);
        if (i.workshopId)
            j["workshopId"] = std::to_string(i.workshopId); // string: JS numbers lose precision past 2^53
        if (!i.title.empty())
            j["title"] = sanitizeUtf8(i.title);
        list.push_back(std::move(j));
    }
    return {
        {"summary",
         {{"found", found},
          {"missing", missing},
          {"workshopInstalled", workshopInstalled},
          {"workshopMissing", workshopMissing},
          {"notChecked", notChecked}}},
        {"items", std::move(list)},
    };
}

} // namespace gmdr::assets

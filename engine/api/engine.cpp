#include "api/engine.h"

#include "api/session.h"
#include "api/settings.h"
#include "api/values.h"
#include "assets/content.h"
#include "assets/locator.h"
#include "assets/vfs.h"
#include "core/file.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/process.h"
#include "core/text.h"
#include "demo/events.h"
#include "demo/header.h"
#include "demo/statedb/format.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <map>
#include <set>

namespace gmdr::api {

namespace fs = std::filesystem;
using demo::statedb::StateReader;
using demo::statedb::WorldState;

namespace {

constexpr const char* kVersion = "0.1.0";

Json errorJson(const Error& e) {
    Json j = {{"code", e.code}, {"message", e.message}};
    if (!e.details.empty())
        j["details"] = e.details;
    return j;
}

Error badArgs(const std::string& what) {
    return makeError("api.bad_args", "invalid command arguments", what);
}

Result<std::string> argString(const Json& a, const char* key) {
    auto it = a.find(key);
    if (it == a.end() || !it->is_string())
        return badArgs(std::string(key) + " must be a string");
    return it->get<std::string>();
}

Result<std::int64_t> argInt(const Json& a, const char* key,
                            std::optional<std::int64_t> fallback = std::nullopt) {
    auto it = a.find(key);
    if (it == a.end() || it->is_null()) {
        if (fallback)
            return *fallback;
        return badArgs(std::string(key) + " is required");
    }
    if (it->is_number_integer())
        return it->get<std::int64_t>();
    if (it->is_number_float() && std::isfinite(it->get<double>()))
        return static_cast<std::int64_t>(it->get<double>());
    return badArgs(std::string(key) + " must be an integer");
}

// uids are 64-bit: sent as decimal strings (JSON numbers lose precision past 2^53).
std::string uidString(std::uint64_t uid) {
    return std::to_string(uid);
}

std::optional<std::uint64_t> parseUid(const Json& a) {
    auto it = a.find("uid");
    if (it == a.end())
        return std::nullopt;
    if (it->is_string()) {
        const std::string s = it->get<std::string>();
        std::uint64_t v = 0;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec == std::errc() && p == s.data() + s.size())
            return v;
        return std::nullopt;
    }
    if (it->is_number_unsigned())
        return it->get<std::uint64_t>();
    return std::nullopt;
}

std::uint64_t directorySize(const fs::path& dir) {
    std::uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec))
            total += it->file_size(ec);
    return total;
}

} // namespace

Result<EngineConfig> makeConfig(const Json& o) {
    if (!o.is_null() && !o.is_object())
        return badArgs("engine config must be an object");
    EngineConfig c;
    auto path = [&](const char* key, fs::path fallback) -> Result<fs::path> {
        if (o.is_object() && o.contains(key)) {
            if (!o[key].is_string())
                return badArgs(std::string(key) + " must be a string");
            return pathFromUtf8(o[key].get<std::string>());
        }
        return fallback;
    };
    GMDR_ASSIGN(c.cacheDir, path("cacheDir", userCacheDir()));
    GMDR_ASSIGN(c.configDir, path("configDir", userConfigDir()));
#ifdef _WIN32
    const char* importerName = "gmdr-import.exe";
#else
    const char* importerName = "gmdr-import";
#endif
    GMDR_ASSIGN(c.importer, path("importer", currentExecutablePath().parent_path() / importerName));
    return c;
}

struct Engine::Impl {
    explicit Impl(EngineConfig c) : config(std::move(c)), settings(config.configDir / "settings.json") {
        registerCommands();
        watchdog = std::thread([this] { watch(); });
    }

    ~Impl() {
        {
            std::lock_guard lock(watchMutex);
            stopping = true;
        }
        watchCv.notify_all();
        watchdog.join();
        std::map<std::string, std::shared_ptr<Session>> all;
        {
            std::lock_guard lock(sessionsMutex);
            all.swap(sessions);
        }
        for (auto& [id, s] : all)
            s->close();
    }

    EngineConfig config;
    Settings settings;

    std::mutex sinkMutex;
    EventSink sink;

    std::mutex sessionsMutex;
    std::map<std::string, std::shared_ptr<Session>> sessions;
    int nextSession = 1;

    std::mutex gmodMutex;
    std::optional<assets::GmodInstall> install;
    std::shared_ptr<const assets::Vfs> vfs;
    std::optional<Error> gmodError;
    std::string gmodKey; // settings value the cache was built for

    std::thread watchdog;
    std::mutex watchMutex;
    std::condition_variable watchCv;
    bool stopping = false;

    using Handler = Result<Json> (Impl::*)(const Json&);
    std::map<std::string, Handler, std::less<>> commands;

    void emit(const std::string& type, Json payload) {
        payload["type"] = type;
        const std::string text = payload.dump(-1, ' ', false, Json::error_handler_t::replace);
        std::lock_guard lock(sinkMutex);
        if (sink)
            sink(text);
    }

    Emit emitter() {
        return [this](const std::string& type, Json payload) { emit(type, std::move(payload)); };
    }

    void watch() {
        std::unique_lock lock(watchMutex);
        while (!watchCv.wait_for(lock, std::chrono::seconds(1), [this] { return stopping; })) {
            std::vector<std::shared_ptr<Session>> all;
            {
                std::lock_guard l(sessionsMutex);
                for (auto& [id, s] : sessions)
                    all.push_back(s);
            }
            for (auto& s : all)
                s->checkIdle(config.importerIdleTimeout);
        }
    }

    Result<std::shared_ptr<Session>> session(const Json& args) {
        GMDR_ASSIGN(const std::string id, argString(args, "demo"));
        std::lock_guard lock(sessionsMutex);
        auto it = sessions.find(id);
        if (it == sessions.end())
            return makeError("demo.not_open", "this demo is not open", id);
        return it->second;
    }

    // Reader plus the tick a state query may use (clamped to what the importer has written).
    struct Ready {
        std::shared_ptr<Session> session;
        std::shared_ptr<StateReader> reader;
        Session::Status status;
        Tick tick = 0;
    };
    Result<Ready> ready(const Json& args, bool needTick) {
        Ready r;
        GMDR_ASSIGN(r.session, session(args));
        r.status = r.session->status();
        r.reader = r.session->reader();
        if (!r.reader || r.status.readyTick < 0)
            return makeError("import.not_ready", "the first part of the demo is still being imported");
        if (needTick) {
            GMDR_ASSIGN(const std::int64_t tick, argInt(args, "tick"));
            r.tick = std::clamp<Tick>(tick, 0, r.status.readyTick);
        }
        return r;
    }

    Result<std::shared_ptr<const assets::Vfs>> gmodVfs(bool refresh) {
        std::lock_guard lock(gmodMutex);
        const std::string key = settings.gmodPath();
        if (!refresh && key == gmodKey && (vfs || gmodError)) {
            if (gmodError)
                return *gmodError;
            return vfs;
        }
        gmodKey = key;
        vfs.reset();
        install.reset();
        gmodError.reset();
        std::optional<fs::path> manual;
        if (!key.empty())
            manual = pathFromUtf8(key);
        auto g = assets::locateGmod(manual);
        if (!g) {
            gmodError = g.error();
            return g.error();
        }
        auto v = assets::Vfs::build(*g);
        if (!v) {
            gmodError = v.error();
            return v.error();
        }
        install = std::move(*g);
        vfs = std::make_shared<const assets::Vfs>(std::move(*v));
        for (const auto& w : install->warnings)
            emit("log", {{"level", "warn"}, {"message", w}});
        return vfs;
    }

    // ---- app, settings, gmod, cache ----------------------------------------------------------------------

    Result<Json> appInfo(const Json&) {
        std::error_code ec;
        return Json{{"name", "DemoGmodRender"},
                    {"version", kVersion},
                    {"formatVersion", demo::statedb::kFormatVersion},
                    {"parserVersion", demo::statedb::kParserVersion},
                    {"cacheDir", pathToUtf8(config.cacheDir)},
                    {"configDir", pathToUtf8(config.configDir)},
                    {"importer", pathToUtf8(config.importer)},
                    {"importerFound", fs::is_regular_file(config.importer, ec)}};
    }

    Result<Json> settingsGet(const Json& args) {
        if (args.contains("key")) {
            GMDR_ASSIGN(const std::string key, argString(args, "key"));
            return Json{{"key", key}, {"value", settings.get(key)}};
        }
        return Json{{"values", settings.all()}};
    }

    Result<Json> settingsSet(const Json& args) {
        GMDR_ASSIGN(const std::string key, argString(args, "key"));
        const Json value = args.contains("value") ? args["value"] : Json();
        GMDR_TRY(settings.set(key, value));
        emit("settings.changed", {{"key", key}});
        return Json{{"key", key}, {"value", settings.get(key)}};
    }

    Result<Json> gmodLocate(const Json& args) {
        const bool refresh = args.value("refresh", false);
        auto v = gmodVfs(refresh);
        if (!v)
            return v.error();
        std::lock_guard lock(gmodMutex);
        Json mounts = Json::array();
        for (const auto& m : install->mounts)
            mounts.push_back(
                {{"name", m.name}, {"path", pathToUtf8(m.path)}, {"origin", m.origin}, {"found", m.found}});
        const auto& s = (*v)->stats();
        return Json{{"path", pathToUtf8(install->gmodRoot)},
                    {"origin", install->origin},
                    {"steam", pathToUtf8(install->steamRoot)},
                    {"mounts", mounts},
                    {"gmas", s.gmas},
                    {"badGmas", s.badGmas},
                    {"vpks", s.vpks},
                    {"files", s.vpkFiles + s.gmaFiles},
                    {"workshopLegacy", s.workshopLegacy},
                    {"seconds", s.seconds},
                    {"warnings", install->warnings}};
    }

    std::set<fs::path> openDirs() {
        std::set<fs::path> dirs;
        std::lock_guard lock(sessionsMutex);
        for (const auto& [id, s] : sessions)
            dirs.insert(s->dir);
        return dirs;
    }

    Result<Json> cacheInfo(const Json&) {
        const fs::path demos = config.cacheDir / "demos";
        std::uint64_t bytes = 0, count = 0;
        std::error_code ec;
        for (fs::directory_iterator it(demos, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec))
                continue;
            ++count;
            bytes += directorySize(it->path());
        }
        return Json{{"dir", pathToUtf8(demos)},
                    {"bytes", bytes},
                    {"demos", count},
                    {"limitBytes", settings.cacheLimitBytes()}};
    }

    Result<Json> cacheClear(const Json&) {
        const auto open = openDirs();
        const fs::path demos = config.cacheDir / "demos";
        std::uint64_t freed = 0, removed = 0;
        std::error_code ec;
        std::vector<fs::path> victims;
        for (fs::directory_iterator it(demos, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec) && !open.count(it->path()))
                victims.push_back(it->path());
        for (const auto& d : victims) {
            const auto size = directorySize(d);
            if (fs::remove_all(d, ec) != static_cast<std::uintmax_t>(-1) && !ec) {
                freed += size;
                ++removed;
            }
        }
        return Json{{"freedBytes", freed}, {"removed", removed}};
    }

    // Removes the least recently used cached demos beyond the size limit (open demos are kept).
    void enforceCacheLimit() {
        const std::uint64_t limit = settings.cacheLimitBytes();
        const auto open = openDirs();
        struct Entry {
            fs::path dir;
            std::uint64_t bytes;
            fs::file_time_type used;
        };
        std::vector<Entry> entries;
        std::uint64_t total = 0;
        std::error_code ec;
        for (fs::directory_iterator it(config.cacheDir / "demos", ec), end; !ec && it != end;
             it.increment(ec)) {
            if (!it->is_directory(ec))
                continue;
            Entry e{it->path(), directorySize(it->path()), fs::file_time_type::min()};
            std::error_code tec;
            const auto t = fs::last_write_time(it->path() / "state.gmstate", tec);
            if (!tec)
                e.used = t;
            total += e.bytes;
            entries.push_back(std::move(e));
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.used < b.used; });
        for (const auto& e : entries) {
            if (total <= limit)
                break;
            if (open.count(e.dir))
                continue;
            std::error_code rec;
            fs::remove_all(e.dir, rec);
            if (!rec) {
                total -= e.bytes;
                emit("log", {{"level", "info"}, {"message", "removed an old demo from the cache"}});
            }
        }
    }

    // ---- demo sessions ---------------------------------------------------------------------------------

    Result<Json> sessionJson(const Session& s) {
        const auto st = s.status();
        Json j = {{"demo", s.id},
                  {"path", pathToUtf8(s.demoPath)},
                  {"name", pathToUtf8(s.demoPath.filename())},
                  {"hash", s.hash.hex()},
                  {"import",
                   {{"state", importStateName(st.state)},
                    {"cached", st.cached},
                    {"firstTick", st.firstTick},
                    {"lastTick", st.lastTick},
                    {"readyTick", st.readyTick}}}};
        if (st.error)
            j["import"]["error"] = errorJson(*st.error);
        if (!st.stats.is_null())
            j["import"]["stats"] = st.stats;
        return j;
    }

    Result<Json> demoOpen(const Json& args) {
        GMDR_ASSIGN(const std::string pathText, argString(args, "path"));
        const fs::path path = pathFromUtf8(pathText);
        std::error_code ec;
        if (!path.is_absolute())
            return makeError("demo.bad_path", "the demo path must be absolute", pathText);
        const fs::path canonical = fs::weakly_canonical(path, ec);
        {
            std::lock_guard lock(sessionsMutex);
            for (const auto& [id, s] : sessions)
                if (s->demoPath == canonical)
                    return sessionJson(*s);
        }
        if (!fs::is_regular_file(canonical, ec))
            return makeError("demo.not_found", "the demo file does not exist", pathText);

        Blake3Digest hash;
        {
            auto file = File::open(canonical, File::Mode::Read);
            if (!file)
                return file.error();
            auto map = MappedFile::map(*file);
            if (!map)
                return map.error();
            auto header = demo::parseHeader(map->data());
            if (!header)
                return header.error();
            hash = blake3(map->data());
        }
        const fs::path dir = config.cacheDir / "demos" / hash.hex();
        fs::create_directories(dir, ec);
        if (ec)
            return makeError("cache.unavailable", "cannot create the cache folder", ec.message());

        std::string id;
        {
            std::lock_guard lock(sessionsMutex);
            id = "d" + std::to_string(nextSession++);
        }
        auto s = std::make_shared<Session>(id, canonical, hash, dir);
        if (auto cached = s->openCached(); !cached) {
            if (cached.error().code != "statedb.missing")
                emit("log", {{"level", "info"},
                             {"message", "rebuilding the cached state: " + cached.error().message}});
            GMDR_TRY(s->startImport(config, emitter()));
        }
        {
            std::lock_guard lock(sessionsMutex);
            sessions[id] = s;
        }
        settings.addRecent(pathToUtf8(canonical), pathToUtf8(canonical.filename()));
        return sessionJson(*s);
    }

    Result<Json> demoClose(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        {
            std::lock_guard lock(sessionsMutex);
            sessions.erase(s->id);
        }
        s->close();
        enforceCacheLimit();
        return Json{{"demo", s->id}, {"closed", true}};
    }

    Result<Json> demoList(const Json&) {
        Json list = Json::array();
        std::vector<std::shared_ptr<Session>> all;
        {
            std::lock_guard lock(sessionsMutex);
            for (auto& [id, s] : sessions)
                all.push_back(s);
        }
        for (auto& s : all)
            if (auto j = sessionJson(*s))
                list.push_back(std::move(*j));
        return Json{{"demos", list}};
    }

    Result<Json> demoInfo(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        GMDR_ASSIGN(Json j, sessionJson(*s));
        if (auto reader = s->reader()) {
            if (auto info = reader->info()) {
                if (info->contains("demo"))
                    j["header"] = (*info)["demo"];
                if (info->contains("server")) {
                    j["server"] = (*info)["server"];
                    const double interval = (*info)["server"].value("tickInterval", 0.0);
                    if (interval > 0) {
                        j["tickInterval"] = interval;
                        j["tickRate"] = 1.0 / interval;
                        const auto st = s->status();
                        if (st.lastTick >= 0)
                            j["durationSeconds"] = static_cast<double>(st.lastTick) * interval;
                    }
                }
                if (info->contains("decodeErrors"))
                    j["decodeErrors"] = (*info)["decodeErrors"];
            }
        }
        return j;
    }

    Result<Json> importCancel(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        s->cancel();
        return Json{{"demo", s->id}};
    }

    // ---- state queries ---------------------------------------------------------------------------------

    int localPlayerIndex(StateReader& reader) {
        auto info = reader.info();
        if (!info || !info->contains("server"))
            return -1;
        return (*info)["server"].value("playerSlot", -2) + 1;
    }

    Json entitySummary(const WorldState& s, const demo::statedb::EntityState& e, const ClassInfo& ci,
                       const std::array<std::uint8_t, 32>& demoHash) {
        Json j = {{"uid", uidString(demo::statedb::lifeUid(demoHash, e.life, e.index, e.serial))},
                  {"index", e.index},
                  {"class", ci.name},
                  {"group", ci.group},
                  {"life", e.life},
                  {"inPvs", e.inPvs}};
        if (ci.group == "player")
            if (auto p = playerInfo(s, e.index)) {
                j["name"] = p->name;
                if (p->bot)
                    j["bot"] = true;
            }
        if (ci.modelIndex >= 0 && static_cast<std::size_t>(ci.modelIndex) < e.props.size())
            if (auto* m = std::get_if<std::int64_t>(&e.props[static_cast<std::size_t>(ci.modelIndex)].v))
                if (const auto* t = s.table("modelprecache"))
                    if (const std::string* name = t->string(static_cast<int>(*m)); name && !name->empty())
                        j["model"] = sanitizeUtf8(*name);
        return j;
    }

    Result<Json> stateEntities(const Json& args) {
        GMDR_ASSIGN(auto r, ready(args, true));
        GMDR_ASSIGN(auto classes, r.session->classes());
        const Json filter = args.value("filter", Json::object());
        const std::string text = toLowerAscii(filter.value("text", ""));
        const std::string group = filter.value("group", "");
        const bool pvsOnly = filter.value("inPvs", false);
        Json list = Json::array();
        const auto hash = r.reader->demoHash();
        GMDR_TRY(r.reader->withStateAt(r.tick, [&](const WorldState& s) {
            for (const auto& slot : s.entities) {
                if (!slot)
                    continue;
                const auto& e = *slot;
                if (e.classId < 0 || static_cast<std::size_t>(e.classId) >= classes->size())
                    continue;
                const auto& ci = (*classes)[static_cast<std::size_t>(e.classId)];
                if (pvsOnly && !e.inPvs)
                    continue;
                if (!group.empty() && ci.group != group)
                    continue;
                Json j = entitySummary(s, e, ci, hash);
                if (!text.empty()) {
                    const std::string hay =
                        toLowerAscii(ci.name + " " + j.value("name", "") + " " + j.value("model", "") + " " +
                                     std::to_string(e.index));
                    if (hay.find(text) == std::string::npos)
                        continue;
                }
                list.push_back(std::move(j));
            }
        }));
        return Json{{"tick", r.tick}, {"entities", list}};
    }

    Result<Json> stateEntity(const Json& args) {
        GMDR_ASSIGN(auto r, ready(args, true));
        GMDR_ASSIGN(auto classes, r.reader->schema());
        GMDR_ASSIGN(auto infos, r.session->classes());
        const auto uid = parseUid(args);
        const std::int64_t index = args.value("index", std::int64_t{-1});
        if (!uid && index < 0)
            return badArgs("uid or index is required");
        const auto hash = r.reader->demoHash();
        std::optional<Json> out;
        GMDR_TRY(r.reader->withStateAt(r.tick, [&](const WorldState& s) {
            const demo::statedb::EntityState* found = nullptr;
            if (uid) {
                for (const auto& slot : s.entities)
                    if (slot && demo::statedb::lifeUid(hash, slot->life, slot->index, slot->serial) == *uid) {
                        found = &*slot;
                        break;
                    }
            } else if (static_cast<std::size_t>(index) < s.entities.size() &&
                       s.entities[static_cast<std::size_t>(index)]) {
                found = &*s.entities[static_cast<std::size_t>(index)];
            }
            if (!found || static_cast<std::size_t>(found->classId) >= classes->size())
                return;
            const auto& e = *found;
            const auto& cls = (*classes)[static_cast<std::size_t>(e.classId)];
            const auto& ci = (*infos)[static_cast<std::size_t>(e.classId)];
            const auto* vars = s.table("networkvars");
            std::vector<int> changed;
            if (auto it = s.changed.find(e.index); it != s.changed.end())
                changed = it->second;
            Json groups = Json::array();
            std::map<std::string, std::size_t> groupIndex;
            for (std::size_t i = 0; i < cls.props.size() && i < e.props.size(); ++i) {
                const auto& p = cls.props[i];
                auto [it, inserted] = groupIndex.try_emplace(p.table, groups.size());
                if (inserted)
                    groups.push_back({{"table", p.table}, {"props", Json::array()}});
                Json prop = {{"i", i},
                             {"name", p.name},
                             {"type", propTypeName(p.type)},
                             {"value", propToJson(e.props[i], vars)}};
                if (std::binary_search(changed.begin(), changed.end(), static_cast<int>(i)))
                    prop["changed"] = true;
                groups[it->second]["props"].push_back(std::move(prop));
            }
            Json j = entitySummary(s, e, ci, hash);
            j["serial"] = e.serial;
            j["table"] = cls.table;
            j["tick"] = r.tick;
            j["changedCount"] = changed.size();
            j["groups"] = std::move(groups);
            out = std::move(j);
        }));
        if (!out)
            return makeError("entity.not_found", "the entity does not exist at this tick");
        return *out;
    }

    // The entity `e` is attached to (EHANDLE in `moveparent`: 13-bit index + 10-bit serial), if it exists at
    // this tick with a matching serial and a known class.
    static const demo::statedb::EntityState*
    parentOf(const WorldState& s, const demo::statedb::EntityState& e, const ClassInfo& ci) {
        if (ci.moveParent < 0 || static_cast<std::size_t>(ci.moveParent) >= e.props.size())
            return nullptr;
        const auto* h = std::get_if<std::int64_t>(&e.props[static_cast<std::size_t>(ci.moveParent)].v);
        if (!h || *h == kInvalidEHandle || *h < 0)
            return nullptr;
        const auto index = static_cast<std::size_t>(*h & 0x1FFF);
        const int serial = static_cast<int>((*h >> 13) & 0x3FF);
        if (index >= s.entities.size() || !s.entities[index] || s.entities[index]->serial != serial ||
            s.entities[index]->index == e.index)
            return nullptr;
        return &*s.entities[index];
    }

    Result<Json> statePositions(const Json& args) {
        GMDR_ASSIGN(auto r, ready(args, true));
        GMDR_ASSIGN(auto classes, r.session->classes());
        const int local = localPlayerIndex(*r.reader);
        const auto hash = r.reader->demoHash();
        Json list = Json::array();
        GMDR_TRY(r.reader->withStateAt(r.tick, [&](const WorldState& s) {
            for (const auto& slot : s.entities) {
                if (!slot)
                    continue;
                const auto& e = *slot;
                if (static_cast<std::size_t>(e.classId) >= classes->size())
                    continue;
                const auto& ci = (*classes)[static_cast<std::size_t>(e.classId)];
                if (ci.group == "world")
                    continue;
                std::optional<Placement> p;
                if (const auto* parent = parentOf(s, e, ci)) {
                    // Attached (weapon in hand, player in a seat): the origin is relative to the parent.
                    // Players are drawn at their seat or vehicle; other attached objects are left out.
                    if (ci.group != "player")
                        continue;
                    const auto& pci = (*classes)[static_cast<std::size_t>(parent->classId)];
                    p = placement(*parent, pci, false);
                    if (p) {
                        if (auto own = placement(e, ci, e.index == local)) {
                            p->yaw = own->yaw;
                            p->pitch = own->pitch;
                        }
                    }
                } else {
                    p = placement(e, ci, e.index == local);
                }
                if (!p)
                    continue;
                Json j = {{"uid", uidString(demo::statedb::lifeUid(hash, e.life, e.index, e.serial))},
                          {"index", e.index},
                          {"group", ci.group},
                          {"inPvs", e.inPvs},
                          {"x", p->origin.x},
                          {"y", p->origin.y},
                          {"z", p->origin.z},
                          {"yaw", p->yaw}};
                if (ci.group == "player") {
                    j["pitch"] = p->pitch;
                    if (auto info = playerInfo(s, e.index))
                        j["name"] = info->name;
                }
                list.push_back(std::move(j));
            }
        }));
        Json out = {{"tick", r.tick}, {"localPlayer", local}, {"entities", list}};
        if (auto cam = r.reader->camera(std::max<Tick>(0, r.tick - 132), r.tick); cam && !cam->empty()) {
            const auto& c = cam->back();
            out["camera"] = {{"tick", c.tick},  {"x", c.origin.x},     {"y", c.origin.y},
                             {"z", c.origin.z}, {"pitch", c.angles.x}, {"yaw", c.angles.y}};
        }
        return out;
    }

    Result<Json> entityLifetime(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        const auto uid = parseUid(args);
        if (!uid)
            return badArgs("uid is required");
        auto reader = s->reader();
        if (!reader)
            return makeError("import.not_ready", "the demo is still being indexed");
        GMDR_ASSIGN(auto lives, reader->lives());
        GMDR_ASSIGN(auto schema, reader->schema());
        const Tick last = s->status().lastTick;
        for (const auto& l : *lives) {
            if (l.uid != *uid)
                continue;
            Json pvs = Json::array();
            for (const auto& [a, b] : l.pvs)
                pvs.push_back({a, b < 0 ? last : b});
            Json j = {{"uid", uidString(l.uid)},
                      {"index", l.index},
                      {"serial", l.serial},
                      {"firstTick", l.firstTick},
                      {"lastTick", l.lastTick < 0 ? last : l.lastTick},
                      {"deleted", l.lastTick >= 0},
                      {"pvs", pvs}};
            if (static_cast<std::size_t>(l.classId) < schema->size())
                j["class"] = (*schema)[static_cast<std::size_t>(l.classId)].name;
            return j;
        }
        return makeError("entity.not_found", "no entity life with this uid");
    }

    Result<Json> timelineSummary(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        GMDR_ASSIGN(const std::int64_t bins, argInt(args, "bins", 1000));
        if (bins < 1 || bins > 8192)
            return badArgs("bins must be 1..8192");
        const auto st = s->status();
        auto reader = s->reader();
        if (!reader || st.lastTick < 0)
            return makeError("import.not_ready", "the demo is still being indexed");
        const bool complete = st.state == ImportState::Ready;
        if (complete) {
            std::lock_guard lock(s->cacheMutex);
            if (auto it = s->summaryCache.find(static_cast<int>(bins)); it != s->summaryCache.end())
                return it->second;
        }
        GMDR_ASSIGN(auto events, reader->events(st.firstTick, st.lastTick, SIZE_MAX));
        const double span = static_cast<double>(std::max<Tick>(1, st.lastTick - st.firstTick + 1));
        std::map<int, std::pair<std::uint64_t, std::vector<std::uint32_t>>> tracks;
        for (const auto& e : events) {
            auto& t = tracks[static_cast<int>(e.kind)];
            if (t.second.empty())
                t.second.assign(static_cast<std::size_t>(bins), 0);
            const auto bin = std::clamp<std::int64_t>(
                static_cast<std::int64_t>(static_cast<double>(e.tick - st.firstTick) / span *
                                          static_cast<double>(bins)),
                0, bins - 1);
            ++t.second[static_cast<std::size_t>(bin)];
            ++t.first;
        }
        Json list = Json::array();
        for (const auto& [kind, t] : tracks)
            list.push_back({{"kind", demo::eventKindName(static_cast<demo::EventKind>(kind))},
                            {"total", t.first},
                            {"counts", t.second}});
        Json out = {{"firstTick", st.firstTick}, {"lastTick", st.lastTick},
                    {"readyTick", st.readyTick}, {"bins", bins},
                    {"complete", complete},      {"tracks", list}};
        if (complete) {
            std::lock_guard lock(s->cacheMutex);
            s->summaryCache[static_cast<int>(bins)] = out;
        }
        return out;
    }

    Result<Json> timelineEvents(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        GMDR_ASSIGN(const std::int64_t from, argInt(args, "from"));
        GMDR_ASSIGN(const std::int64_t to, argInt(args, "to"));
        GMDR_ASSIGN(const std::int64_t limit, argInt(args, "limit", 1000));
        if (limit < 1 || limit > 20000)
            return badArgs("limit must be 1..20000");
        std::set<std::string> kinds;
        if (auto it = args.find("kinds"); it != args.end() && it->is_array())
            for (const auto& k : *it)
                if (k.is_string())
                    kinds.insert(k.get<std::string>());
        auto reader = s->reader();
        if (!reader)
            return makeError("import.not_ready", "the demo is still being indexed");
        GMDR_ASSIGN(auto events, reader->events(from, to, SIZE_MAX));
        Json list = Json::array();
        bool truncated = false;
        for (const auto& e : events) {
            const char* kind = demo::eventKindName(e.kind);
            if (!kinds.empty() && !kinds.count(kind))
                continue;
            if (list.size() >= static_cast<std::size_t>(limit)) {
                truncated = true;
                break;
            }
            Json j = {{"tick", e.tick}, {"kind", kind}, {"name", sanitizeUtf8(e.name)}};
            if (!e.summary.empty())
                j["summary"] = sanitizeUtf8(e.summary);
            if (e.entity >= 0)
                j["entity"] = e.entity;
            list.push_back(std::move(j));
        }
        return Json{{"events", list}, {"truncated", truncated}};
    }

    Result<Json> cameraTrack(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        GMDR_ASSIGN(const std::int64_t from, argInt(args, "from"));
        GMDR_ASSIGN(const std::int64_t to, argInt(args, "to"));
        GMDR_ASSIGN(std::int64_t step, argInt(args, "step", 1));
        step = std::max<std::int64_t>(1, step);
        if ((to - from) / step > 20000)
            return badArgs("too many samples: raise step");
        auto reader = s->reader();
        if (!reader)
            return makeError("import.not_ready", "the demo is still being indexed");
        GMDR_ASSIGN(auto samples, reader->camera(from, to));
        Json list = Json::array();
        Tick next = from;
        for (const auto& c : samples) {
            if (c.tick < next)
                continue;
            list.push_back({c.tick, c.origin.x, c.origin.y, c.origin.z, c.angles.x, c.angles.y, c.angles.z});
            next = c.tick + step;
        }
        return Json{{"format", {"tick", "x", "y", "z", "pitch", "yaw", "roll"}}, {"samples", list}};
    }

    // ---- content ---------------------------------------------------------------------------------------

    // Lists the map's pakfile inside gmdr-import: the map may have been downloaded from a server.
    Result<assets::ContentOverlay> listPakfile(const assets::Vfs& v, const std::string& map) {
        assets::ContentOverlay overlay;
        overlay.name = "maps/" + map + ".bsp";
        auto slice = v.locate(overlay.name);
        if (!slice)
            return overlay; // map missing or packed in a way we do not unpack: no overlay
        auto file = File::open(slice->path, File::Mode::Read);
        if (!file)
            return file.error();
        GMDR_TRY(file->setInheritable(true));
        ProcessOptions opts;
        opts.executable = config.importer;
        opts.args = {"--list-pakfile",
                     "--in-handle",
                     std::to_string(file->nativeHandle()),
                     "--offset",
                     std::to_string(slice->offset),
                     "--size",
                     std::to_string(slice->size)};
        opts.inheritHandles = {file->nativeHandle()};
        opts.memoryLimitBytes = 1ull << 30;
        auto child = ChildProcess::spawn(opts);
        if (!child)
            return child.error();
        std::string line;
        std::optional<Error> error;
        while (child->readLine(line)) {
            auto j = Json::parse(line, nullptr, false);
            if (j.is_discarded() || !j.is_object())
                continue;
            if (j.contains("pakfile") && j["pakfile"].contains("files") && j["pakfile"]["files"].is_array()) {
                for (const auto& f : j["pakfile"]["files"])
                    if (f.is_string())
                        overlay.add(f.get<std::string>());
            } else if (j.contains("error") && j["error"].is_object()) {
                error = makeError(j["error"].value("code", "import.failed"), j["error"].value("message", ""));
            }
        }
        child->wait();
        if (error)
            return *error;
        return overlay;
    }

    Result<Json> contentCheck(const Json& args) {
        GMDR_ASSIGN(auto s, session(args));
        {
            std::lock_guard lock(s->cacheMutex);
            if (s->contentCache)
                return *s->contentCache;
        }
        auto reader = s->reader();
        if (!reader || s->status().state != ImportState::Ready)
            return makeError("import.not_ready", "the content list is available after the import finishes");
        GMDR_ASSIGN(auto manifest, reader->manifest());
        GMDR_ASSIGN(auto v, gmodVfs(false));
        const std::string map = manifest.value("map", "");
        auto overlay = listPakfile(*v, map);
        if (!overlay)
            emit("log", {{"level", "warn"},
                         {"message", "cannot read the map's packed files: " + overlay.error().message}});
        auto report = assets::checkContent(manifest, *v, overlay ? &*overlay : nullptr);
        Json out = report.toJson();
        out["map"] = sanitizeUtf8(map);
        out["pakfileFiles"] = overlay ? overlay->files.size() : 0;
        std::lock_guard lock(s->cacheMutex);
        s->contentCache = out;
        return out;
    }

    // ---- registry --------------------------------------------------------------------------------------

    void registerCommands() {
        commands = {
            {"app.info", &Impl::appInfo},
            {"settings.get", &Impl::settingsGet},
            {"settings.set", &Impl::settingsSet},
            {"gmod.locate", &Impl::gmodLocate},
            {"cache.info", &Impl::cacheInfo},
            {"cache.clear", &Impl::cacheClear},
            {"demo.open", &Impl::demoOpen},
            {"demo.close", &Impl::demoClose},
            {"demo.list", &Impl::demoList},
            {"demo.info", &Impl::demoInfo},
            {"import.cancel", &Impl::importCancel},
            {"state.entities", &Impl::stateEntities},
            {"state.entity", &Impl::stateEntity},
            {"state.positions", &Impl::statePositions},
            {"entity.lifetime", &Impl::entityLifetime},
            {"timeline.summary", &Impl::timelineSummary},
            {"timeline.events", &Impl::timelineEvents},
            {"camera.track", &Impl::cameraTrack},
            {"content.check", &Impl::contentCheck},
        };
    }

    Result<Json> call(std::string_view cmd, const Json& args) {
        auto it = commands.find(cmd);
        if (it == commands.end())
            return makeError("api.unknown_command", "unknown command", std::string(cmd));
        if (!args.is_object())
            return badArgs("args must be an object");
        try {
            return (this->*(it->second))(args);
        } catch (const Json::exception& e) {
            return badArgs(e.what());
        } catch (const std::exception& e) {
            return makeError("api.internal", "internal error", e.what());
        }
    }
};

Engine::Engine(EngineConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

Engine::~Engine() = default;

Result<Json> Engine::call(std::string_view cmd, const Json& args) {
    return impl_->call(cmd, args);
}

std::string Engine::callJson(std::string_view request) {
    Json response;
    auto req = Json::parse(request, nullptr, false);
    if (req.is_discarded() || !req.is_object() || !req.contains("cmd") || !req["cmd"].is_string()) {
        response = {{"ok", false},
                    {"error", errorJson(makeError("api.bad_request", "request must be {cmd, args}"))}};
    } else {
        const Json args = req.contains("args") && !req["args"].is_null() ? req["args"] : Json::object();
        auto r = impl_->call(req["cmd"].get<std::string>(), args);
        if (r)
            response = {{"ok", true}, {"result", std::move(*r)}};
        else
            response = {{"ok", false}, {"error", errorJson(r.error())}};
    }
    return response.dump(-1, ' ', false, Json::error_handler_t::replace);
}

void Engine::setEventSink(EventSink sink) {
    std::lock_guard lock(impl_->sinkMutex);
    impl_->sink = std::move(sink);
}

} // namespace gmdr::api

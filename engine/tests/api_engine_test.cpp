#include "api/engine.h"
#include "api/gmdr.h"
#include "core/file.h"
#include "core/text.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <mutex>
#include <random>
#include <thread>

using namespace gmdr;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("gmdr_api_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeFile(const fs::path& p, const std::vector<std::uint8_t>& data) {
    fs::create_directories(p.parent_path());
    auto f = File::open(p, File::Mode::CreateTruncate);
    REQUIRE(f);
    REQUIRE(f->writeAt(0, data));
}

// The smallest valid demo: header + dem_stop.
std::vector<std::uint8_t> minimalDemo() {
    std::vector<std::uint8_t> d(1072, 0);
    std::memcpy(d.data(), "HL2DEMO", 7);
    const std::int32_t demoProtocol = 3, netProtocol = 24;
    std::memcpy(d.data() + 8, &demoProtocol, 4);
    std::memcpy(d.data() + 12, &netProtocol, 4);
    std::memcpy(d.data() + 16 + 520, "gm_test", 7); // map name
    d.push_back(7);                                 // dem_stop
    for (int i = 0; i < 4; ++i)
        d.push_back(0);
    return d;
}

api::EngineConfig testConfig(const TempDir& tmp) {
    api::EngineConfig c;
    c.cacheDir = tmp.path / "cache";
    c.configDir = tmp.path / "config";
#ifdef GMDR_TEST_IMPORTER
    c.importer = pathFromUtf8(GMDR_TEST_IMPORTER);
#endif
    return c;
}

struct EventLog {
    std::mutex m;
    std::condition_variable cv;
    std::vector<Json> events;
    void add(const std::string& json) {
        std::lock_guard lock(m);
        events.push_back(Json::parse(json));
        cv.notify_all();
    }
    bool waitFor(const std::string& type, std::chrono::seconds timeout = std::chrono::seconds(30)) {
        std::unique_lock lock(m);
        return cv.wait_for(lock, timeout, [&] {
            for (const auto& e : events)
                if (e.value("type", "") == type)
                    return true;
            return false;
        });
    }
};

} // namespace

TEST_CASE("command bus: request validation and errors") {
    TempDir tmp;
    api::Engine engine(testConfig(tmp));
    auto parse = [&](std::string_view req) { return Json::parse(engine.callJson(req)); };

    CHECK(parse("not json")["error"]["code"] == "api.bad_request");
    CHECK(parse("{\"args\":{}}")["error"]["code"] == "api.bad_request");
    CHECK(parse("{\"cmd\":\"nope\"}")["error"]["code"] == "api.unknown_command");
    CHECK(parse("{\"cmd\":\"demo.open\",\"args\":[]}")["error"]["code"] == "api.bad_args");
    CHECK(parse("{\"cmd\":\"demo.open\",\"args\":{\"path\":5}}")["error"]["code"] == "api.bad_args");
    CHECK(parse("{\"cmd\":\"demo.open\",\"args\":{\"path\":\"relative.dem\"}}")["error"]["code"] ==
          "demo.bad_path");
    const std::string missing = pathToUtf8(tmp.path / "missing.dem");
    CHECK(engine.call("demo.open", {{"path", missing}}).error().code == "demo.not_found");
    CHECK(engine.call("demo.info", {{"demo", "d99"}}).error().code == "demo.not_open");

    writeFile(tmp.path / "garbage.dem", std::vector<std::uint8_t>(2000, 0x41));
    auto bad = engine.call("demo.open", {{"path", pathToUtf8(tmp.path / "garbage.dem")}});
    REQUIRE_FALSE(bad);
    CHECK(startsWith(bad.error().code, "demo."));

    auto info = parse("{\"cmd\":\"app.info\"}");
    CHECK(info["ok"] == true);
    CHECK(info["result"]["formatVersion"] == 1);
}

TEST_CASE("settings: validation, persistence, UI-owned keys") {
    TempDir tmp;
    {
        api::Engine engine(testConfig(tmp));
        CHECK(engine.call("settings.set", {{"key", "gmod.path"}, {"value", 5}}).error().code ==
              "settings.bad_value");
        CHECK(engine.call("settings.set", {{"key", "bogus"}, {"value", 1}}).error().code ==
              "settings.unknown_key");
        REQUIRE(engine.call("settings.set", {{"key", "ui.theme"}, {"value", "light"}}));
        REQUIRE(engine.call("settings.set", {{"key", "cache.limitBytes"}, {"value", 1024}}));
        auto v = engine.call("settings.get", {{"key", "ui.theme"}});
        REQUIRE(v);
        CHECK((*v)["value"] == "light");
    }
    api::Engine again(testConfig(tmp));
    auto all = again.call("settings.get", Json::object());
    REQUIRE(all);
    CHECK((*all)["values"]["ui.theme"] == "light");
    CHECK((*all)["values"]["cache.limitBytes"] == 1024);
    REQUIRE(again.call("settings.set", {{"key", "ui.theme"}, {"value", nullptr}}));
    CHECK((*again.call("settings.get", {{"key", "ui.theme"}}))["value"].is_null());
}

#ifdef GMDR_TEST_IMPORTER
TEST_CASE("demo import through gmdr-import, cached reopen, close") {
    TempDir tmp;
    const fs::path demoPath = tmp.path / "tiny.dem";
    writeFile(demoPath, minimalDemo());
    std::string hash;
    {
        api::Engine engine(testConfig(tmp));
        EventLog log;
        engine.setEventSink([&](const std::string& json) { log.add(json); });
        auto open = engine.call("demo.open", {{"path", pathToUtf8(demoPath)}});
        REQUIRE(open);
        const std::string demo = (*open)["demo"];
        hash = (*open)["hash"];
        REQUIRE(log.waitFor("import.done"));
        CHECK(log.waitFor("import.indexed"));

        auto info = engine.call("demo.info", {{"demo", demo}});
        REQUIRE(info);
        CHECK((*info)["import"]["state"] == "ready");
        CHECK((*info)["header"]["map"] == "gm_test");
        // Opening the same file again returns the same session.
        auto same = engine.call("demo.open", {{"path", pathToUtf8(demoPath)}});
        REQUIRE(same);
        CHECK((*same)["demo"] == demo);
        auto list = engine.call("demo.list", Json::object());
        REQUIRE(list);
        CHECK((*list)["demos"].size() == 1);
        auto summary = engine.call("timeline.summary", {{"demo", demo}, {"bins", 4}});
        REQUIRE(summary);
        CHECK((*summary)["complete"] == true);
        auto recent = engine.call("settings.get", {{"key", "recent"}});
        REQUIRE(recent);
        CHECK((*recent)["value"].size() == 1);
        REQUIRE(engine.call("demo.close", {{"demo", demo}}));
        CHECK(engine.call("demo.info", {{"demo", demo}}).error().code == "demo.not_open");
    }
    CHECK(fs::is_regular_file(tmp.path / "cache" / "demos" / hash / "state.gmstate"));
    CHECK_FALSE(fs::exists(tmp.path / "cache" / "demos" / hash / "state.gmstate.part"));
    {
        api::Engine engine(testConfig(tmp));
        auto open = engine.call("demo.open", {{"path", pathToUtf8(demoPath)}});
        REQUIRE(open);
        CHECK((*open)["import"]["state"] == "ready");
        CHECK((*open)["import"]["cached"] == true);
        auto cache = engine.call("cache.info", Json::object());
        REQUIRE(cache);
        CHECK((*cache)["demos"] == 1);
        // An open demo survives cache.clear.
        auto cleared = engine.call("cache.clear", Json::object());
        REQUIRE(cleared);
        CHECK((*cleared)["removed"] == 0);
        REQUIRE(engine.call("demo.close", {{"demo", (*open)["demo"]}}));
        cleared = engine.call("cache.clear", Json::object());
        REQUIRE(cleared);
        CHECK((*cleared)["removed"] == 1);
    }
}
#endif

TEST_CASE("C ABI: create, call, events, destroy") {
    TempDir tmp;
    Json cfg = {{"cacheDir", pathToUtf8(tmp.path / "cache")}, {"configDir", pathToUtf8(tmp.path / "config")}};
    gmdr_engine* e = gmdr_create(cfg.dump().c_str());
    REQUIRE(e);
    int events = 0;
    gmdr_set_event_sink(e, [](void* user, const char*, size_t) { ++*static_cast<int*>(user); }, &events);
    char* out =
        gmdr_call(e, "{\"cmd\":\"settings.set\",\"args\":{\"key\":\"ui.density\",\"value\":\"compact\"}}");
    REQUIRE(out);
    CHECK(Json::parse(out)["ok"] == true);
    gmdr_free(out);
    CHECK(events == 1); // settings.changed
    out = gmdr_call(e, nullptr);
    CHECK(Json::parse(out)["ok"] == false);
    gmdr_free(out);
    gmdr_set_event_sink(e, nullptr, nullptr);
    gmdr_destroy(e);

    CHECK(gmdr_create("{not json") == nullptr);
    CHECK(gmdr_create("{\"cacheDir\": 5}") == nullptr);
    char* none = gmdr_call(nullptr, "{}");
    CHECK(Json::parse(none)["error"]["code"] == "api.no_engine");
    gmdr_free(none);
}

#include "api/engine.h"
#include "api/gmdr.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

struct gmdr_engine {
    std::unique_ptr<gmdr::api::Engine> engine;
};

namespace {

char* copyString(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out)
        return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

} // namespace

extern "C" {

gmdr_engine* gmdr_create(const char* config_json) {
    try {
        gmdr::Json overrides;
        if (config_json && *config_json) {
            overrides = gmdr::Json::parse(config_json, nullptr, false);
            if (overrides.is_discarded())
                return nullptr;
        }
        auto config = gmdr::api::makeConfig(overrides);
        if (!config)
            return nullptr;
        auto* e = new gmdr_engine;
        e->engine = std::make_unique<gmdr::api::Engine>(std::move(*config));
        return e;
    } catch (...) {
        return nullptr;
    }
}

void gmdr_destroy(gmdr_engine* engine) {
    delete engine;
}

char* gmdr_call(gmdr_engine* engine, const char* request_json) {
    static const char* kNoEngine =
        "{\"ok\":false,\"error\":{\"code\":\"api.no_engine\",\"message\":\"engine is null\"}}";
    static const char* kOom =
        "{\"ok\":false,\"error\":{\"code\":\"api.internal\",\"message\":\"out of memory\"}}";
    if (!engine || !engine->engine)
        return copyString(kNoEngine);
    try {
        char* out = copyString(engine->engine->callJson(request_json ? request_json : ""));
        return out ? out : copyString(kOom);
    } catch (...) {
        return copyString(kOom);
    }
}

void gmdr_free(char* str) {
    std::free(str);
}

void gmdr_set_event_sink(gmdr_engine* engine, gmdr_event_fn sink, void* user) {
    if (!engine || !engine->engine)
        return;
    if (!sink) {
        engine->engine->setEventSink(nullptr);
        return;
    }
    engine->engine->setEventSink(
        [sink, user](const std::string& json) { sink(user, json.data(), json.size()); });
}

} // extern "C"

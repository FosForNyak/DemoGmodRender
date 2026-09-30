/* DemoGmodRender engine: C ABI (ADR-003).
 *
 * The whole API is one JSON command bus:
 *   request  {"cmd": "demo.open", "args": {"path": "C:/demos/match.dem"}}
 *   response {"ok": true, "result": {...}}  or  {"ok": false, "error": {"code": "...", "message": "...",
 * "details": "..."}} Events (import progress, logs) arrive through the sink as {"type": "import.progress",
 * ...}. All strings are UTF-8. Every function is thread-safe; the sink may be called from engine threads and
 * must not block or call back into the engine.
 */
#ifndef GMDR_H
#define GMDR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gmdr_engine gmdr_engine;
typedef void (*gmdr_event_fn)(void* user, const char* json, size_t len);

/* config_json: optional overrides {"cacheDir", "configDir", "importer"}; NULL or "" for defaults.
 * Returns NULL only if the configuration is invalid. */
gmdr_engine* gmdr_create(const char* config_json);
void gmdr_destroy(gmdr_engine* engine);

/* Runs one command. The result is never NULL and must be released with gmdr_free. */
char* gmdr_call(gmdr_engine* engine, const char* request_json);
void gmdr_free(char* str);

/* Replaces the event sink (NULL to remove). */
void gmdr_set_event_sink(gmdr_engine* engine, gmdr_event_fn sink, void* user);

#ifdef __cplusplus
}
#endif

#endif /* GMDR_H */

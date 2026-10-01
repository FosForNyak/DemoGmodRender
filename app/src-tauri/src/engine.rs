//! Safe wrapper over the engine's C ABI (engine/api/gmdr.h, ADR-003): one JSON call and an event sink.

use std::ffi::{c_char, c_void, CStr, CString};
use std::sync::mpsc::Sender;
use std::sync::Arc;

#[repr(C)]
struct GmdrEngine {
    _private: [u8; 0],
}

type EventFn = extern "C" fn(user: *mut c_void, json: *const c_char, len: usize);

extern "C" {
    fn gmdr_create(config_json: *const c_char) -> *mut GmdrEngine;
    fn gmdr_destroy(engine: *mut GmdrEngine);
    fn gmdr_call(engine: *mut GmdrEngine, request_json: *const c_char) -> *mut c_char;
    fn gmdr_free(s: *mut c_char);
    fn gmdr_set_event_sink(engine: *mut GmdrEngine, sink: Option<EventFn>, user: *mut c_void);
}

struct Inner {
    ptr: *mut GmdrEngine,
    sink: *mut Sender<String>,
}

// The C API is thread-safe (documented in gmdr.h); the sender is only used from the sink callback.
unsafe impl Send for Inner {}
unsafe impl Sync for Inner {}

impl Drop for Inner {
    fn drop(&mut self) {
        unsafe {
            gmdr_set_event_sink(self.ptr, None, std::ptr::null_mut());
            gmdr_destroy(self.ptr);
            drop(Box::from_raw(self.sink));
        }
    }
}

extern "C" fn on_event(user: *mut c_void, json: *const c_char, len: usize) {
    // Must not block: an unbounded channel send never does.
    let sender = unsafe { &*(user as *const Sender<String>) };
    let bytes = unsafe { std::slice::from_raw_parts(json as *const u8, len) };
    let _ = sender.send(String::from_utf8_lossy(bytes).into_owned());
}

#[derive(Clone)]
pub struct Engine(Arc<Inner>);

impl Engine {
    /// Creates the engine with default paths; events go to `events`.
    pub fn new(events: Sender<String>) -> Result<Self, String> {
        let ptr = unsafe { gmdr_create(std::ptr::null()) };
        if ptr.is_null() {
            return Err("the engine could not start".into());
        }
        let sink = Box::into_raw(Box::new(events));
        unsafe { gmdr_set_event_sink(ptr, Some(on_event), sink as *mut c_void) };
        Ok(Engine(Arc::new(Inner { ptr, sink })))
    }

    /// Runs one command: `{"cmd", "args"}` -> `{"ok", "result" | "error"}`.
    pub fn call(&self, request: &str) -> String {
        let request = match CString::new(request) {
            Ok(r) => r,
            Err(_) => {
                return r#"{"ok":false,"error":{"code":"api.bad_request","message":"request contains a NUL byte"}}"#
                    .into()
            }
        };
        unsafe {
            let out = gmdr_call(self.0.ptr, request.as_ptr());
            let text = CStr::from_ptr(out).to_string_lossy().into_owned();
            gmdr_free(out);
            text
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Engine;
    use serde_json::Value;
    use std::time::Duration;

    fn parse(s: &str) -> Value {
        serde_json::from_str(s).expect("engine returns JSON")
    }

    #[test]
    fn calls_errors_events_and_threads() {
        // Own folders, so the test never touches the user's settings or cache.
        let dir = std::env::temp_dir().join(format!("gmdr-rust-ffi-{}", std::process::id()));
        std::env::set_var("GMDR_CACHE_DIR", dir.join("cache"));
        std::env::set_var("GMDR_CONFIG_DIR", dir.join("config"));

        let (tx, rx) = std::sync::mpsc::channel();
        let engine = Engine::new(tx).expect("engine starts");

        let info = parse(&engine.call(r#"{"cmd":"app.info"}"#));
        assert_eq!(info["ok"], true);
        assert_eq!(info["result"]["formatVersion"], 1);

        assert_eq!(parse(&engine.call("not json"))["error"]["code"], "api.bad_request");
        assert_eq!(parse(&engine.call("{\"cmd\":\"app.info\u{0}\"}"))["error"]["code"], "api.bad_request");
        assert_eq!(parse(&engine.call(r#"{"cmd":"nope"}"#))["error"]["code"], "api.unknown_command");

        // Events cross the C callback into the channel.
        let set = parse(&engine.call(r#"{"cmd":"settings.set","args":{"key":"ui.theme","value":"light"}}"#));
        assert_eq!(set["ok"], true);
        let event = parse(&rx.recv_timeout(Duration::from_secs(5)).expect("an event"));
        assert_eq!(event["type"], "settings.changed");

        // The engine is shared across threads (Tauri runs commands on a pool).
        let handles: Vec<_> = (0..4)
            .map(|_| {
                let e = engine.clone();
                std::thread::spawn(move || parse(&e.call(r#"{"cmd":"settings.get"}"#))["ok"] == true)
            })
            .collect();
        assert!(handles.into_iter().all(|h| h.join().unwrap()));

        drop(engine);
        let _ = std::fs::remove_dir_all(dir);
    }
}

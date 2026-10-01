// DemoGmodRender: Tauri shell around the C++ engine (ADR-001). The UI talks to the engine only through
// `engine_call` (the JSON command bus, ADR-003) and receives its events as the Tauri event "engine".
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod engine;

use tauri::{Emitter, Manager};

#[tauri::command]
async fn engine_call(state: tauri::State<'_, engine::Engine>, request: String) -> Result<String, String> {
    let engine = state.inner().clone();
    // Commands may take a while (opening a demo hashes it): keep them off the UI thread.
    tauri::async_runtime::spawn_blocking(move || engine.call(&request))
        .await
        .map_err(|e| e.to_string())
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            let (tx, rx) = std::sync::mpsc::channel::<String>();
            let engine = engine::Engine::new(tx)?;
            app.manage(engine);
            let handle = app.handle().clone();
            std::thread::Builder::new()
                .name("engine-events".into())
                .spawn(move || {
                    for event in rx {
                        let _ = handle.emit("engine", event);
                    }
                })?;
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![engine_call])
        .run(tauri::generate_context!())
        .expect("error while running DemoGmodRender");
}

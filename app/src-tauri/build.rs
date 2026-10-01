// Builds the C++ engine (scripts/build-engine.cmd), links its static libraries and ships gmdr-import as a
// sidecar next to the app executable.
use std::path::{Path, PathBuf};
use std::process::Command;

const PRESET: &str = "windows-msvc-release";
const TRIPLE: &str = "x86_64-pc-windows-msvc";

fn main() {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    // Not canonicalize(): on Windows it yields \\?\ paths that cmd.exe cannot run scripts from.
    let repo = manifest.parent().and_then(Path::parent).expect("repository root").to_path_buf();
    let engine = repo.join("engine");
    let build = engine.join("build").join(PRESET);

    for dir in ["core", "demo", "assets", "api", "tools", "CMakeLists.txt", "CMakePresets.json", "vcpkg.json"] {
        println!("cargo:rerun-if-changed={}", engine.join(dir).display());
    }
    println!("cargo:rerun-if-env-changed=GMDR_SKIP_ENGINE_BUILD");

    if std::env::var_os("GMDR_SKIP_ENGINE_BUILD").is_none() {
        let status = Command::new("cmd")
            .arg("/c")
            .arg(repo.join("scripts").join("build-engine.cmd"))
            .args([PRESET, "gmdr_api", "gmdr-import"])
            .status()
            .expect("run scripts/build-engine.cmd");
        assert!(status.success(), "engine build failed: run scripts\\build-engine.cmd to see the errors");
    }

    for lib_dir in ["api", "assets", "demo", "core"] {
        println!("cargo:rustc-link-search=native={}", build.join(lib_dir).display());
    }
    println!(
        "cargo:rustc-link-search=native={}",
        build.join("vcpkg_installed").join("x64-windows-static-md").join("lib").display()
    );
    for lib in ["gmdr_api", "gmdr_assets", "gmdr_demo", "gmdr_core", "zstd", "blake3", "xxhash"] {
        println!("cargo:rustc-link-lib=static={lib}");
    }
    println!("cargo:rustc-link-lib=dylib=advapi32");
    println!("cargo:rustc-link-lib=dylib=userenv"); // AppContainer profiles for the importer

    // Tauri expects sidecars as binaries/<name>-<target triple>.exe.
    let sidecar_dir = manifest.join("binaries");
    std::fs::create_dir_all(&sidecar_dir).expect("create binaries/");
    copy_if_changed(
        &build.join("tools").join("gmdr-import").join("gmdr-import.exe"),
        &sidecar_dir.join(format!("gmdr-import-{TRIPLE}.exe")),
    );

    tauri_build::build();
}

fn copy_if_changed(from: &Path, to: &Path) {
    let same = std::fs::read(from).ok().zip(std::fs::read(to).ok()).map_or(false, |(a, b)| a == b);
    if !same {
        std::fs::copy(from, to).unwrap_or_else(|e| panic!("copy {} -> {}: {e}", from.display(), to.display()));
    }
}

# Staff Engineer — index

Standards for the whole repo:
- **C++:** C++20, `-Wall -Wextra -Werror` (GCC/Clang), `/W4 /WX /permissive-` (MSVC); no exceptions across the C ABI; `Result<T>` for recoverable errors; no raw `new`/`delete`; no `reinterpret_cast` over file bytes; every size from a file is checked before allocation.
- **Naming:** `snake_case` files, `PascalCase` types, `camelCase` functions/variables, `kConstant` constants, `m_` not used; namespaces `gmdr::core`, `gmdr::demo`, `gmdr::assets`, `gmdr::api`.
- **Formatting:** clang-format (LLVM base, 4 spaces, 110 cols); rustfmt; Prettier for TS.
- **Tests:** doctest next to modules in `engine/tests`; every parser gets a fuzz target.
- **Errors:** dotted codes (`demo.bad_header`), human message in Ukrainian for UI-visible errors, details in English for logs.
- **Logging:** structured lines `{level, module, msg, fields}`; no personal data (player names, SteamIDs) at info level.
- **Dependencies:** vcpkg manifest with pinned baseline; licenses MIT/BSD/Apache/zlib/CC0 only.

## Features
| Slug | File |
| --- | --- |
| app-shell-demo-import | [app-shell-demo-import.md](app-shell-demo-import.md) |

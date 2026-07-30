# AGENTS.md — AzerothCore WotLK

> **Repository:** `https://github.com/bendingmoon/azerothcore-wotlk.git` (fork of `mod-playerbots/azerothcore-wotlk`, branch `Playerbot`)
> AzerothCore: WoW 3.3.5a server emulator. C++20, CMake (out-of-source only), MySQL 8.4.

This fork diverges from upstream `master` (custom equipment-upgrade, quest-escort features). When in doubt, follow upstream AzerothCore conventions and the style of the file being edited.

## Related client project

Unity client at `D:\Unity\clientproj` (C# hot-update + tolua/Lua, connects via WoW Auth/World protocol).

| Item | Path |
|------|------|
| C# hot-update code | `D:\Unity\clientproj\Assets\HotUpdate\MoonClient` |
| Lua / tolua UI code | `D:\Unity\clientproj\Assets\Scripts\Lua` |
| Client AGENTS.md | `D:\Unity\clientproj\AGENTS.md` |

## Code organization

- Build is **CMake auto-collected**: most dirs under `src/server/game/` and `src/server/scripts/` have no own `CMakeLists.txt`; sources are globbed recursively (`src/cmake/macros/AutoCollect.cmake`). Adding a file is often enough.
- Key dirs: `src/server/game/` (core simulation: `Entities/`, `Spells/`, `Maps/`, `AI/`, `Movement/`, `Handlers/`, `Scripting/`, `Server/`), `src/server/scripts/` (content scripts), `src/common/` (Asio, crypto, config, logging), `modules/` (custom modules), `data/sql/` (DB schemas/updates), `apps/` (dev/CI helpers, not compiled).
- Executables: `authserver` (port 3724), `worldserver` (ports 8085 / 7878).
- Windows: pre-generated VS2022 solution at `build/AzerothCore.sln`.

## Scripting conventions

- Scripts inherit typed bases (`CreatureScript`, `SpellScriptLoader`, `GameObjectScript`, `CommandScript`, …), register via macros like `RegisterSpellScript(...)`.
- Each script file exposes an `AddSC_*()` function that must be called from the appropriate regional loader (e.g. `spells_script_loader.cpp`).
- Naming: classes `PascalCase`; `instance_<name>.cpp`, `boss_<name>.cpp`, `spell_<class>.cpp`, `cs_<name>.cpp`; managers are `*Mgr` singletons.

## Module system

Modules are auto-discovered from `modules/` (any subdir with `src/`). Built static by default; disable with `-DDISABLED_AC_MODULES="mod1;mod2"`. Loader: `modules/ModulesLoader.cpp.in.cmake` → `AddModulesScripts()` registered via `sScriptMgr->SetModulesLoader(...)`.

Current modules: `mod-playerbots` (defines `MOD_PLAYERBOTS`), `mod-item-upgrade`, `StatBooster`.

## Databases

`acore_auth`, `acore_characters`, `acore_world` (+ `acore_playerbots` in this fork). Base schemas in `data/sql/base/db_*/`; updates in `data/sql/updates/db_*/`; new changes go in `data/sql/updates/pending_db_*/` via `create_sql.sh`.

SQL style: backticks on identifiers; `DELETE` before `INSERT`; no `REPLACE INTO`; end with `;`; InnoDB; avoid deleting from `*_template` tables.

## Code style

- C++: 4-space indent, no tabs, UTF-8, LF, max 80 cols. No braces around single-line statements. `auto const&` over `const auto&`; `Type const*`; `{}` fmt placeholders over `%u`; prefer helpers like `IsPlayer()`, `HasNpcFlag()`.
- Style checkers: `python apps/codestyle/codestyle-cpp.py`, `python apps/codestyle/codestyle-sql.py`.
- Commits: Conventional Commits (`Type(Scope): desc`, max 50 chars; types `feat/fix/refactor/style/docs/test/chore`; scopes `CORE`, `DB`). Local branch has legacy Chinese commits — use the documented format for new work.

## Agent notes

- Do NOT run any compilation or build (full or incremental) unless the user explicitly demands it.

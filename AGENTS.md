# AGENTS.md — AzerothCore WotLK

This file contains practical, project-specific guidance for AI coding agents working in this repository. It is based on the actual files in the tree; make sure to cross-check any facts against the current source before acting on them.

> **Repository:** `https://github.com/bendingmoon/azerothcore-wotlk.git`  
> **Upstream:** `https://github.com/mod-playerbots/azerothcore-wotlk.git`  
> **Current branch:** `Playerbot`  
> **Project metadata:** `acore.json` → name `azerothcore-wotlk`, version `16.0.0-dev`, license `GPL2`

---

## Related client project

This server is developed alongside a Unity client located at `D:\Unity\clientproj`.
The client uses a hybrid C# hot-update + tolua/Lua architecture and connects to this AzerothCore-based server via the WoW Auth/World protocol.
See `D:\Unity\clientproj\AGENTS.md` for the client-side project map.

| Item | Path |
|------|------|
| Unity project root | `D:\Unity\clientproj` |
| C# hot-update code | `D:\Unity\clientproj\Assets\HotUpdate\MoonClient` |
| Lua / tolua UI code | `D:\Unity\clientproj\Assets\Scripts\Lua` |
| Client AGENTS.md | `D:\Unity\clientproj\AGENTS.md` |

---

## Project overview

AzerothCore is an open-source MMORPG server emulator for *World of Warcraft: Wrath of the Lich King* (patch 3.3.5a). It is written primarily in **C++20** and uses **CMake** as its build system and **MySQL** for persistence. The code is descended from MaNGOS/TrinityCore/SunwellCore and is licensed under GPL v2 (older portions) and AGPL v3 (newer components). See `.github/README.md` and `.github/CONTRIBUTING.md` for the human-facing project introduction.

This particular checkout is a fork that tracks the `mod-playerbots/azerothcore-wotlk` upstream on the `Playerbot` branch. Recent local commits add custom equipment-upgrade and quest-escort features, so changes here may diverge from upstream `master`.

---

## Technology stack

| Layer | Technology |
|-------|------------|
| Languages | C++20, C, Bash, SQL, Python (tooling) |
| Build system | CMake 3.16–3.22 |
| Database | MySQL 8.4 (three logical DBs: `acore_auth`, `acore_characters`, `acore_world`; this fork also uses `acore_playerbots`) |
| Networking | Boost.Asio |
| Crypto | OpenSSL, Argon2 |
| Geometry/pathfinding | g3dlite, Recast/Detour |
| Formatting | fmt |
| Unit tests | Google Test / Google Mock |
| Container runtime | Docker + Docker Compose |

Major bundled dependencies live under `deps/` (boost, mysql, openssl, zlib, recastnavigation, g3dlite, fmt, argon2, jemalloc, gperftools, gsoap, libmpq, bzip2, fkYAML, readline, SFMT, utf8cpp, etc.).

---

## Build and test commands

CMake **requires an out-of-source build**. In-source builds are blocked at the root `CMakeLists.txt`.

### Standard Linux/macOS configure & build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/azeroth-server \
         -DCMAKE_BUILD_TYPE=RelWithDebInfo \
         -DSCRIPTS=static -DMODULES=static
make -j$(nproc)
make install
```

### Using the project dashboard (recommended for routine work)

```bash
# Interactive menu: init, install-deps, compiler, module, setup-db, run-worldserver, etc.
./acore.sh

# Or directly invoke the compiler helper
./bin/acore-compiler build
```

### Key CMake options

| Option | Values | Default | Meaning |
|--------|--------|---------|---------|
| `SCRIPTS` | none / static / dynamic / minimal-static / minimal-dynamic | `static` | How core scripts are built |
| `MODULES` | none / static / dynamic | `static` | How `modules/` are built |
| `APPS_BUILD` | none / all / auth-only / world-only | `all` | Which server apps to build |
| `TOOLS_BUILD` | none / all / db-only / maps-only | `none` | Data extraction/tools |
| `BUILD_TESTING` | ON / OFF | OFF | Build unit tests |
| `USE_COREPCH` / `USE_SCRIPTPCH` | ON / OFF | ON | Precompiled headers |
| `WITH_WARNINGS` | ON / OFF | OFF | Show all warnings |
| `WITH_COREDEBUG` | ON / OFF | OFF | Extra debug code |
| `WITH_DYNAMIC_LINKING` | ON / OFF | OFF | Experimental shared-library mode |

Defaults are declared in `conf/dist/config.cmake`. You can override them in `conf/config.cmake` (created from `conf/dist/config.cmake` if needed).

### Windows

The repository already contains a generated Visual Studio 2022 solution at `build/AzerothCore.sln`. The CI workflow builds on Windows with `acore.sh install-deps` followed by `acore.sh compiler build`. Minimum MSVC version is 19.24 (VS 2019 16.4).

### Unit tests

```bash
cd build
cmake .. -DBUILD_TESTING=ON
cmake --build . --config Release -j$(nproc)

# Run directly
./src/test/unit_tests

# Or via CTest
ctest
```

Tests live in `src/test/`, link against the `game` library (and optionally `modules`), and use Google Test/Mock. Coverage reporting is available via a `coverage` target when `BUILD_TESTING` is enabled.

---

## Runtime architecture

The project produces two primary server executables:

| Executable | Sources | Purpose | Default port |
|------------|---------|---------|--------------|
| `authserver` | `src/server/apps/authserver/` | Account authentication, SRP6, realm list, bans | 3724 |
| `worldserver` | `src/server/apps/worldserver/` | Game world simulation, networking, scripting | 8085 (world), 7878 (SOAP/RA) |

Optional tools built with `TOOLS_BUILD` include:

| Tool | Path |
|------|------|
| `dbimport` | `src/tools/dbimport/` |
| `map_extractor` | `src/tools/map_extractor/` |
| `vmap4_extractor` | `src/tools/vmap4_extractor/` |
| `vmap4_assembler` | `src/tools/vmap4_assembler/` |
| `mmaps_generator` | `src/tools/mmaps_generator/` |

---

## Code organization

The build is **CMake-driven and auto-collected**: most subdirectories under `src/server/game/` and `src/server/scripts/` do not contain their own `CMakeLists.txt`; parent CMake files glob all relevant sources recursively via `src/cmake/macros/AutoCollect.cmake`.

### Top-level directories

| Path | Contents |
|------|----------|
| `src/cmake/` | CMake macros, compiler settings, platform checks |
| `src/common/` | Low-level shared code: Asio wrappers, crypto, config, logging, collision, threading, utilities |
| `src/server/apps/` | Executable entry points (`authserver`, `worldserver`) |
| `src/server/database/` | Database connection pool, prepared statements, updater/migration system |
| `src/server/shared/` | Server-layer shared code: packets, network, realms, secrets |
| `src/server/game/` | Core world simulation (~50+ subsystems) |
| `src/server/scripts/` | Blizzlike content scripts: instances, spells, commands, outdoor PvP, events, world bosses |
| `src/test/` | Google Test unit tests and mocks |
| `src/tools/` | Standalone extraction and import tools |
| `modules/` | Third-party / custom modules |
| `apps/` | Development, CI, and deployment helpers (not compiled into the server) |
| `data/sql/` | Database schemas, updates, pending changes, and custom SQL |
| `deps/` | Bundled third-party dependencies |

### CMake target layering

```
common
  → database
      → shared
          → game-interface
                → game, scripts, modules
                      → worldserver
                      → authserver
```

### Key game subsystems (`src/server/game/`)

| Directory | Responsibility |
|-----------|----------------|
| `Entities/` | `Player`, `Creature`, `Unit`, `Item`, `GameObject`, `Pet`, `Vehicle`, etc. |
| `Spells/` | Spell mechanics, auras, effects |
| `Maps/` | Map/grid/cell management, instancing |
| `AI/` | CoreAI, ScriptedAI, SmartScripts |
| `Movement/` | Movement generators, splines, waypoints |
| `Handlers/` | Client packet handlers (methods on `WorldSession`) |
| `Scripting/` | `ScriptMgr`, script registries, `ScriptObject` base classes |
| `Server/` | `WorldSession`, `World`, opcodes |
| `Battlegrounds/` / `Battlefield/` / `OutdoorPvP/` | PvP systems |
| `Chat/` / `Groups/` / `Guilds/` / `Instances/` | Social/instancing systems |
| `Globals/` | Global object managers |

### Scripting conventions

Scripts inherit typed base classes (`CreatureScript`, `SpellScriptLoader`, `InstanceMapScript`, `GameObjectScript`, `CommandScript`, etc.) and register via helper macros like `RegisterSpellScript(...)`. Each script file usually exposes an `AddSC_*()` function that is called from regional script loaders (e.g., `spells_script_loader.cpp`, `eastern_kingdoms_script_loader.cpp`).

### Naming conventions

- Classes: `PascalCase` (`WorldSession`, `CreatureAI`).
- Instance files: `instance_<name>.cpp`, `boss_<name>.cpp`.
- Spell files: `spell_<class>.cpp` with internal structs like `spell_mage_arcane_missiles`.
- Commands: `cs_<name>.cpp` (e.g., `cs_reload.cpp`).
- Managers: `*Mgr` singletons (`ObjectMgr`, `MapMgr`).
- Indentation: 4 spaces for C++, 2 spaces for JSON/YAML/shell.
- Encoding: UTF-8; line endings: LF.

---

## Module system

Modules are auto-discovered from the `modules/` directory. Any subdirectory containing a `src/` folder is considered a module. Disable specific modules with `-DDISABLED_AC_MODULES="mod1;mod2"`.

Build modes mirror core scripts:

- `static` → compiled into the `modules` static library (default).
- `dynamic` → compiled into `mod_<name>.dll` / `mod_<name>.so`.
- `disabled` → skipped.

The generated loader (`modules/ModulesLoader.cpp.in.cmake`) produces `AddModulesScripts()`, which `worldserver/Main.cpp` registers with `ScriptMgr` via:

```cpp
sScriptMgr->SetModulesLoader(AddModulesScripts);
```

This fork currently contains at least:

- `modules/mod-playerbots/` — enables the `MOD_PLAYERBOTS` compile definition.
- `modules/mod-item-upgrade/` — custom equipment-upgrade module (active in recent commits).
- `modules/StatBooster/` — stat-boosting module.

Module configs (`.conf.dist`) are copied into the build config directory by CMake (`CopyModuleConfig`).

---

## Database and SQL workflow

### Logical databases

| Database | Purpose | Base schema location |
|----------|---------|----------------------|
| `acore_auth` | Accounts, realms, bans | `data/sql/base/db_auth/` |
| `acore_characters` | Character data, inventories, progress | `data/sql/base/db_characters/` |
| `acore_world` | Game content: creatures, items, quests, spells, loot | `data/sql/base/db_world/` |
| `acore_playerbots` | Playerbot module data (this fork) | module-provided |

### SQL directory layout

- `data/sql/base/db_*/` — baseline schemas.
- `data/sql/updates/db_*/` — merged updates named `YYYY_MM_DD_INDEX.sql`.
- `data/sql/updates/pending_db_*/` — pending changes under review.
- `data/sql/custom/` — re-applicable custom SQL.
- `data/sql/archive/` — old updates.

### Contribution workflow

1. Create a pending SQL file with `data/sql/updates/pending_db_*/create_sql.sh` (generates `rev_<timestamp>.sql`).
2. Do not edit files outside the pending folders once a PR is open.
3. SQL style is enforced by `apps/codestyle/codestyle-sql.py` in CI.
4. After merge to `master`/`test-staging`, `.github/workflows/import_pending.yml` moves pending files to `data/sql/updates/db_*` and commits them.

### SQL style rules

- Use backticks around identifiers.
- `DELETE` must precede `INSERT`.
- No `REPLACE INTO`.
- No tabs, no double semicolons, no trailing blank lines.
- Statements must end with a semicolon.
- Engine must be InnoDB.
- Avoid deleting from `creature_template`, `gameobject_template`, `item_template`, or `quest_template`.

---

## Code style guidelines

There is **no root `.clang-format`** for the core. Style is enforced by custom Python scripts and CI.

- C++: 4-space indentation, no tabs, UTF-8, LF, max 80 characters per line.
- JSON/YAML/shell: 2-space indentation.
- No braces around single-line statements.
- Prefer `auto const&` over `const auto&`.
- Use `Type const*` syntax for const pointers.
- Use `{}` placeholders for formatting instead of `%u` style specifiers.
- Prefer helper methods such as `IsItem()`, `IsCreature()`, `IsPlayer()`, `GetNpcFlags()`, `HasNpcFlag()`.

### Running style checks locally

```bash
# C++
python apps/codestyle/codestyle-cpp.py

# SQL
python apps/codestyle/codestyle-sql.py

# cppcheck (used in CI)
cppcheck --force --inline-suppr --suppressions-list=./.suppress.cppcheck src/ --output-file=report.txt
```

### Commit message format

The project documents Conventional Commits in `.git_commit_template.txt`:

```
Type(Scope/Subscope): Short description (max 50 chars)
```

- Types: `feat`, `fix`, `refactor`, `style`, `docs`, `test`, `chore`.
- Scopes: `CORE` / `Core` (C++), `DB` (SQL).
- Body lines should be max 72 characters.

**Note:** The local `Playerbot` branch currently contains Chinese commits that do not follow this template. Prefer the documented format for new work.

---

## Testing instructions

1. Configure with `-DBUILD_TESTING=ON`.
2. Build the project.
3. Run `./src/test/unit_tests` or `ctest` from the build directory.

The CI reusable action `.github/actions/linux-build/action.yml` also performs dry-starts of `authserver` and `worldserver`, checks `Errors.log`, and runs the unit tests with warnings treated as errors.

---

## CI/CD and automation

Workflows live in `.github/workflows/`:

| Workflow | Purpose |
|----------|---------|
| `core-build.yml` | Ubuntu builds on `Playerbot` / `test-staging` |
| `core-build-playerbots.yml` | Same, plus `mod-playerbots/mod-playerbots` checkout |
| `core-build-pch.yml` / `core-build-nopch.yml` | PCH on/off builds |
| `core_modules_build.yml` | Module compilation tests |
| `windows_build.yml` | Windows build |
| `macos_build.yml` | macOS build |
| `tools_build.yml` | Tools/extractor builds |
| `docker_build.yml` | Docker image build/push |
| `dashboard-ci.yml` | Bash tests, build, module install, service manager tests |
| `codestyle.yml` | C++ style + cppcheck |
| `sql-codestyle.yml` | SQL style checks |
| `import_pending.yml` | Imports pending SQL on push to `master`/`test-staging` |

**Fork-specific caveat:** Some workflows (`codestyle.yml`, `sql-codestyle.yml`, `core-build-pch.yml`, `core-build-nopch.yml`, `core_modules_build.yml`) are gated to run only on specific upstream repositories (`mod-playerbots/azerothcore-wotlk` or `azerothcore/azerothcore-wotlk`). Because the active remote is `bendingmoon/azerothcore-wotlk`, those workflows may not trigger here. `core-build.yml`, `core-build-playerbots.yml`, `windows_build.yml`, `macos_build.yml`, `tools_build.yml`, `docker_build.yml`, and `dashboard-ci.yml` are not guarded this way and will run on `Playerbot` / `test-staging`.

---

## Docker and deployment

- `apps/docker/Dockerfile` — multi-stage production Dockerfile (Ubuntu 22.04, Clang, ccache).
- `apps/docker/Dockerfile.dev-server` — development/devcontainer image.
- `docker-compose.yml` — orchestrates MySQL, `ac-db-import`, `ac-worldserver`, `ac-authserver`, `ac-client-data-init`, `ac-tools`, and `ac-dev-server`.
- `apps/docker/entrypoint.sh` — runtime entrypoint that copies configs and starts the selected `ACORE_COMPONENT`.
- `.devcontainer/` — VS Code dev container configuration using the `ac-dev-server` service.

Typical Docker usage:

```bash
docker compose build
docker compose up -d
```

Create an admin account inside the running worldserver container after first start.

---

## Security considerations

- Supported versions: upstream only supports the official `master` branch; Playerbot/NPCBot forks and repacks are explicitly unsupported.
- Do not use precompiled repacks in production; compile from source to know what is running.
- Report vulnerabilities (exploits, hacks, performance attacks) via GitHub bug reports.
- PRs must disclose AI tool usage.
- SQL changes should be properly escaped/parameterized where applicable.
- Avoid hardcoded credentials, buffer overflows, and memory leaks.
- `.github/SECURITY.md` and `.github/agents/pr-reviewer.md` contain the full security and review checklist.

---

## Useful reference files

| Topic | File(s) |
|-------|---------|
| Project README | `.github/README.md` |
| Agent guidance (this repo) | `CLAUDE.md` |
| CMake defaults | `conf/dist/config.cmake` |
| Dashboard / build helper | `acore.sh`, `apps/installer/main.sh`, `apps/compiler/compiler.sh` |
| Docker docs | `apps/docker/README.md` |
| Module how-to | `modules/how_to_make_a_module.md` |
| Security policy | `.github/SECURITY.md` |
| PR template | `pull_request_template.md` |
| Config policy | `doc/ConfigPolicy.md` |
| Logging reference | `doc/Logging.md` |
| C++ style checker | `apps/codestyle/codestyle-cpp.py` |
| SQL style checker | `apps/codestyle/codestyle-sql.py` |

---

## Agent notes

- Always verify file paths against the current tree; the project uses recursive source collection and generated loaders, so adding a new file is often enough — but registration may require an `AddSC_*()` call in the appropriate loader.
- This is a fork; upstream `master` conventions may not perfectly match local `Playerbot` conventions. When in doubt, follow the documented AzerothCore conventions and the existing code style in the file you are editing.
- Do not run full builds unless explicitly requested; prefer targeted compilation or the existing `build/` artifacts when possible.

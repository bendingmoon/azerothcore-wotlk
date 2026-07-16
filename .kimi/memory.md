# Kimi Memory — AzerothCore WotLK Project

This file is the curated index for Kimi Code CLI. Detailed feature memories live in sibling files under `.kimi/`.

## Project Pairing

- **Server repo**: `D:\UnityWow\azerothcore\azerothcore-wotlk` (this repo)
- **Client repo**: `D:\Unity\clientproj` (Unity + tolua/Lua hybrid)
- **Current branch**: `Playerbot`
- **Key module**: `modules/mod-playerbots/`

## Memory Files

| File | Topic |
|------|-------|
| [auto-pilot.md](auto-pilot.md) | Real-player quest automation (auto-pilot) |
| [afk-grind.md](afk-grind.md) | In-place mob farming (AFK grind) |
| [other-memories.md](other-memories.md) | Index of original Claude Code memories |

## Memory Management Rule

When a new feature or large topic needs to be remembered:

1. Create a new sibling file: `.kimi/<feature>.md`.
2. Add one row to the **Memory Files** table above.
3. Do **not** dump detailed feature content directly into `memory.md`.

This keeps `memory.md` bounded and readable while allowing feature notes to grow independently.

## Remember When Answering

- Always verify paths against the current tree before acting.
- This is a Playerbot fork; upstream `master` conventions may differ.
- Prefer minimal changes and follow existing code style.
- Server-side changes usually need corresponding Unity C# + Lua changes for mobile features.
- Do not run full builds unless explicitly requested.

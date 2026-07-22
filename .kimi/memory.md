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
| [mod-item-upgrade-review-fixes.md](mod-item-upgrade-review-fixes.md) | Item upgrade 全链路审查与修复（2026-07-18，高危7项+中危+C#，Lua 留用户） |
| [equip-reforge.md](equip-reforge.md) | 装备洗练（StatBooster / CMSG_BOOST_ITEM）全链路：四种模式、物品字段布局、配置表、已知疑点（2026-07-21） |
| [lfg-proposal-veto-fix.md](lfg-proposal-veto-fix.md) | LFG 匹配被 OnPlayerbotCheckLFGQueue 否决（真人带 botAI 误判）+ 替补 bot 等级死循环修复（2026-07-20） |
| [lfg-bot-whisper-teleport-fix.md](lfg-bot-whisper-teleport-fix.md) | 禁止 bot 向真人发悄悄话 + LFG 等待匹配期间禁止随机传送（2026-07-21） |
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

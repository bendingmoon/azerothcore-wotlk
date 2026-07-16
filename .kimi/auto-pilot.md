# Feature: Auto-Pilot (Real-Player Quest Automation)

> Original memory: `auto-pilot-feature.md`

Real players can temporarily hand control to bot AI for automated quest completion (and future dungeon play). AI simulates real human gameplay — travel, kill mobs, loot, return to quest giver. **Not cheat-completion.** Auto-cancels on quest completion or manual movement.

## Core Design

- **Identity split**: `IsRealPlayer()` vs `IsAIEnabled()`
  - `IsRealPlayer()`: true when `isAutoPilot` OR `master==bot` OR session is not bot session. Used by LFG, social, logout, `IsRandomBot` checks.
  - `IsAIEnabled()`: true when `isAutoPilot` OR `!IsRealPlayer()`. Used by AI execution gating.
- **Safety**: real player is NOT treated as bot; session `IsBot() == false`, `IsRandomBot() == false`, `IsRealPlayer() == true`.
- **Gameplay strategy**: reuses `NewRpgDoQuestAction` (POI → mmap pathfinding → grind at POI → return to quest giver).
- **AutoPilotTask enum**:
  ```cpp
  enum class AutoPilotTask : uint8 {
      NONE    = 0,
      QUEST   = 1,
      DUNGEON = 2,  // reserved
  };
  ```

## Opcodes

| Opcode | Hex | Direction | Payload |
|--------|-----|-----------|---------|
| `CMSG_MOBILE_AUTO_QUEST_START` | 0x52B | C→S | `uint32 questId` (0 = auto-pick first incomplete) |
| `SMSG_MOBILE_AUTO_QUEST_START_RESPONSE` | 0x52C | S→C | `uint32 questId \| uint8 success \| string message` |
| `SMSG_MOBILE_AUTO_QUEST_STOP` | 0x52D | S→C | `uint8 stopReason \| uint32 questId \| string message` |

## Stop Reasons

0. Quest completed / rewarded
1. Manual movement detected
2. Player command (`.bot auto stop`)
3. Quest abandoned / status changed
4. Other fallback

## Key Server Files

- `src/server/game/Server/Protocol/Opcodes.h` / `.cpp` — opcode registration
- `src/server/game/Server/WorldSession.h` — handler declaration
- `modules/mod-playerbots/src/Bot/PlayerbotAI.h` / `.cpp` — state, `IsRealPlayer`, `IsAIEnabled`, `AllowActive` bypass
- `modules/mod-playerbots/src/Bot/PlayerbotMgr.h` / `.cpp` — `StartAutoPilot`, `StopAutoPilot`, movement detection, `.bot auto quest/stop`
- `modules/mod-playerbots/src/Script/Playerbots.cpp` — `HandleMobileAutoQuestStartOpcode`
- `modules/mod-playerbots/src/Ai/Base/Value/GrindTargetValue.cpp` — quest-only target filtering, relaxed fallback scan
- `modules/mod-playerbots/src/Ai/Base/Actions/ChooseTargetActions.cpp` — bypass `AllowActivity` for auto-pilot
- `modules/mod-playerbots/src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp` — skip accept-new-quest / quest-log-cleanup during auto-pilot
- `modules/mod-playerbots/src/Ai/World/Rpg/Action/NewRpgAction.cpp` — `DoIncompleteQuest` GO interaction logic
- `modules/mod-playerbots/src/PlayerbotAIConfig.h` / `.cpp` + `conf/playerbots.conf.dist` — `autoPilotEnabled`

## Auto-Gathering Extension (2026-06-28)

Handles gameobject-interaction quests (herbs, ore, chests):

- Two GO search paths in `DoIncompleteQuest`:
  - Objective 0-3 (NPC/GO): `FindNearestQuestGameObject(goEntry)` matches `RequiredNpcOrGo`.
  - Objective 4-9 (item): `FindNearestQuestItemGameObject(itemId)` scans CHEST/GOOBER and checks `LootTemplates_Gameobject.HaveQuestLootForPlayer()`.
- GOOBER (10): `go->Use(bot)` → `KillCreditGO()`.
- CHEST (3): lock/skill/spell/key fallback; if all fail, `SendLoot` + `CMSG_GAMEOBJ_REPORT_USE`.
- Uses `go->GetInteractionDistance()` instead of hardcoded `INTERACTION_DISTANCE`.
- Anti-repeat via `lastInteractGO` and next-candidate search.

## Recent Fixes (2026-07-14)

- **Dual-target quest mobs not attacked**: gray quest mobs were filtered out by `isHonorOrXPTarget`. Fixed by skipping that filter only when auto-pilot is active.
  - File: `modules/mod-playerbots/src/Ai/Base/Value/GrindTargetValue.cpp`
  - Code: `if (!bot->isHonorOrXPTarget(unit) && !botAI->IsAutoPilotActive()) continue;`
- **POI selection for multi-objective quests**: added score-based objective switching and 45s no-progress timeout.
  - Files: `NewRpgAction.cpp`, `NewRpgInfo.h`, `NewRpgAction.h`
- **Diagnostic logging prefixes**: `[GrindTarget]`, `[needForQuest]`, `[AttackAnything]`.
  - File: `ChooseTargetActions.cpp`

## Usage

```bash
.bot auto quest          # auto first incomplete quest
.bot auto quest 12345    # auto specific quest
.bot auto stop           # manual stop
# WASD movement auto-cancels
```

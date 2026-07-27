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

## Use-Item-On-Creature Quests (2026-07-17)

Handles quests credited by using a quest item on a creature (e.g. "wake up
the sleeping druid with the charm"), previously impossible: the objective is
`RequiredNpcOrGo > 0`, so it fell into the grind path and either the friendly
target was never attacked or it was killed before the item could be used.

- **Item identification**: `PlayerbotAI::FindQuestUseItem(quest, creatureEntry, &spellId, strongOnly)`
  scans `GetInventoryItems()`. Items must be **bound to the quest**
  (`IsQuestBoundItem`: StartItem / RequiredItemId / ItemDrop) — otherwise a
  use-item from another quest crediting the same creature entry would hijack
  the objective and burn the other quest's item (fixed 2026-07-17).
  Strong match: item spell has `SPELL_EFFECT_KILL_CREDIT`(90)/
  `KILL_CREDIT2`(134) with `MiscValue == creatureEntry`. Weak fallback
  (only when `strongOnly == false`): `quest->GetSrcItemId()` (StartItem)
  whose spell can target a unit — accepts `Targets & TARGET_FLAG_UNIT`
  OR any effect with `TargetA` object type `TARGET_OBJECT_TYPE_UNIT`
  (some cast-on-creature spells lack TARGET_FLAG_UNIT, e.g. quest 5441
  Lazy Peons: item 16114 → spell 19938 "Awaken Peon", dummy effect +
  SmartAI timed-actionlist CALL_KILLEDMONSTER — strong match impossible,
  weak match required).
- **Execution branch**: `NewRpgDoQuestAction::UseQuestItemOnCreatureObjective`
  in `NewRpgAction.cpp` (called before the GO/grind fallbacks in
  `DoIncompleteQuest`). Finds target via
  `NewRpgBaseAction::FindNearestQuestCreature(entry, 80, excludeGuid)`,
  moves within `SpellInfo::GetMaxRange() - 2`, then sends `CMSG_USE_ITEM`
  with `TARGET_FLAG_UNIT` (same packet shape as `UseItemAction::UseItem`).
- **Anti-spam**: `DoQuest.lastUseItemTarget` / `lastUseItemTime` +
  `useItemRetryTime = 4s`; cleared at every POI reset alongside
  `lastInteractGO`.
- **Grind guard**: `GrindTargetValue::needForQuest` (auto-pilot branch only)
  skips killing a creature when it is a use-item objective and the item is in
  bags — checked via `FindQuestUseItem(..., strongOnly = true)`, so only a
  proven KILL_CREDIT item bound to this quest vetoes killing; weak matches
  still allow the kill path (which also credits in this core).
- **Not covered yet**: weakened-target variants (use item at low HP),
  item-on-GO objectives, credit-bunny targets whose entry differs from the
  visible mob.

## Client Movement Fix (2026-07-17)

Fixed local-player movement during auto-pilot / AFK grind walking through
terrain/buildings and freeze-then-teleport glitches. Root cause:
`WMoveComponent.onMoveEvent` routed server-driven `SMSG_MONSTER_MOVE` updates
for the local player into the manual straight-line branch (ignoring spline
`PathPoints`, `originalPos`, terrain height; gravity disabled in auto-pilot),
while the code comment already stated auto-pilot players should use the sync
path. Changes (Unity client, `D:\Unity\clientproj`):

- `WComponents/Common/WMoveComponent.cs`
  - New `IsServerControlled` property: local player with `IsAutoPilot || IsAfkGrinding`.
  - `onMoveEvent` routes server-controlled players into the NPC sync branch
    (shadow-follow + path points + `TrySetHeight` per step).
  - `Update()` routes them to `LagSyncUpdate` instead of `PlayerUpdate`.
  - Removed two leftover `[SPEED_BUG]` `Debug.LogError` diagnostics.
- `WEntity/Registers/WMoveRegister.cs`
  - `MonsterMoveResponse` sets `moveArgs.isRun = true` for the server-controlled
    local player (SMSG_MONSTER_MOVE carries no run/walk flag; auto-pilot
    movement is always run speed). Without it `IsRun` stays false and the
    action component plays `STATE_MOVE` (walk) instead of `STATE_RUN`.
- `WEntity/WPlayer.cs`
  - `IsAutoPilot` / `IsAfkGrinding` setters now `NavComponent.Interrupt(false)`
    on entry so a stale client click-to-move navigation cannot resume after
    the server-driven session ends.

## Recent Fixes (2026-07-26)

- **Real player auto-learned professions during auto-pilot / AFK grind**: root cause was
  `gossip hello` trigger (CMSG_GOSSIP_HELLO / CMSG_QUESTGIVER_HELLO, PlayerbotAI.cpp:165-166)
  → `WorldPacketHandlerStrategy` → `TrainerAction`. `TrainerAction::Execute`'s guard relied on
  `HasActivePlayerMaster()`, but auto-pilot players have no master, so with
  `AiPlayerbot.AllowLearnTrainerSpells = true` (default) every teachable spell — including
  tradeskill (生活技能) trainer spells — was auto-learned.
- **Fix**: hard `botAI->IsRealPlayer()` early-return gates added to all adverse auto actions:
  - `TrainerAction::Execute` (auto-learn spells, the reported bug)
  - `TalkToQuestGiverAction::ProcessQuest` + `TurnInQueryQuestAction::Execute` (auto turn-in /
    auto reward pick; auto-pilot design is manual turn-in, NewRpgAction.cpp:931)
  - `AcceptInvitationAction` (group invite), `GuildAcceptAction`, `PetitionSignAction`,
    `ArenaTeamAcceptAction` (auto-accept invites)
  - `TradeStatusAction::Execute` (auto-accept trades)
  - `LootRollAction::Execute` + `MasterLootRollAction::isUseful` (auto need/greed rolls)
  - `EquipUpgradesPacketAction::Execute` (auto-equip loot; AutoEquipUpgradeLoot defaults true)
  - `LfgAcceptAction::Execute` (auto-accept LFG proposals)
  - `ReadyCheckAction::ReadyCheck` (auto ready checks)
  - `BGStatusAction::Execute` (auto-enter battlegrounds)
  - `AutoMaintenanceOnLevelupAction::Execute` (bot-style levelup broadcast; learn/talent/gear
    sub-actions were already `IsRandomBot`-gated and safe)
- Already safe (had `IsRealPlayer`/`IsRandomBot` gates): `ReleaseSpiritAction`,
  `ReviveFromCorpseAction`, `AreaTriggerAction`, `LfgJoinAction`, `LeaveGroupAction`,
  `AutoMaintenanceOnLevelupAction` learn/talent/gear sub-actions.

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

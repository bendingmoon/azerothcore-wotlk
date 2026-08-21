# Feature: AFK Grind (Auto Farm / 挂机)

> Original memory: `afk-grind-feature.md`

Completed 2026-07-14. Adds `AutoPilotTask::GRIND = 3` on top of the existing AutoPilot framework. Character attacks nearby mobs and loots within a radius; auto-cancels on manual movement or death (`AutoPilotStopReason::PLAYER_DIED`, shared stop-reason enum — see auto-pilot.md).

## Design

- Maximally reuses AutoPilot: identity control, activity gate, manual-movement detection, stop notification.
- Shares `PlayerbotMgr::StartAutoPilot()` / `StopAutoPilot()` entry with auto-quest.
- No `questId` in the protocol (simpler than auto-quest).

## Opcodes

| Opcode | Hex | C# Int | Direction | Payload |
|--------|-----|--------|-----------|---------|
| `CMSG_MOBILE_AFK_GRIND_START` | 0x535 | 1333 | C→S | `uint8 unused` |
| `SMSG_MOBILE_AFK_GRIND_START_RESPONSE` | 0x536 | 1334 | S→C | `uint8 success + string message` |
| `SMSG_MOBILE_AFK_GRIND_STOP` | 0x537 | 1335 | S→C | `uint8 stopReason + string message` |

## Key Files

- `src/server/game/Server/Protocol/Opcodes.h` / `.cpp` — 3 opcodes
- `src/server/game/Server/WorldSession.h` — `HandleMobileAfkGrindStartOpcode`
- `modules/mod-playerbots/src/Bot/PlayerbotAI.h` — `GRIND = 3`, `grindCenterPos`
- `modules/mod-playerbots/src/Bot/PlayerbotMgr.cpp` — GRIND case, `.bot auto grind`, opcode routing
- `modules/mod-playerbots/src/Script/Playerbots.cpp` — handler implementation
- `modules/mod-playerbots/src/Ai/Base/Value/GrindTargetValue.cpp` — radius check `≤ AfkGrindRadius`
- `modules/mod-playerbots/src/PlayerbotAIConfig.h` / `.cpp` + `conf/playerbots.conf.dist` — `AfkGrindRadius = 50`

## Unity Client Files

- `WWorldOpcode.cs`
- `WQuestHandlerRequest.cs` — `WMobileAfkGrindStartRequest`
- `WQuestHandlerResponse.cs` — `WMobileAfkGrindStartResponse`, `WMobileAfkGrindStopResponse`
- `WNetClient.cs` — handler registration
- `WQuestMgr.cs` — `StartAfkGrind`, `OnAfkGrindStartResponse`, `OnAfkGrindStop`, `IsAfkGrinding`
- `WPlayer.cs` — `IsAfkGrinding` + movement guard
- `MoonClient_WQuestMgrWrap.cs` — Tolua binding

## Recent Fixes (2026-07-14)

- **Compilation fixes for distance/position checks**:
  - `WorldPosition::GetDistance()` → `GetExactDist2d()`
  - `Player::GetWorldPosition()` → `WorldPosition(master)`
- **Radius check** now uses:
  ```cpp
  if (grindCenter.GetMapId() == bot->GetMapId() &&
      grindCenter.GetExactDist2d(*unit) > sPlayerbotAIConfig.afkGrindRadius)
      continue;
  ```

## Recent Fixes (2026-08-10)

- **Manual movement sometimes failed to cancel grind (stuck grinding forever)**: root
  cause was in the shared detection in `PlayerbotMgr::HandleMasterIncomingPacket` —
  `lastAIMoveTime` was refreshed every AI tick so the 500ms window never opened.
  Replaced with ACK-exclusion filtering; cancel is now deterministic. Details in
  auto-pilot.md (same-date entry).

## Recent Fixes (2026-08-21)

- **挂机结束后动不了**：与托管任务同根因——服务端微移动同步包把
  `WMoveComponent.Enabled` 锁 false，退出后摇杆和取消包全被挡死。修复见
  auto-pilot.md 同日条目（`OnServerControlEnd` + `StopAutoPilot` 无条件清理移动），
  `IsAfkGrinding` setter 退出分支同样已接入。

## Usage

```bash
.bot auto grind      # start AFK grinding
.bot auto stop       # stop
# WASD / joystick input auto-cancels (any movement opcode except server-forced-movement ACKs)
```

```lua
-- Lua
WQuestMgr:StartAfkGrind()
WQuestMgr:IsAfkGrinding()
```

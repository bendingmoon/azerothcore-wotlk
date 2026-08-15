# LFG bot 尸体堆积修复（2026-08-15）

## 症状

奥格瑞玛城内大量白骨尸体（客户端 WCorpse 显示兜底名"尸体"）。DB `corpse` 表奥格坐标框内 66 具全是 bot（playerbots_names 可匹配），每小时稳定新增 1~6 具、全天不断；`die_to_logout_sec` 全为 0（尸体在登出同一秒创建）。

## 根因链路（六步）

1. LFG 陪打 bot（LfgGroupBotMgr 池化角色）登录后**停驻在存档点**（常在奥格旅店/副本门口），不传送到玩家身边；进本时 `SetEntryPoint()` 记下停驻点（LFGMgr.cpp:2285-2288）。
2. bot 副本里战死；`AutoReleaseSpiritAction::ShouldAutoRelease` 因"队长同图+当前是副本"按设计**不释放**（ReleaseSpiritAction.cpp:182-188）→ 躺尸等救。
3. 打完/散队/被踢 → `BotLfgCleanupOperation::Execute` → `Group::RemoveMember`。
4. RemoveMember 触发核心 `LFGScripts::OnRemoveMember`（LFGScripts.cpp:259-263）：成员在副本图就**直接 `TeleportToEntryPoint()`，无死亡检查**（`LFGMgr::TeleportPlayer` 的 PLAYER_DEAD 检查在 LFGMgr.cpp:2245，但这条老路径被注释弃用）→ 死尸被拉回奥格停驻点。
5. 同一操作内登出 → `WorldSession::LogoutPlayer` 兜底 `BuildPlayerRepop`（WorldSession.cpp:662-667）→ 尸体在奥格**落库**。
6. 尸体为可复活尸体，旧过期时间 3 天（Corpse::IsExpired）→ 堆积。

## 修复内容

- `modules/mod-playerbots/src/Script/WorldThr/PlayerbotOperations.h` `BotLfgCleanupOperation::Execute`：退队/登出**之前**，若 bot 死亡 → `ResurrectPlayer(1.0f)`；若已有尸体（幽灵态）→ `Map::RemoveCorpse` + `corpse->DeleteFromDB` + `delete corpse`（完全仿照 `Map::ConvertCorpseToBones` 的三段式）。登出时 bot 是活的，`LogoutPlayer` 不再建尸体。新增 include：CharacterDatabase.h / Corpse.h / Map.h。
- `src/server/game/Entities/Corpse/Corpse.cpp` `IsExpired`：可复活尸体 `3 * DAY` → `2 * HOUR`（对全体玩家生效，白骨仍 60 分钟）。

## 关键认知

- `Player::ResurrectPlayer`（Player.cpp:4388）**不**自动处理尸体（本版无 SpawnCorpseBones 调用，转白骨发生在捡尸/灵魂医者等调用点），尸体必须手动清。
- `Player::GetCorpse()` 只查**当前图**（GetMap()->GetCorpseByPlayer，Player.cpp:4626-4629）；跨图幽灵尸体（尸在副本、魂在奥格）会漏清——由 2 小时过期兜底。
- `Map::RemoveCorpse`（Map.cpp:2989-3008）不 delete 对象、不删 DB 行，调用方负责；DB 删除用 `corpse->DeleteFromDB(trans)`（CHAR_DEL_CORPSE）。
- 白骨不入库、重启即清；可复活尸体入库（Player.cpp CreateCorpse SaveToDB），过期扫描时才转白骨。
- 尸体过期流转：可复活尸体过期 → `ConvertCorpseToBones`（DB 行此刻删除）→ 白骨再活 60 分钟。2h 改动后总可见时长 ≤ 3h，DB 在 2h 点即干净，重启无残留。
- 真人玩家跑尸窗口同样从 3 天变为 2 小时（用户明确要求）。
- `corpse.guid` = 角色 GUID；bot 身份用 `playerbots_names` JOIN `characters.name` 识别。

## 存量清理

```sql
DELETE c FROM acore_characters.corpse c
JOIN acore_characters.characters ch ON ch.guid = c.guid
JOIN acore_characters.playerbots_names n ON n.name = ch.name
WHERE c.mapId = 1 AND c.posX BETWEEN 1200 AND 2300 AND c.posY BETWEEN -5000 AND -3800;
```
执行后需重启 worldserver（已加载的内存尸体对象才消失）。

## 待验证

重编译重启后，跑一车 LFG 副本（有 bot 战死场景）确认：日志出现 `is dead at cleanup, resurrecting before logout`，且 corpse 表不再新增奥格 bot 尸体。

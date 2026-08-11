# LFG 陪打 bot 在队满血但不进副本修复（2026-08-12）

> 症状：1 个真人匹配 4 个陪打 bot，进本后有时只有 3 个 bot；缺失的 bot **还在队伍里、
> 满血、活着**，只是永远没被传进副本。

## 根因（代码逐行核实）

1. 匹配成功时核心 `LFGMgr::TeleportPlayer`（LFGMgr.cpp:2229）对**死亡/坠落/疲劳/
   载具/魅惑/战斗中**的成员只发错误包、无重试——bot 此时被落在野外（老问题，
   见 lfg-bot-follow-stuck-fix.md，当时已加 15s 轮询补发 CMSG_LFG_TELEPORT 兜底）。
2. **但兜底补发走的还是同一个 `TeleportPlayer`**，同样的检查再卡一次：如果 bot
   处于不会自愈的状态（战斗标志卡死——如怪闪避后威胁列表不清、宠物卡战斗；
   坠落/跳跃 unit state 卡死；在远海水里疲劳计时激活），**每次补发都静默失败**
   （仅 LOG_DEBUG），bot 永远进不来。且旧兜底以 `!IsInCombat()` 为前置条件，
   战斗卡死的 bot 连补发都轮不到。→ 完全吻合"在队、满血、不进本"。
3. 次生 bug：`CheckAndCleanup` 的 IN_QUEUE / IN_DUNGEON 分支把
   `!bot || !bot->IsInWorld()` 一律当"掉线"清理登出；bot 跨图传送途中
   （RemovePlayerFromMap 后、worldport ack 前不在任何地图上）恰好被 15s 轮询
   撞上会被误清。注意 `ObjectAccessor::FindConnectedPlayer` 能查到不在世界的
   在线玩家，`FindPlayer` 反而要求 IsInWorld（ObjectAccessor.cpp:245-260）。

## 改动内容（modules/mod-playerbots）

- `src/Bot/LfgGroupBotMgr.h`：`LfgSpawnedBotInfo` 新增 `teleportFixAttempts`。
- `src/Bot/LfgGroupBotMgr.cpp` `CheckAndCleanup`：
  - IN_DUNGEON 兜底传送改**两段式**：第 1 次轮询发现落单 → 补发
    CMSG_LFG_TELEPORT（官方路径，能走通就走通）；下一轮仍在副本外 → 说明
    TeleportPlayer 在为一个不会自愈的理由拒绝 → 直接 `CombatStop() +
    TeleportTo(副本地图入口)` 强拉（TeleportTo 自己会清移动标志/断战斗/处理
    宠物与载具；传送前补 `SetEntryPoint()` 记账，与 TeleportPlayer 一致，
    保证以后"传出副本"回到正确位置）。前置条件去掉 `!IsInCombat()`，战斗卡死
    的 bot 也能被强拉。进本后重置计数并打日志。
  - IN_QUEUE / IN_DUNGEON 的"掉线清理"拆分：`!bot`（真掉线）才清理；
    `bot && !IsInWorld()`（传送途中）跳过本轮，下轮再查。

线程安全性：CheckAndCleanup 与 bot session 包泵（PlayerbotHolder::UpdateSessions ←
OnPlayerbotUpdate ← World.cpp:1191）都在世界线程，直接 TeleportTo 与原补发路径同级。

## 部署 / 验证 / 回滚

- modules 为 static 编译，需重新编译 worldserver 生效（本次本地未编译验证）。
- 注意：上一个兜底修复（lfg-bot-follow-stuck-fix.md，08-11）当时也未编译验证，
  若线上二进制旧于该修复，本症状会 100% 复现——先确认构建包含两次修复。
- 验证：
  1. 排随机本，匹配成功后 4 个 bot 全部进本；
  2. 若仍有落单，日志应依次出现 "is outside the dungeon ..., teleporting in" →
     （15s 后）"still outside the dungeon ..., force-teleporting in" →
     "made it into the dungeon"；
  3. 副本全程观察 bot 不再出现"在队但永远不进本"。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.cpp src/Bot/LfgGroupBotMgr.h`

## 已知残余（未修）

- 落单期间 bot 死亡变幽灵在外游荡：兜底有 IsAlive 门，等其 AI 跑尸复活后拉入；
  若 ghost 跑尸卡死（尸体在副本内、外面跑不到）仍无法自愈，属极端情况。
- `TeleportPlayer` 拒绝原因只有 LOG_DEBUG，排查时看不到——如再发可先开 debug
  日志确认具体 error 码（DEAD/FALLING/FATIGUE/VEHICLE/CHARM/COMBAT）。

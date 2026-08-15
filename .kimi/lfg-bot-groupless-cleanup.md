# LFG bot 掉出队伍立即清理（2026-08-13）

> 需求：陪打 bot 如果不在队伍中了就直接清理掉，不要挂着等超时。

## 口径（为什么不能按字面"不在队伍就清"）

bot 状态机里有两个状态**本来就不在队伍里**，一刀切会废掉装配/补位功能：

- `WAITING_ASSEMBLY`：停驻待命，等 TryAssembleGroup 组队（进组前无队伍是正常态）；
- `IN_QUEUE`（poolMode，踢人补位替补）：solo 在撮合池排队（进组前无队伍是正常态）。

`IN_DUNGEON` 原本已有"不在队伍就清理"分支，但其它"进过组又掉出来"的路径没有兜底，
bot 会一直挂到各种超时（停驻 120s / 池排队 300s）才被注意。

## 实现

- `LfgGroupBotMgr.h`：`LfgSpawnedBotInfo` 新增 `wasGrouped`（被观测到在队至少一次；
  停驻/池排队的 bot 永远为 false）。
- `LfgGroupBotMgr.cpp` `CheckAndCleanup` 每轮轮询入口（各状态分支之前）：
  bot 在线且在世界中 → 有组则置 `wasGrouped`；`wasGrouped && 无组` → 立即
  `CleanupBot`。传送途中（!IsInWorld）跳过，防止误判。
- 重复清理由 `MarkBotForCleanup` 去重；与 IN_DUNGEON 既有分支互为双保险。

## 覆盖到的路径

- 组队后 rolecheck/proposal 失败 teardown 漏清的 bot；
- bot AI 自己 LeaveGroupAction 掉出队伍；
- 队伍解散时最后一个成员恰好是 bot（Group.cpp 单人解散改动的穿插时序）；
- 替补 bot 进组后又被踢。

## 已知残余

- 清理操作本身失败导致的"已脱离跟踪但仍在线"的孤儿 bot 不在本轮扫描范围
  （跟踪表已除名，LfgGroupBotMgr 看不到它），暂未发现实际发生。

## 部署 / 验证 / 回滚

- 模块 static 编译，需重编 worldserver（本地未编译验证，遵循 AGENTS.md 约定）；
  `codestyle-cpp.py` 两个文件均通过。
- 验证：
  1. 正常单排 → bot 停驻（无队伍）**不被**误清，装配进本正常；
  2. 本内踢掉一个 bot → 该 bot 立即被清理登出（不再等超时）；
  3. 玩家在副本里主动退队 → 剩余 bot 在 master-null 60s 清理后，最后一个 bot
     若因队伍解散掉出组，下一轮轮询（1s）即被清理；
  4. 日志出现 "is no longer in a group, cleaning up"。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.h src/Bot/LfgGroupBotMgr.cpp`

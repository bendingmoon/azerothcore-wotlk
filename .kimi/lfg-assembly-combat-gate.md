# LFG 装配战斗门控：战斗中不弹 proposal，两轮后移出队列（2026-08-15）

> 需求：随机地下城 2 分钟后匹配 bot 弹出"确认进入"时，若玩家正在战斗会有问题——
> 战斗中没点弹窗 40s 超时（`LFG_TIME_PROPOSAL`，LFGMgr.h:50）会被记 DENY、吃 150s
> DUNGEON_COOLDOWN 光环（LFGMgr.cpp:2036-2040，pussywizard 私货，零售没有）并移出
> 队列、bot 全退场；战斗中点了接受则传送被拒（`LFG_TELEPORTERROR_COMBAT`，
> LFGMgr.cpp:2261），bot 进本玩家落野外。
> 官方（零售/上游）口径：弹窗与战斗无关、可接受；传送被战斗拒绝是**故意设计**
> （防传送脱战逃生），脱战后点小眼睛手动重进；超时未答=移出队列。
> 本修复在模块侧加战斗门控，从源头避免弹窗撞上战斗；核心 LFG 零改动。

## 实现

- `LfgGroupBotMgr.h/.cpp` 新增 `CombatBlocksAssembly(Player*)`，两个调用点：
  - `OnPlayerQueueForLfg`（spawn 侧，入队延迟门控之后）：战斗中不 spawn bot；
  - `TryAssembleGroup`（装配侧，玩家冷却/逃兵光环检查之后）：战斗中不装配。
- 轮次语义（`m_combatBlockStart` / `m_combatBlockRounds`，世界线程）：
  - 首次发现玩家在战斗 → 记 `combatBlockStart`，return 等下一轮轮询；
  - 连续战斗满 `AiPlayerbot.LfgAssemblyCombatTimeout`（默认 300s，0=关闭门控恢复旧行为）
    → 消耗一轮：`CleanupBotsForPlayer` 清掉停驻 bot，重新开始下一轮计时；
  - 第 2 轮（`LFG_ASSEMBLY_COMBAT_ROUNDS_MAX = 2`）也耗尽 → `sLFGMgr->LeaveLfg(playerGuid)`
    （纯移出队列，**无冷却光环**，脱战后可立即重排）+ 中文系统消息通知玩家
    （"你长时间处于战斗状态，地下城查找器已将你移出队列……"，项目内有中文
    sysmessage 先例 Playerbots.cpp:109）；
  - 任何一次轮询发现玩家脱战 → 两张表都清空，下一轮立即 spawn/装配
    （装配只需 1s 脱战窗口即可触发，不存在"脱战但轮次被累计"的不公）。
- park 超时豁免：`CheckAndCleanup` 的 120s `LFG_BOT_PARK_TIMEOUT` 分支新增
  `!m_combatBlockStart.count(playerGuid)` 条件——战斗等待期间的 bot 由轮次边界
  统一清理，不再每 120s 白循环 spawn/清一批。
- 重置点：装配成功（DUNGEON）与玩家不在队列两处既有 reset（清 m_assemblyFailures
  的地方）同步清两张战斗记账表。注意 `CleanupBotsForPlayer` 内**不能**清
  （轮次边界会调它，清了会重置轮次计数）。
- 最坏时长：连续战斗约 10 分钟（300s×2 + 轮询粒度）后被移出队列。

## 边界说明

- 只挡"装配前"的战斗；proposal 已弹出后玩家才进战斗的残余窗口仍在（40s 内点接受
  → 传送被拒 → 脱战后小眼睛 → LfgTeleport(0) 进本，客户端链路已通，
  WBattleMgr.Dungeon.cs:952 + MainCtrl.lua BtnLfgFunc）。客户端未做自动重试。
- 玩家在战斗中被真人撮合走（proposal/DUNGEON）不受门控影响，属官方行为。
- 踢人补位（pool 模式）玩家在副本内无需传送，不受影响（门控只加在 fresh 装配路径；
  OnPlayerQueueForLfg 的门控对补位同样生效——战斗中不补 spawn，脱战后立即补）。

## 改动文件

- `modules/mod-playerbots/src/Bot/LfgGroupBotMgr.h`：CombatBlocksAssembly 声明 +
  m_combatBlockStart / m_combatBlockRounds 成员。
- `modules/mod-playerbots/src/Bot/LfgGroupBotMgr.cpp`：常量 LFG_ASSEMBLY_COMBAT_ROUNDS_MAX=2、
  CombatBlocksAssembly 实现、两处门控调用、park 超时豁免、两处 reset 补清、
  include Chat.h（本 core 里 ChatHandler 定义在 src/server/game/Chat/Chat.h，模块惯例引 Chat.h）。
- `modules/mod-playerbots/src/PlayerbotAIConfig.h/.cpp`：`lfgAssemblyCombatTimeout`，
  配置键 `AiPlayerbot.LfgAssemblyCombatTimeout`，默认 300。
- `modules/mod-playerbots/conf/playerbots.conf.dist`：配置说明。

## 部署 / 验证 / 回滚

- modules static 编译，需重编 worldserver；本地未编译验证（遵循 AGENTS.md 约定）。
  `python3 apps/codestyle/codestyle-cpp.py` 四个 C++ 文件均通过。
- 验证：
  1. 单排随机本，2 分钟门控过后保持战斗（找个怪挂着）→ 日志出现
     "is in combat, holding off bot matchmaking"，无 spawn、无弹窗；
  2. 脱战 → 下一轮轮询（≤1s/≤30s）立即 spawn/装配，弹 proposal；
  3. 战斗持续 300s → 日志 "combat wait round 1/2"，停驻 bot 被清；
     再 300s → "removing from LFG queue"，玩家收到系统消息，LFG 界面复位，
     无冷却光环可立即重排；
  4. `AiPlayerbot.LfgAssemblyCombatTimeout = 0` 恢复旧行为。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.h src/Bot/LfgGroupBotMgr.cpp src/PlayerbotAIConfig.h src/PlayerbotAIConfig.cpp conf/playerbots.conf.dist`

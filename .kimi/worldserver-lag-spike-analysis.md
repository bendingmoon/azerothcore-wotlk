# 世界服卡顿（Update time diff 尖峰）分析与优化方案

> 记录日期：2026-08-12。线上服（宝塔面板，Linux，72 核 / 125.8G 内存，负载常年 <5%）。
> 结论先行：尖峰主因是 **mod-playerbots 的 LFG 按需机器人系统**在世界线程上成批登录 bot + `PlayerbotFactory::Randomize`，并被"失败重试循环"放大。优化分两步：先错峰+退避，再池化复用。

## 1. 问题现象

- `Update time diff` 偶发尖峰：14452ms（321 人在线）→ 后续 5505 / 4871 / 1852ms。
- 常态指标健康：Median 3~13ms，P99 < 70ms。即"单线程偶发阻塞"，不是容量不足。
- 面板 CPU/内存/磁盘都很闲 → 世界主线程在"等/算"，不是资源瓶颈。
- 尖峰频率约每 5~10 分钟一次（受 `RecordUpdateTimeDiffInterval = 300000` 影响，日志每 5 分钟最多记一条，仅记 >`MinRecordUpdateTimeDiff = 100` 的）。

## 2. 诊断结论（已定位）

主因链路（全部跑在世界线程）：

1. `RandomPlayerbotMgr::CheckLfgQueue()` 周期性扫描全服玩家（`modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp:1259`），对排队真人调 `sLfgGroupBotMgr.OnPlayerQueueForLfg(...)`（:1342）。
2. `LfgGroupBotMgr` 一次性 spawn 4 个 bot（`LfgGroupBotMgr.cpp:150-213`），`LoginBot` → `sRandomPlayerbotMgr.AddPlayerBot(guid, 0)`（:556）。
3. 登录用 `DelayQueryHolder` 异步预取（`PlayerbotMgr.cpp:146-157`），但**回调在世界线程上构建 Player 对象**；一批 4~9 个 bot 同拍完成 → 挤在一帧。
4. 下一轮 `CheckAndCleanup` 给每个 bot 跑 `PlayerbotFactory::Randomize()`（`LfgGroupBotMgr.cpp:633`，内部做装备/天赋/技能全量重建，CPU 密集）+ `SetValue(bot, "level", 玩家精确等级)`（:625-633）。
5. **失败重试放大器**：`Assembly rolecheck failed → Tearing down → Cleaning up all bots → 下一周期重新 spawn 4 个`（日志中 failures: 1→2→3 连续出现）；另有给已离线玩家白 spawn 一轮的浪费（"no human found (all offline?)" / "offline > 5min" cleanup）。

佐证：

- 尖峰时间点与 Playerbots.log 中 `Spawned bot` / `logged in` / `synced to level` 突发吻合。
- `MaxRandomBots = 0`（无后台随机 bot），bot 负载 100% 来自 LFG 按需 spawn → 治好 `LfgGroupBotMgr` 即治好 bot 引起的尖峰。
- 重启回填期（玩家 47→153 爬升）尖峰 = bot 登录突发 + 真人回连风暴叠加。

## 3. 关于"加缓存"的讨论结论

- 这版 `PlayerbotFactory` 直连 SQL 已经很少：world 库查询是启动时建一次的静态缓存（`enchantGemIdCache`、`ccBreakTrinketCache`，`PlayerbotFactory.cpp:463/472`）。瓶颈不在 SQL 等待。
- Randomize 的大头是 **CPU 型全量重建**（清/学法术技能、装备逐件生成、属性重算、数据包构建）+ 登录回调的对象构建 + 销毁-重建循环。
- 因此"缓存"的正确形态：**池化复用**（消灭重复重建）+ 静态数据 memoize（按需，照 `ccBreakTrinketCache` 模式）+ 错峰退避（摊平同拍突发）。

## 4. 优化实施计划

### 第一步：错峰 + 退避（小而稳，先压尖峰）——只改 `LfgGroupBotMgr.cpp`

- 登录错峰：每次 `CheckLfgQueue` 周期最多发起 1~2 个 `LoginBot`，其余进等待队列逐拍消化。
- Randomize 错拍：`CheckAndCleanup` 每拍最多对 1 个 bot 调 `Randomize`。
- 退避：teardown / rolecheck 失败后给该玩家挂 60s 冷却，打断"失败→拆→重 spawn"热循环。
- spawn / 组装前复查玩家在线，消掉给离线玩家白干一轮的浪费。

> **已实现（2026-08-13，第二版）**，改动仅在 `LfgGroupBotMgr.cpp/.h`。
> 注意：第一版用"全局 15s 登录间隔"错峰，但 `CheckLfgQueue` 周期是 30s，
> 每玩家每周期只能发出 1 个登录 → 整队要 90~120s，超出装配设计预算
> （过 2 分钟门控后 ~60s 内进本，见 lfg-bot-force-assembly.md），已返工为
> **挂起队列 + 1s 节拍**：
> - 登录错峰：`OnPlayerQueueForLfg` 只把缺口入队 `m_pendingSpawns`（角色+poolMode），
>   `CheckAndCleanup` 每拍（`LFG_CLEANUP_INTERVAL = 1`s）由 `ProcessPendingSpawns`
>   消化 1 条：复查玩家在线/在队/仍 QUEUED → `SelectBotCharacter` → `LoginBot`。
>   4 个 bot 约 4s 发完登录，登录构建天然分散在不同帧。
> - Randomize 错拍：每拍最多 1 个 bot 做登录后重初始化（等级同步 + `Randomize`），
>   其余留在 `LOGGING_IN` 等下拍；玩家已离线的 bot 不初始化直接清理。
> - 节拍：`CheckAndCleanup` 15s → 1s（全部超时检查都是时间戳式，不受影响；
>   teleportFixAttempts 两段式兜底仍成立：合法传送 1 个 tick 内即处理，1s 后仍未进本
>   才强制传送）。装配触发粒度随之从 ≤15s 提到 ≤1s。
> - 队列一致性：挂起条目计入 `GetSpawnedBotCountForPlayer` / `CalculateNeededRoles`
>   （下一周期不会重复入队）；`CleanupBotsForPlayer` 连带丢弃该玩家的挂起条目。
> - 退避：`TeardownAssembly` 后该玩家挂 `LFG_ASSEMBLY_FAILURE_COOLDOWN = 60`s 冷却
>   （`m_assemblyCooldown`），`OnPlayerQueueForLfg` 与 `TryAssembleGroup` 双侧拦截；
>   进副本成功或玩家离队时清除。
> - 复查在线：装配前玩家须 `IsInWorld()`；`TransitionToInDungeon` 改用
>   `FindConnectedPlayer`（传送中的真人仍算在线），全组无真人在线时立即 `CleanupBot`。
> - 效果：过门控后整队约 8~12s 上线+装配（原同拍 4 个、第一版错峰 90~120s），
>   世界线程任意一帧最多 1 个登录构建 + 1 个 Randomize；失败重试被 60s 冷却打断。

### 第二步：池化复用（治本）

- cleanup 不再销毁 bot，改为"还池"；池按 **（职业 × 等级段）** 索引。
- 复用走**轻量同步**（不重建）：`GiveLevel(目标级)` + `InitStatsForLevel` + `InitAvailableSpells`（幂等补学）；
  - 装备容差带 ±3~5 级内不动，出带才单跑 `InitEquipment`；
  - 天赋点差 ≤2~3 点不重排，否则才 `InitTalentsTree`。
- **只升不降**：降级要脱装备/删法术，太麻烦；遇到降级需求换桶或回炉重建。
- 放宽 `LfgGroupBotMgr.cpp:625` 的"精确同级"过度约束 → bot 等级落在玩家可排副本等级范围（±2~3 级）即可，命中率翻倍。
- 现成的轻量路径分水岭：`Randomize(incremental=true)` 跳过 Clear 系列（`PlayerbotFactory.cpp:568-578`），天赋/装备还有 `equipmentPersistence` 开关（:625/:673）。

## 5. 验证 / 排障工具

- **PerfMon（先量再改）**：`AiPlayerbot.PerfMonEnabled = 1`，游戏内 GM 跑 `.playerbots pmon`（`PlayerbotCommandScript.cpp:75`），看 `PlayerbotFactory_Reset` / `RandomizeFirst` / 登录等各埋点毫秒数。
- **时间戳对照**：Server.log 的 `Update time diff: Xms` 时间戳 vs Playerbots.log 同秒是否有 spawn 突发。
- **A/B 验证**：低峰把 `AiPlayerbot.LfgSpawnBotsOnDemand = 0` 观察一晚，尖峰消失即实锤（代价：暂时没有机器人陪排本）。
- **不崩服抓栈**：卡顿时 `gdb -p $(pidof worldserver) --batch -ex "thread apply all bt" > /tmp/bt.txt`，抓完进程继续跑。
- **MySQL 慢查询**：`long_query_time = 1`，对照尖峰时间。

## 6. FreezeDetector（MaxCoreStuckTime）注意事项

- 这版核心触发 = **主动崩服**（`Main.cpp:643-644`：`ABORT("World Thread hangs...")`），不是温和记日志。当前设 15 秒；线上需确认有守护自动拉起。
- 崩溃输出：`LOG_ERROR` 进 Server.log；ABORT 消息打 stderr；Linux 下**没有文本堆栈文件**（`Crashes\` 目录仅 Windows 有，`WheatyExceptionReport.cpp`），堆栈要靠 core dump + gdb。
- 若要抓 3~5 秒级尖峰的现场，可低峰临时调 3（会崩服，需守护），抓到 core 后调回。

## 7. 已建议的配置调整（worldserver.conf）

- 日志降噪：`Logger.playerbots=3`（这些 LFG 日志是 LOG_INFO，4 级挡不住）、`Logger.network.opcode=2`、`Logger.module=4`、`Logger.auctionHouse=4`、`Log.Async.Enable = 1`。
- `PlayerSaveInterval = 60000` 太密（默认 900000），建议 300000~900000。
- `Network.Threads = 10` 过多 → 2~4；`ThreadPool = 30` → 4~8。
- `Wintergrasp.Enable = 1` 与 TBC（Expansion=1、满级 70）不一致，不需要就设 2。
- `ProcessPriority = 1` 仅 Windows 有效，Linux 用 systemd `Nice=`/`CPUAffinity=`。

## 8. 杂项清理（与卡顿无关，顺手做）

- **spell 2567 / skill 176（投掷）刷屏**：非法职业角色被教了投掷（疑似自定义全技能 NPC），登录校验删除后又被教回。清理：
  ```sql
  DELETE cs FROM `characters`.`character_spell` cs
  JOIN `characters` c ON c.guid = cs.guid
  WHERE cs.spell = 2567 AND c.class IN (2,5,7,8,9,11);
  ```
  并检查 `Creatures.CustomIDs`（190010,55005,999991,25462,98888,601014,34567,34568）里哪个 NPC 在教 2567，断源。
- **`No races are available for class: 6`**：TBC 无死亡骑士，随机 bot 建号配置里摘掉 DK（启动少几十次失败建号）。
  **已修复（2026-08-13）**：`RandomPlayerbotFactory.cpp:710` 建号循环在 `disableDeathKnightLogin` 或 `Expansion != WOTLK` 时跳过 DK（配置侧修不掉——该循环只认核心 `DisabledClassMask`，不管登录开关）。
- **日志刷屏代码**：`LfgActions.cpp:192` 的 "LFG roles checked" 每 tick 重复打，应改为状态变化时才打；`CheckLfgQueue` 每人一行可降 LOG_DEBUG。
- `MoveSplineInitArgs::Validate: velocity > 0.01f failed`（entry 11794/17729/16843/17724/21696/4662/18642）：生物 AI 移动速度为 0，无害警告。
- `ActionButton loading problem`：动作条残留已无法术，自动清理，无害。
- `Possible hacking attempt ... loot gameobject`：多为 bot 拾取 respawn 中的 GO，非真外挂。

## 9. 关键代码位置索引

| 内容 | 位置 |
|---|---|
| FreezeDetector / 崩服逻辑 | `src/server/apps/worldserver/Main.cpp:378`、`:623-652` |
| diff 日志生成 | `src/server/game/Time/UpdateTime.cpp:163-175` |
| LFG 全员扫描 | `modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp:1259-1351` |
| spawn 批量逻辑 | `modules/mod-playerbots/src/Bot/LfgGroupBotMgr.cpp:150-213` |
| 登录入口 | `LfgGroupBotMgr.cpp:533-559`（LoginBot）→ `PlayerbotMgr.cpp:83-160`（AddPlayerBot 异步 holder） |
| 清理/还池改造点 | `LfgGroupBotMgr.cpp:563-579`（CleanupBot）、`:242-266`（CleanupBotsForPlayer） |
| Randomize 调用点 / 等级精确同步 | `LfgGroupBotMgr.cpp:605-709`（:625-633 等级同步、:633 Randomize） |
| Randomize 主流程 | `modules/mod-playerbots/src/Bot/Factory/PlayerbotFactory.cpp:552-710` |
| 静态缓存示例 | `PlayerbotFactory.cpp:455-500`（gems / CC-break trinket） |
| 刷屏日志 | `modules/mod-playerbots/src/Ai/Base/Actions/LfgActions.cpp:192` |

## 10. 安全提醒

- worldserver.conf 的数据库密码（acore 账号）曾在排障对话中明文贴出，建议尽快改密；以后贴配置先打码 `*DatabaseInfo` 三行。

## 11. 模块级补充扫描（2026-08-13，错峰+退避落地后）

> 对 mod-playerbots 全模块做了一轮"世界线程阻塞点"扫描（三个方向：同步 DB、每 tick 处理器、重 CPU）。
> 在当前配置（MaxRandomBots=0、Autologin=0、GuildTasks=0）下，以下大头已确认**不触发或已异步**：
> CheckBgQueue/ProcessBot/GetBots/ScaleBotActivity/CSV 日志（autologin 门控）、GuildTask 全链（开关短路）、
> `SetEventValue` 落库（CommitTransaction 异步）、`SaveToDB` 的 DB I/O 本身（异步，但**语句拼接是同步 CPU**）。

### A. 剩余尖峰点（**已实现，2026-08-13**）

1. **登出尖峰（已修）**：`LfgGroupBotMgr` 三个清理入口（`CleanupBot`/`CleanupBotsForPlayer`/`OnBotLeftGroup`）
   不再直接 `QueueOperation`，改为推入 `m_pendingCleanupOps`，`CheckAndCleanup` 每拍（1s）最多 drain
   `LFG_CLEANUP_BATCH = 2` 个 → 整队 4~5 个 bot 登出摊到 ~2s 多帧。标记（TO_LOGOUT）仍即时生效，
   仅重活延后；重选保护由 `LoginBot` 的在线检查兜底（已标记未登出的 bot 被选中也只会 spawn 失败重试）。
2. **LFG spawn 链 4 个同步小查询（已修）**：
   - `SelectBotCharacter`（`LfgGroupBotMgr.cpp`）→ 废掉 `ORDER BY RAND()` SQL，改为进程内一次
     `BuildBotPool()` 全量预载（按职业分桶 `{guidLow, race}`），抽取时按阵营/排除集/`FindConnectedPlayer`
     内存过滤，最多 12 次随机尝试；outLevel 改由 `sCharacterCache` 实时取（仅日志用）。
     池子在建号之后 lazy 构建一次；控制台重建账号需重启生效（与 addclassCache 同款语义）。
   - `IsAccountLinked`（`PlayerbotMgr.cpp:105`）→ `masterAccountId == 0`（随机 bot 登录）时短路跳过
     （isRndbot 本来就放行，查询不改变结果）。真人手动 add 仍走原查询。
   - `IsAccountType`（`RandomPlayerbotMgr.cpp`）→ `AssignAccountTypes` 结尾把全表灌进
     `m_accountTypeCache`（mutex 保护），之后纯内存；未加载前（启动早期）回退原 DB 查询。
   - `PlayerbotRepository::Load` → 读穿缓存 `m_storeCache`；该表写入方只有本类的 `Save`/`Reset`，
     两者同步维护缓存（Randomize 里的 `Reset` 会清缓存项，下次 Load 回源）。
3. **附带修复**：`OnBotLogin`（`PlayerbotMgr.cpp`）原来**每次 bot 登录都无条件 `SaveToDB`**
   （世界线程 1~10ms 语句拼接）持久化 NO_XP_GAIN 标志——改为标志真变化时才存。

### B. 顺手小改（**已实现**）

4. `CheckLfgQueue` 逐玩家日志（`RandomPlayerbotMgr.cpp` 三处）降 `LOG_DEBUG`；周期边界日志保留 INFO。
   （`LFGMgr::GetState` 的 map 插入副作用与跨线程无锁访问属核心既有问题，未动。）
5. `BotGuildCacheWorldScript::OnUpdate` 增加门控：`randomBotGuildCount == 0 && !randomBotGuildNearby`
   时直接 return，跳过每小时全表扫描。
6. `PetIsDeadValue::Calculate`（`StatsValues.cpp`）：无宠物时按角色缓存"是否拥有宠物"60s
   （mutex 保护，地图线程安全），不再每次 AI 评估都同步查 `character_pet`。

### C. 观察项（现在不重，随规模放大）——**本轮不动**

6. `HandleMasterOutgoingPacket`（`PlayerbotMgr.cpp:1749`）：每个发给真人的包遍历全部 bot 比对 master，
   O（全服发包数 × 在线 bot 数）；bot 少时无碍，bot 多了做 master→bots 反查表。
7. `IsRandomBot` 依赖 `currentBots`，autologin=0 时恒空 → LFG bot 被 `OnPlayerLogin` 塞进 `players` 向量，
   `HasPlayerNearby`/`AllowActive` 会把 bot 当"附近真人"（行为偏差，开销小）。
8. 死代码：`activateCheckBgQueueThread/activateCheckLfgQueueThread/activatePrintStatsThread` 无调用者。

### D. "bot 越多越卡"深扫（2026-08-13，结构性发现）

> 症状：在线 bot 越多 diff 持续越高（非尖峰）。逐层排查后排除每包扫描（已有 4-opcode 白名单）、
> bot session 空转（不进 WorldSessionMgr，零开销）、广播投递（构建一次+廉价丢弃）。
> 真正的线性放大点：

1. **主犯：BuildUpdate 把 bot 当完整接收者，序列化产物随即丢弃**。**（已修复，2026-08-13）**
   `Map::SendObjectUpdates`（`Map.cpp:1710`）对每个变化对象调 `WorldObject::BuildUpdate`
   （`Object.cpp:3045-3058`）→ 对 `_visiblePlayersMap` 里**每个接收者**（含每个 bot，可见性链接
   `ObjectVisibilityContainer::LinkWorldObjectVisibility` 全程无 bot 判定）跑 `BuildFieldsUpdate`
   = 全字段扫描 + `ByteBuffer(500)` 堆分配 + 拷贝；然后在 `Map.cpp:1716-1720` 被
   `OnPlayerbotCheckUpdatesToSend` 丢弃。**成本全额支付、产物零利用**，随"视野内变化对象数 × bot 数"
   放大（副本战斗 = 高密度变动）。AI 直读对象字段、不消费 UpdateData → 跳过构建对 bot 无影响。
   **修复**：`Object.cpp` `BuildUpdate`——接收者循环对 `GetSession()->IsBot()` 跳过
   `BuildFieldsUpdate`；对象自身是 bot 时 self 分支同样跳过。可见性数据结构不动，`HaveAtClient`
   语义不变，真人（含 autopilot 真人，`IsBot()=false`）不受影响。线上 `MapUpdate.Threads = 8` 已确认，
   多核摊薄已开启。
2. **从犯：bot 的 relocation 可见性事件无门控**。**（已修复，2026-08-13）**
   bot 位移超阈值触发 `ExecuteDelayedUnitRelocationEvent`（`Unit.cpp:16400`）→ 全视野
   `Cell::VisitObjects` + 逐对象包构建。修复方案（核心三处，保持 Link/Unlink 簿记不动、`HaveAtClient` 语义不变）：
   - `PlayerUpdates.cpp` 批量版 `UpdateVisibilityOf<T>`（relocation 扫描路径）：bot 跳过
     `BuildCreateUpdateBlockForPlayer`/`BuildOutOfRangeUpdateBlock`（建块才是大头，且在此层就已发生，
     不是 SendToSelf 才发生）；
   - `PlayerUpdates.cpp` 单目标版 `UpdateVisibilityOf`（对象进出视野通知路径）：bot 跳过
     `SendUpdateToPlayer`/`DestroyForPlayer`/`GetInitialVisiblePackets`，Link/Unlink 照常；
   - `GridNotifiers.cpp` `VisibleNotifier::SendToSelf`：簿记跑完后 bot 早退，跳过 `BuildPacket` +
     逐新单位的 `GetInitialVisiblePackets`（光环/近战起手包）。
   判定统一用 `GetSession()->IsBot()`（只有构造 session 时定型，真人托管不受影响）。
3. **配置乘数：`MapUpdate.Threads` 默认 1**（`WorldConfig.cpp:547`）。=1 时所有副本实例+野外+全部
   bot AI 串行在**一条** worker 线程，世界线程每 tick `m_updater.wait()` 等它（`MapMgr.cpp:278`）——
   bot 成本 100% 落在世界 tick 关键路径。72 核机器建议 4~8。maps 是共享队列动态领取（`MapUpdater.cpp:172`），
   >1 即可把不同副本实例摊到多核（单实例内部仍单线程）。
4. **固有 AI 成本（非 bug，可后续优化）**：LFG bot 有真人 master → react 固定 100ms
   （`PlayerbotAI.cpp:6868`）；`PossibleTargetsValue`/`RtiTargetValue` 等值 checkInterval=1 →
   **每次 Get 都重算**（grid 扫描 + LOS vmap 射线，`NearestUnitsValue.cpp`、`RtiTargetValue.cpp:35`）；
   战斗 bot 单 engine tick 估 0.5~3ms。后续可给这些热值加 checkInterval 缓存。

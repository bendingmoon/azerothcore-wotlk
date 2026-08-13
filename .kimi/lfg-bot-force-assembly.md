# LFG 强制组队装配：2 分钟后确定性补 bot 开打（2026-08-12）

> 需求/事故：真人单排随机地下城 20 分钟没配上 bot，最后匹配到 4 个真人才开打。
> 旧的"bot 各自 solo 进公共撮合池"设计在结构上无法保证撮合成功，改为
> **服务端直接组队 → 满编整队排 LFG → 必出 proposal**。

## 旧设计的四个断点（排查结论，供回溯）

1. **主因**：bot 入队列表用 `LfgDungeons[teamId]`（全服排队真人所选地下城的并集）。
   核心 `JoinLfg` 规定 RANDOM 只能单选不可混排（LFGMgr.cpp:667），并集里带随机 ID 且
   size>1 时 bot 入队被静默拒绝（LFG_JOIN_DUNGEON_INVALID），永远不在队列里；
   模块无感知，300s 超时清理→重 spawn→再被拒，死循环。
2. bot 无归属：撮合池任意 5 个兼容项即成团，bot 会被别人队抢走/真人先填满。
3. `LfgAcceptAction`：proposal 到达时 bot 在战斗/死亡 → 拒绝 → 核心给 150s
   随机本冷却（RemoveProposal），之后连重新入队都被拒。
4. IN_QUEUE 300s 超时重摇，任何持续不兼容都会无限重试。

## 新设计（fresh 队列 = 装配模式；踢人补位 = 池模式）

- spawn 入口/2 分钟延迟门控不变（见 lfg-bot-queue-delay.md）。
- **装配模式**（poolMode=false，默认）：bot 登录后不进池。等级同步改为
  **精确等于玩家等级**（夹 [15, randomBotMaxLevel]），天赋仍按分配职责 roll；
  `SetRoles(bot, assignedRole)` 预置职责（rolecheck 秒答用），CombatStop，
  状态 → WAITING_ASSEMBLY 原地待命（有 master 会跟随玩家）。
- `TryAssembleGroup`（CheckAndCleanup 每 1s 轮询触发——2026-08-13 由 15s 提速，
  见下"错峰改造"；世界线程同步原子执行）：
  1. 玩家仍 QUEUED、无 DUNGEON_COOLDOWN/DESERTER 光环（有才等下轮，不动队列）；
     bot 有光环则清理换新鲜 bot；
  2. `LeaveLfg(player)` → 需要时 `new Group()+Create+AddGroup` → `AddMember(bot)`×N；
  3. `JoinLfg(player, roles|PLAYER_ROLE_LEADER, 原选择)` → 队伍进 ROLECHECK
     （LEADER 标志让核心把队长给真人）；
  4. **后置校验**：`GetState(gguid)==ROLECHECK` 才算成功，否则 TeardownAssembly
     回滚（清 bot、解散自建队、恢复玩家 solo 队列）。
- rolecheck：**无弹窗二次确认**——装配后模块用 LFG 系统已存职责同步代答
  （真人成员=首次入队确认的职责；bot=停驻时预置的职责；队长由 JoinLfg 应答并带
  LEADER 标志，代答时剥掉其他人的 LEADER 保证队长唯一）。代答是同步完成的，
  rolecheck 当场 FINISHED 入队；bot AI 稍后的重复应答因 RoleChecksStore 已删而
  自然落空。代答后校验 `GetState(gguid)==QUEUED`，否则 teardown 恢复 solo 队列。
- proposal：bot `LfgAcceptAction` 自动接受（新增：IsLfgBot 且活着 → CombatStop
  后强制接受，不再战斗拒绝；死亡仍拒绝）；真人自己点接受。
- `MakeNewGroup` 走 premade 分支 `ConvertToLFG(false)` 保留这个队
  （LFGMgr.cpp:1743），后续传送/兜底/问候/队长修正逻辑原样复用。
- **池模式**（poolMode=true，仅本内踢人补位 requeue）：替补 bot 仍 solo 入池，
  但入队列表改为**只排该 LFG 队伍正在打的那一个具体本**
  （`GetDungeon(GetGroup(player))`），不再用并集 —— 顺带修掉断点 1 对补位的影响。

## 状态机与兜底（bot 不会挂起）

- 新状态 `WAITING_ASSEMBLY`；`IN_QUEUE` 只剩池模式用。
- `m_assemblies[playerGuid]` 记录在途装配；玩家到 DUNGEON → 成功（清记录与失败数）；
  玩家 NONE（rolecheck/proposal 失败或被拒）→ 立即 teardown 不恢复队列；
  90s 看门狗（LFG_ASSEMBLY_TIMEOUT）→ teardown + 恢复 solo 队列。
- 每次 teardown `m_assemblyFailures`+1；≥3（LFG_ASSEMBLY_MAX_FAILURES）停止 spawn，
  玩家离开队列后重置。有失败记录的玩家跳过重排队后的 2 分钟延迟（不重复罚等）。
- bot 120s 停驻超时（LFG_BOT_PARK_TIMEOUT，有在途装配/已进 LFG 组时豁免）→ 清理腾位。
- 装配期间玩家被真人匹配走（PROPOSAL/DUNGEON）→ bot 逐个清理退场。

## 行为时序（单人排随机本）

- 0–120s：真人优先撮合（不变）；期间匹配成功则无 bot 介入。
- 120s 起：缺口入队 `m_pendingSpawns`，CheckAndCleanup 每 1s 消化 1 个 spawn、
  每 1s 最多 1 个 bot 做登录后重初始化（错峰防世界线程尖峰，2026-08-13 改造），
  整队约 8~12s 上线；装配 ≤1s 轮询粒度触发；rolecheck 由模块代答当场完成；
  满编队入队后**同一撮合 tick 必出 proposal** → 玩家点接受 → 进本。**最坏约 3 分钟**，
  只剩玩家点接受这一个手动环节。
- 真人自组 2–4 人队同样适用：补齐剩余坑位；真人队员**不会**收到第二次职责确认
  弹窗（模块用他们首次确认的职责代答）。

## 改动文件

- `modules/mod-playerbots/src/Bot/LfgGroupBotMgr.h/.cpp`：poolMode、WAITING_ASSEMBLY、
  LfgAssemblyInfo、TryAssembleGroup/TeardownAssembly/TransitionToInDungeon
  （进本问候+队长修正从 IN_QUEUE 分支抽出共用）、失败计数门控。
- `modules/mod-playerbots/src/Ai/Base/Actions/LfgActions.cpp`：LfgAcceptAction 两处
  战斗拒绝分支加 IsLfgBot 强制接受。
- 核心 LFGMgr/LFGQueue **零改动**（全部走公开 API）。

## 难度限制（2026-08-12 追加）

- **英雄难度不出 bot**：`OnPlayerQueueForLfg` 在 spawn 前检查玩家所选地下城，
  命中任一英雄项（`type == LFG_TYPE_HEROIC`、`difficulty == DUNGEON_DIFFICULTY_HEROIC`，
  或随机英雄本 ID 260/262 显式兜底）→ 直接返回，只有普通难度才生成陪打 bot。
  对踢人补位同样生效（英雄本踢人后重排队也不会补 bot）。

## 配套客户端修复（2026-08-12，实测发现的协议坑）

- 症状：装配组队成功后进本，客户端报 `GroupListResponse.LoadData` 越界
  （ReadUInt64 destination array too short），随后小眼睛/离开地下城按钮消失、
  LFG 主面板卡在"排队中"。
- 根因：装配出的 premade 经 `MakeNewGroup` 走 `ConvertToLFG(false)` → groupType
  只有 `GROUPTYPE_LFG(0x08)` 无 `LFG_RESTRICTED(0x04)`；服务端 `Group.cpp:1710`
  只要有 LFG 就写 5 字节 LFG 块（符合协议），而客户端
  `WTeamHandlerResponse.cs GroupListResponse.LoadData` 原要求双标志才读 →
  字段整体错位 5 字节崩溃。旧流程全是 solo 撮合（ConvertToLFG() 默认 restricted），
  双标志齐全所以从未暴露。
- 修复：客户端解析条件改为只判 `GROUPTYPE_LFG`（D:\Unity\clientproj
  ...\GameObjects\Player\Models\Response\WTeamHandlerResponse.cs）。

## 部署 / 验证 / 回滚

- modules static 编译，需重编 worldserver；**本地未编译验证**（遵循 AGENTS.md 不主动编译）。
- 验证：
  1. 单人排随机本 → 2 分钟内无 bot；之后看到 "parked for party assembly"×4 →
     "Assembling party" → "group queued as full premade" → 弹 proposal → 接受进本
     （全程无职责确认弹窗）。
  2. 排队期间有其他真人排不同随机类别 → 不影响（bot 不进池）。
  3. 本内踢 bot → 继续寻找队员 → 替补 bot 立即 spawn 且只排当前本。
  4. 玩家在 proposal 弹窗点拒绝 → bot 退场、队伍解散、玩家有 150s 冷却（原版行为）。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.* src/Ai/Base/Actions/LfgActions.cpp`

# LFG 单人队伍自动解散：修随机本掉线重登后前端状态死锁（2026-08-13）

> 症状：随机地下城打本途中掉线，重登后小眼睛不显示"进入副本"，随机本面板显示
> "离开队列"但点了没反应（退不出查找队列）。

## 根因链（逐行核实）

1. 掉线约 60s 后，4 个陪打 bot 因 master 失联被模块逐个 `RemoveMember` + 登出
   （LfgGroupBotMgr.cpp masterNullSince 60s，PlayerbotOperations.h BotLfgCleanupOperation
   先 RemoveMember 后登出）。
2. 核心原有"单人 LFG 组解散"逻辑（Group.cpp RemoveMember 尾部）带保留条件：
   仅当 `!mapId || !leader || (leader 活着且不在副本地图) || state==NONE` 才解散。
   条件判的是**队长**而非**最后剩的成员**——清理时序中队长指针可能落在刚移出组但
   还没登出的 bot 身上（在线、活着、在副本里）→ 条件全过 → **只剩离线真人的单人
   LFG 组被保留**。玩家重登快（清理进行中）的穿插时序同理可触发。
3. 玩家重登 → 登录发 `SMSG_GROUP_LIST`（CharacterHandler.cpp:962）→ 列表不含自己，
   memberCount=0 → 客户端 `WNetClient.OnGroupList`（WNetClient.cs:2758）把"0 成员"
   一律当"被踢/解散" → 清队伍 + `SetLfgDungeonId(0)` 重置 LfgInfo。
4. 地图加载完发 `CMSG_LFG_GET_STATUS` → 服务端看玩家仍在 LFG 组、state=DUNGEON →
   回 `SMSG_LFG_UPDATE_PARTY`(join=true) → 前端 `IsJoin=true, IsQueued=false,
   DungeonId=0` → 小眼睛隐藏（不显示进入副本）+ 面板"离开队列"发 `CMSG_LFG_LEAVE`
   → 服务端 `LeaveLfg` 对 DUNGEON 态组是 no-op 且不回包（LFGMgr.cpp:985-990）→
   永远退不出。死锁。

注：被踢/真解散的空 GROUP_LIST 特征为 groupType=0x10、LeaderGuid=0
（Group.cpp:600-602/797-800），与合法单人队伍包（带 LFG 块、LeaderGuid=自己）不同，
客户端靠 memberCount 区分是区分不开的。

## 修复（服务端一处，按用户拍板的 A 方案）

- `src/server/game/Groups/Group.cpp` RemoveMember 尾部：LFG 队伍剩 1 人时
  **无条件 `Disband()`**（删掉原 4 个保留条件），单人 LFG 组永不存续 → 客户端
  那个"0 成员误判"分支永远不会在 LFG 组上触发，客户端不用改不用发版。

## 解散后的通知链（已核实，前端处理正确）

- 在线的最后一人依次收到：
  1. `SMSG_LFG_UPDATE_PARTY`(REMOVED_FROM_QUEUE)（OnDisband→RemoveGroupData，
     LFGMgr.cpp:2676-2680，同时清服务端状态与已选地下城）→ 前端清 IsJoin/IsQueued；
  2. `SMSG_GROUP_DESTROYED`（Group.cpp:786）→ 清队伍 + SetLfgDungeonId(0)；
  3. 0x10 空 GROUP_LIST → 再清一遍（幂等）。
  另 `_homebindIfInstance` 把副本里的人标 m_InstanceValid=false → 标准 60s
  传回炉石流程。终态：未加入、未排队，可直接重排。
- 不在线的最后一人（本 bug 场景）：当时收不到包，但内存与 DB 已清净；重登后无组
  → 不发 GROUP_LIST；GET_STATUS 回 NONE + 空地下城列表 → IsHaveExtra=0 → 前端忽略
  → 干净初始状态。

## 影响面 / 取舍

- 真人 2 人打本走了一个时，剩下的不再能"原地等补位"（原 offer-continue 对单人
  不再出现），需重新排——本服有 2 分钟强制装配兜底，重排成本可接受（用户确认）。
- 最后一人不会被错误计逃兵：Disband 路径不触发 OnRemoveMember 的 deserter 判定。

## 已知残余（未修，防后续脱钩的加固项）

- 客户端 `OnLfgUpdatePlayerResponse` UPDATE_STATUS 分支无条件 `IsJoin=true`
  （WBattleMgr.Dungeon.cs:653-656），IsHaveExtra≠已加入，其它脱钩路径仍可能卡住。
- 服务端 `LeaveLfg` 对 NONE/DUNGEON 态静默 no-op 不回包，前端无自愈手段。
- 队列状态面板（RandomDungeonQueueStatusCtrl）无退出按钮。
- `LFGScripts.cpp:93` @todo：登录不主动推 LFG 状态，恢复全靠客户端 GET_STATUS。

## 部署 / 验证 / 回滚

- 核心改动，需重编 worldserver（本地未编译验证，遵循 AGENTS.md 约定）。
- 验证：
  1. 单人排随机本进本后杀进程 → 等 2 分钟以上（bot 清完、组已解散）→ 重登 →
     无队伍、面板"开始寻找"、可正常重排；
  2. 重登快于清理（60s 内）→ 组还在 → 小眼睛/传送正常；
  3. 本内把 bot 一个个踢掉 → 最后一个被踢时队伍直接解散、收到
     "你的队伍离开了地下城查找器队列。"、副本内成员走 60s 回城倒计时；
  4. 日志应见 `LFGScripts::OnDisband`（debug lfg）。
- 回滚：`git checkout -- src/server/game/Groups/Group.cpp`

## 回归与修正（2026-08-13 追加）

- **回归**：无条件解散把"打完尾王 bot 陆续退场"也覆盖了——最后一个 bot 离队时
  触发 `Disband()`，其 `_homebindIfInstance` 把还在本里的真人标成 `m_InstanceValid=false`
  → 60s 传送倒计时，玩家没捡到装备就被传出副本。
- **修正**：恢复条件解散，但判断对象从**队长**改为**最后剩下的成员本人**
  （原条件看队长才留下"清理时队长指针落在未登出 bot 身上"的逃生口）：
  `!lastMember || !mapId || state==NONE || (lastMember 活着且不在副本地图)` → 解散；
  在线且还在副本里的最后一人保留队伍（= retail 行为，可捡装备/交任务/自行离队）。
- 掉线场景依旧覆盖：最后一人不在线 → `!lastMember` → 解散。
- **已知残余（已于当日修复）**：队伍被保留时（在线在本里），客户端会收到 0 成员
  GROUP_LIST → 原前端误判会清掉 LfgInfo。**已修**：`WNetClient.OnGroupList`
  （WNetClient.cs:2751-2765）仅对 LFG 队伍增加区分——`IsLFGGroup && LeaderGuid != 0`
  的空列表是合法单人 LFG 队伍，正常走 UpdateTeamInfo + SetLfgDungeonId；
  真被踢/真解散的空列表（0x10 标记、LeaderGuid==0）维持原解散处理，非 LFG 队伍
  行为不变。C# 走热更下发。

# LFG 机器人延迟介入匹配（真人优先 2 分钟）（2026-08-10）

> 需求：真人排随机地下城时，先给真人之间撮合的机会；排队超过 2 分钟没匹配到真人，陪打 bot 才开始介入匹配。

## 实现方案

- bot spawn 的唯一入口是 `LfgGroupBotMgr::OnPlayerQueueForLfg`（由
  `RandomPlayerbotMgr::CheckLfgQueue` 每 30s 轮询、世界线程调用，仅当玩家/
  队伍处于 `LFG_STATE_QUEUED`）。延迟门控加在这里即可覆盖全部 spawn 路径。
- 排队起始时间不在模块里自行跟踪，而是复用核心队列数据：
  `LFGQueue::AddQueueData` 在入队时记录 `joinTime`（solo 以玩家 guid 为键，
  组队经职责检查后以 group guid 为键，LFGMgr.cpp:873 / 1573）。取消重排会
  重新 `AddQueueData`，joinTime 自然重置，无需模块侧清理任何状态。

## 改动内容

### 核心（src/server/game/DungeonFinding）

- `LFGMgr.h` / `LFGMgr.cpp`：新增公开访问器
  `time_t GetQueueJoinTime(ObjectGuid guid)`——状态非 `LFG_STATE_QUEUED`
  返回 0，否则返回 `GetQueue(guid).GetJoinTime(guid)`。
  注意调用时机必须在世界线程（与 `GetState`/`GetRoles` 相同约定）；
  先查状态再取 joinTime，避免 `QueueDataStore[guid]` 对不在队列的 guid
  默认插入脏条目。

### 模块（modules/mod-playerbots）

- `src/PlayerbotAIConfig.h/.cpp`：新增 `lfgSpawnBotQueueDelay`，配置键
  `AiPlayerbot.LfgSpawnBotQueueDelay`，默认 120（秒），0 = 立即 spawn（旧行为）。
- `src/Bot/LfgGroupBotMgr.cpp` `OnPlayerQueueForLfg`：在 `existingBots >= 4`
  检查后新增门控——`sLFGMgr->GetQueueJoinTime(队伍?group guid:玩家 guid)`，
  等待时长 < 延迟则 return（每 30s 轮询会再次进来）。
- `conf/playerbots.conf.dist`：补充 `AiPlayerbot.LfgSpawnBotQueueDelay = 120` 说明。

## 关键豁免：踢人补位（requeue）不走延迟

- 本内踢人 → "继续寻找队员"（`JoinLfgContinue`）会让队伍以**新 joinTime**
  重新进入 QUEUED（JoinLfg 重新 AddQueueData），若一刀切会白等 2 分钟。
- 判定：触发时玩家所在队伍 `group->isLFGGroup()` 为 true（只有 LFG 撮合进本
  后的队伍才有此标记，真人自发组队排队没有）→ 跳过延迟立即补 bot。
- 连带效果：打完随机本原班人马再排（已是 LFG 队伍）也立即补位，合理。

## 行为时序

- 0–120s：玩家正常参与真人撮合；期间匹配成功（离开 QUEUED 态）则全程无 bot。
- ≥120s 仍在 QUEUED：下一轮 `CheckLfgQueue`（≤30s 粒度）触发 spawn，实际
  介入时刻为 120–150s 之间。
- 日志：等待期每轮输出 `LFG: {} queued {}s (< {}s), waiting for real players`。

## 部署 / 验证 / 回滚

- 改了核心（LFGMgr）+ 模块（static 编译），worldserver 需整体重编译；本地未编译验证。
- 验证：
  1. 单人排随机本 → 2 分钟内日志只有 "waiting for real players"，无 spawn；
     超过 2 分钟出现 "Spawned bot ..." 并进本。
  2. 本内踢掉一个 bot → "继续寻找队员" → 替补 bot 立即 spawn（无 2 分钟等待）。
  3. `AiPlayerbot.LfgSpawnBotQueueDelay = 0` 时恢复旧的立即 spawn 行为。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.cpp src/PlayerbotAIConfig.* conf/playerbots.conf.dist`；
  核心 `git checkout -- src/server/game/DungeonFinding/LFGMgr.h src/server/game/DungeonFinding/LFGMgr.cpp`。

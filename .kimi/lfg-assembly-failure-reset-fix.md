# LFG 排 2 小时配不上 bot：两个静默根因修复（2026-08-19）

> 起因：排查"玩家排随机地下城 2 小时没匹配上 bot"。完整门控清单见排查结论，
> 本轮修复其中两个会造成**静默永久不匹配**的代码缺陷。

## 根因一：装配失败 3 次永久拉黑（m_assemblyFailures 重置失效）

- 设计口径（lfg-bot-force-assembly.md）：teardown 计 1 次失败，≥3（LFG_ASSEMBLY_MAX_FAILURES）
  停止 spawn，**玩家离开队列后重置**。
- 缺陷：重置分支（LfgGroupBotMgr.cpp `CheckAndCleanup` 装配段，
  `playerState != LFG_STATE_QUEUED → erase`）只对 `assemblyPlayers` 集合成员执行，
  而该集合只来自"仍有停驻/登录中 bot"和"有在途装配"两类玩家。teardown 会把 bot 全清、
  装配记录删掉 → 玩家一两秒内脱离集合 → 重置**永远不会触发**。累计 3 次失败
  （含玩家自己拒绝/超时 proposal 3 次：proposal 被拒 → 状态 NONE → teardown(false) 计数）
  后，该角色在服务器本次运行期间永远不再出 bot，手动退队重排也无效。
- **修复**：`CheckAndCleanup` 装配段构建 `assemblyPlayers` 时把 `m_assemblyFailures` 的 key
  一并加入，使"离开队列/离线 → 重置 failures+cooldown+战斗记账表"分支对带失败记录的玩家可达。
- 连带语义（符合文档原口径）：
  - 拒绝/超时 proposal → 被移出队列（NONE）→ 下一轮即重置；重排后按无失败记录处理，
    **重新走 2 分钟真人优先门控**（比修复前多等 2 分钟，换取不再永久拉黑）。
  - restoreQueue=true 的装配失败（看门狗/JoinLfg 被拒）玩家仍在队列 → 不重置 →
    仍跳过 2 分钟门控立即重试（不变）。
  - 仍在队列中且已锁定的玩家：每轮进 TryAssembleGroup 被失败门控挡回（开销可忽略），
    退队/掉线即解锁。

## 根因二：premade 队伍 processedGroups 去重先于队长判定

- 缺陷：`RandomPlayerbotMgr::CheckLfgQueue` 按 `players` 向量（登录顺序）迭代，
  `processedGroups` 去重 insert 发生在循环开头，而"是否队长"的跳过一个在
  `OnPlayerQueueForLfg` 里。**队长只要比任一队员晚登录（排在向量后面），队员每轮先把
  队伍标记为已处理、队长随后被去重跳过、非队长又不 spawn → 该队伍永远不补 bot**。
  单排不受影响（无 group）。
- **修复**：去重 claim 挪到 `queueingRealPlayers.push_back` 处，且仅队长生效——
  `processedGroups.insert(...)` 只在 `group->IsLeader(...)` 为真时求值（短路），
  队员不再占位。`LfgDungeons` 并集累积保持对所有排队成员执行
  （消费方仅 LfgActions.cpp 的随机 bot 进池选本，重复项只影响权重，语义无害）。

## 改动文件

- `modules/mod-playerbots/src/Bot/LfgGroupBotMgr.cpp`：assemblyPlayers 增加 m_assemblyFailures 来源。
- `modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp`：CheckLfgQueue 去重改为队长 claim。

## 部署 / 验证 / 回滚

- 模块 static 编译，需重编 worldserver；**本地未编译验证**（遵循 AGENTS.md 约定）。
  `python3 apps/codestyle/codestyle-cpp.py` 两个文件均通过（注意须用 python3，裸 python 过老）。
- 验证：
  1. 单排随机本 → 2 分钟后 bot 装配进本；进本前拒绝 proposal ×3 → 退队重排 →
     bot 恢复 spawn（修复前：永远不再出）。
  2. 两名真人组 premade，**队长后登录** → 排队 2 分钟后正常补满 bot
     （修复前：永远不补）。
  3. 装配看门狗失败累计 3 次锁定后，玩家退队再排 → 锁定解除。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.cpp src/Bot/RandomPlayerbotMgr.cpp`

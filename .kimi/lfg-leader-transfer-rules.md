# LFG 队伍队长转移规则（2026-08-11）

> 需求：1) 随机地下城队伍里，真人队长把队长转移给 bot 要禁止；
> 2) 转移给另一个真人时，队内所有 bot 的 master 要切到新队长（原来 bot 会一直
> 跟随旧队长——`UpdateAIGroupMaster` 只在 master 为空或非真人时才重新寻找）。

## 实现

### 核心（src/server/game）

- `Scripting/ScriptMgr.h` + `Scripting/ScriptDefines/PlayerbotsScript.cpp`：
  `PlayerbotScript` 新增 `OnPlayerbotCanChangeGroupLeader(Group*, Player*)` 钩子
  （默认 true，任一模块返回 false 即否决），与 `OnPlayerbotCheckLFGQueue` 同模式。
- `Handlers/GroupHandler.cpp` `HandleGroupSetLeaderOpcode`：`ChangeLeader` 前调钩子，
  被否决则静默 return（与该函数其它非法分支一致）。
  **只拦玩家手动转移**；核心内部 `Group::ChangeLeader`（含 LfgGroupBotMgr 自己的
  "队长是 bot 改给真人"修正、退队自动递补）不受影响——这是有意为之。

### 模块（modules/mod-playerbots/src/Script/Playerbots.cpp）

- `PlayerbotsScript::OnPlayerbotCanChangeGroupLeader`：LFG 队伍（`isLFGGroup()`）且
  新队长是 AI bot（有 botAI 且 `!IsRealPlayer()`）→ 否决。
- 新增 `PlayerbotsGroupScript : public GroupScript`（注册于 `AddPlayerbotsScripts`）：
  `OnChangeLeader` 里，LFG 队伍 + 新队长为真人 → 遍历成员，所有 AI bot
  `SetMaster(newLeader)`。不用重置策略，follow 走 formation→GetMaster 即时生效。
  注意核心传 hook 时 `m_leaderGuid` 已更新，oldLeaderGuid 参数不可信，实现未依赖它。

## 已知边界（未处理）

- 队长退队时核心 `RemoveMember` 自动递补队长可能选到 bot：bot 们仍会通过
  `FindNewMaster` 跟随剩余真人；队内无真人时由 LfgGroupBotMgr 的 master 失联
  60s 清理兜底。需要"自动递补优先真人"可后续再加。
- 非 LFG 的普通队伍（小号 bot 等）两条规则均不适用，行为不变。

## 部署 / 验证 / 回滚

- 改了核心 + 模块（static 编译），worldserver 需整体重编译；本地未编译验证。
- 验证：
  1. 随机本内真人队长点 bot"设为队长"→ 无反应（不生效）；
  2. 转移给另一名真人 → bot 们立即改跟新队长；
  3. 非 LFG 自发组队转移队长给 bot 不受影响。
- 回滚：核心 `git checkout -- src/server/game/Scripting/ScriptMgr.h src/server/game/Scripting/ScriptDefines/PlayerbotsScript.cpp src/server/game/Handlers/GroupHandler.cpp`；
  模块 `cd modules/mod-playerbots && git checkout -- src/Script/Playerbots.cpp`。

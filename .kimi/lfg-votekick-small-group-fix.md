# LFG 真人队长踢 bot 免投票直接出队（2026-08-12）

> 症状：随机本里只剩队长（真人）+ 1 个 bot 时，投票踢 bot 踢不掉。
> 根因：原版 AC 投票门槛 `LFG_GROUP_KICK_VOTES_NEEDED = 3` 硬编码（LFGMgr.h:55），
> 2 人队（发起者自动同意+被踢者自动反对，无其他投票人）永远凑不够 3 票，
> 挂到 60s 超时取消；3 人队（最多 2 票同意）同样永远踢不出去。
> **用户决策**：不改计票逻辑（曾实现多数决伸缩门槛，被回退），改为
> "LFG 队伍 + 真人队长 + 被踢对象是 bot → 不走投票，直接踢出"。

## 改动内容

核心（src/server/game）：

- `Scripting/ScriptMgr.h`：`PlayerbotScript` 新增虚钩子
  `OnPlayerbotLfgKickBypassVote(Group*, kicker, victim)`（默认 false =
  正常投票）；`ScriptMgr` 新增同名声明。
- `Scripting/ScriptDefines/PlayerbotsScript.cpp`：调度实现（任一脚本返回
  true 即跳过投票）。
- `Groups/Group.cpp` `RemoveMember` 的 LFG KICK 分支：钩子返回 true →
  `Player::RemoveFromGroup(this, guid, GROUP_REMOVEMETHOD_KICK_LFG)` 直接移除
  （与投票通过完全相同的效果：传出副本、无逃亡者惩罚、队长收到补位邀请）；
  否则照旧 `InitBoot` 走投票。

模块（modules/mod-playerbots）：

- `src/Script/Playerbots.cpp` `PlayerbotsScript` 实现钩子，三个条件全满足才跳过投票：
  1. `group->isLFGGroup()`（随机本队伍）；
  2. `kicker == 队长` 且为真人（无 botAI 或 IsRealPlayer）；
  3. `victim` 为 bot（有 botAI 且 !IsRealPlayer）。
  其余情况（非队长发起、踢真人、普通队伍）一律走原版投票。

## 后续链路（无需改动，已核实）

- bot 被踢 → `LFGScripts::OnRemoveMember`（KICK_LFG）传出副本 → 无队伍 →
  `LfgGroupBotMgr::CheckAndCleanup` 下轮 15s 轮询 "left dungeon/group, cleaning up"
  自动清理登出。
- 队长点"继续寻找队员"重排队 → isLFGGroup 跳过 2 分钟延迟立即补新 bot
  （见 lfg-bot-queue-delay.md）。

## 部署 / 验证 / 回滚

- 改了核心 + 模块（static 编译），worldserver 需整体重编译；本地未编译验证。
- 验证：
  1. 剩 1 真人队长 + 1 bot → 队长踢 bot → 无投票弹窗，bot 立即出队传出，
     ~15s 后自动登出；
  2. 5 人队踢 bot（队长发起）→ 同样直接踢出；
  3. 踢真人 / 非队长发起 → 仍走投票流程。
- 回滚：`git checkout -- src/server/game/Scripting/ScriptMgr.h src/server/game/Scripting/ScriptDefines/PlayerbotsScript.cpp src/server/game/Groups/Group.cpp`；
  `cd modules/mod-playerbots && git checkout -- src/Script/Playerbots.cpp`

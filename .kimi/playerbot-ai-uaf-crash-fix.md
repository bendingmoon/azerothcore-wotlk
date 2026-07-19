# Bot 地图线程自毁 UAF 修复（2026-07-18，第二起崩溃）

> 关联：与 [lfg-bot-cleanup-thread-fix.md](lfg-bot-cleanup-thread-fix.md) 同属"地图线程安全"系列。本文记录 `ActionNode::getContinuers` 野指针崩溃。

## 崩溃签名与判据

- 栈：PC = `0x0000000100000000`（野跳转），调用点 `ActionNode::getContinuers`（Action.h:125-127 的 `action->getContinuers()` 虚调用）← `Engine::DoNextAction`（Engine.cpp:215）← `PlayerbotAI::DoNextAction` ← `UpdateAIInternal` ← `OnPlayerAfterUpdate`（地图 worker）。
- 现场：动作刚 `ListenAndExecute` 成功返回，随后取 continuers 时虚表为垃圾 → **Action 对象在其 Execute() 期间被销毁**。
- 事发动作：`PartyCommandAction`（SMSG_PARTY_COMMAND_RESULT，PARTY_OP_LEAVE，离队者为主人）→ `Leave()` 返回 true。地图 530。

## 分析结论

- Action 对象由 `AiObjectContext`（每 AI 或共享）持有；`Engine::Reset/Init`、`ResetStrategies`、`PlayerbotAI::Reset()` 均**不**删 Action（已逐一核实）。Action 只随 **PlayerbotAI 销毁**而死。
- 登出拆除是同步的：`PlayerbotHolder::LogoutPlayerBot` → `WorldSession::LogoutPlayer(true)`（同步 teardown）+ `delete botWorldSessionPtr`（连带删 Player → `OnDestructPlayer` → `delete botAI`）。
- **根因**：`PlayerbotAI::UpdateAIInternal` 登出分支（PlayerbotAI.cpp:530-544，`isLogingOut()` 且满足条件时）在**地图线程**同步调用 `masterBotMgr->LogoutPlayerBot` / `sRandomPlayerbotMgr.LogoutPlayerBot` → 当场销毁 Player/AI/session，而 `Player::Update`/`OnPlayerAfterUpdate`/`UpdateAI` 仍在栈上，`_playerbotsAIMap` 也被地图线程并发 erase。

## 补丁（modules/mod-playerbots）

`src/Bot/PlayerbotAI.cpp` 一处：登出分支改为投递 `BotLogoutOperation` 到 `PlayerbotWorldThreadProcessor`（世界线程执行），立即 return。

- `BotLogoutOperation::Execute` 内部已区分随机 bot（`sRandomPlayerbotMgr.LogoutPlayerBot`）与账号 bot（master 的 `PlayerbotMgr`）两条路径，与原 if/else 语义等价。
- 所需头文件 `PlayerbotOperations.h`/`PlayerbotWorldThreadProcessor.h` 本已包含，无新增依赖。
- 重复投递无害：bot 已下线后第二个 op 会 `return false`。

## 排查中排除的路径（备查）

- 世界线程相位分离（`MapMgr.cpp:279 wait()`）覆盖：`BotLogoutOperation`、`LogoutAllBots`（OnPlayerbotLogout / CMSG_LOGOUT_REQUEST）、`HandleCommand "remove"`、`ProcessBot`、`DisablePlayerBot`（OnPlayerLogout 调用）。
- `ResetStrategies`/`Reset` 系调用不删 Action；共享 context 重建仅发生在 config reload（`PlayerbotAIConfig.cpp:704`）。
- 部署版本行号比本地 HEAD 旧 ~6 行：若服务器构建早于 `80b813d9`（2026-06-21"机器人自动同意踢出队友及离线退队BUG修复"），本崩溃可能已由该提交修复，升级即覆盖。

## 验证 / 部署

- 需重编译 worldserver（modules static）。
- 验证点：bot 触发即时登出（休息区/飞行/GM 权限）时，日志中登出应发生在世界线程（`PlayerbotWorldThreadProcessor` 处理 `BotLogout`），不再出现 `getContinuers` 崩溃。
- 若仍复现同类崩溃，采完整 core dump（`si_addr` + 全线程栈）继续定位。

# LFG 踢人补位重排队职责检查失败修复（2026-07-24）

> 症状：随机本内踢掉一名陪打 bot 后，点"继续寻找队员"（前端 `JoinLfgContinue`），
> 服务端职责检查失败，前端提示"加入失败，选择的职责组合不合法"
> （LFG_JOIN_FAILED + LFG_ROLECHECK_WRONG_ROLES）。

## 根因

- 组队重排队（`LFGMgr::JoinLfg` isContinue 分支，LFGMgr.cpp:830+）会发起**完整职责检查**，
  要求剩余全部成员（真人 + 3 个陪打 bot）重新确认职责，组合须满足 1坦/1奶/≤3DPS
  （`CheckGroupRoles`，LFGMgr.cpp:1621）。
- 陪打 bot spawn 时只按 `assignedRole` 筛**职业**（`GetPreferredClasses`，
  LfgGroupBotMgr.cpp:394），`PlayerbotFactory.Randomize` 随的**天赋专精随机**，
  不保证匹配职责（职业池重叠大：骑/德/战可坦可DPS，牧/萨可奶可DPS）。
- 初次入队 bot 直接用 `assignedRole` 发 CMSG_LFG_JOIN（LfgGroupBotMgr.cpp:587），没问题；
  但职责检查应答走 `LfgRoleCheckAction` → `LfgJoinAction::GetRoles()`（按天赋专精推导），
  与 `assignedRole` 口径不一致。典型翻车：玩家是 DPS，踢掉坦后，"奶" bot 是暗影牧/元素萨
  → 4 人全答 DPS → 超 3 DPS 上限 → WRONG_ROLES。

## 修复内容

### 服务端（modules/mod-playerbots）

- `src/Ai/Base/Actions/LfgActions.cpp` `LfgRoleCheckAction::Execute`：
  bot 应答职责检查时**优先用 LFGMgr 已存职责**（`sLFGMgr->GetRoles(bot->GetGUID())`，
  对陪打 bot 即当初入队的 `assignedRole`，与撮合组队口径一致），为 0 才回退天赋推导。

### 前端（D:\Unity\clientproj WBattleMgr.Dungeon.cs）

- 新增 `_isLfgContinueRequeue` 标记：`JoinLfgContinue` 发起时置 true（并清 IsOfferContinue）；
  重排队触发的职责检查 `INITIALITING` 不再弹职责确认框（队长职责已随入队包提交）。
- 标记清理点：`OnLfgJoinResultResponse`（任何入队结果）、职责检查 FINISHED 及
  MISSING_ROLE/WRONG_ROLES/ABORTED/NO_ROLE 四个失败分支、`ResetLfgInfo`。
- `OnLfgOfferContinueResponse`：Lfgdungeons 查表加 null 保护（原来直接 `.ID` 会 NRE），
  查不到表时发系统消息并 return。
- 新增 `_lfgInfo.IsJoiningQueue` 中间态（"正在加入队列..."，Lua 经 `GetLfgInfo()` 读取）：
  `JoinLfgContinue` 置位 + ShowTipsAndSystemMsg 提示 + UpdateLfgStatus；
  清除点：`OnLfgJoinResultResponse`、`OnLfgUpdatePartyResponse` 的
  ADDED_TO_QUEUE（成功，提示改为 ShowTipsAndSystemMsg）与 ROLECHECK_FAILED、`ResetLfgInfo`。
  成功权威信号 = SMSG_LFG_UPDATE_PARTY / LFG_UPDATETYPE_ADDED_TO_QUEUE（LFGMgr.cpp:1558）；
  队列中持续状态走周期性 SMSG_LFG_QUEUE_STATUS → `OnLfgQueueStatusResponse`。

## 遗留隐患（已于 2026-08-11 修复，见 lfg-bot-role-spec.md）

- ~~陪打 bot 的天赋/装备按随机专精随的，没按 `assignedRole` 配——"奶" bot 可能实际不会奶。~~
  已修：`Randomize`/`InitTalentsTree` 支持指定专精，spawn 时按 assignedRole 强制。

## 部署 / 验证 / 回滚

- modules 为 static 编译，需重新编译 worldserver 生效；前端为热更 C#。
- 验证：随机本内踢掉一个 bot → 点"继续寻找队员"→ 不再报职责错误，
  不弹职责确认框，服务端日志 bot 应答的 role 与 spawn 时 assignedRole 一致，
  随后替补 bot spawn 并入队传送进本。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Ai/Base/Actions/LfgActions.cpp`；
  前端还原 WBattleMgr.Dungeon.cs。

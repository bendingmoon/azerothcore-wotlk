# LFG bot 悄悄话骚扰 + 等待匹配期被随机传送修复（2026-07-21）

> 用户报告两个现象：1) 组队时 bot 老给真人发悄悄话；2) 排随机本等待匹配期间（如 2 个 bot 已就位等第 3 个），有 bot 会被传送到玩家身边。

## 问题一：bot 向真人玩家发悄悄话

bot 发悄悄话有两条路径，均需封堵：

1. **直发 `bot->Whisper(...)`**（约 25 处：SayAction、SendMailAction、TradeStatusAction、
   NewRpgAction、PlayerbotSecurity 等）——全部经过核心 `Player::Whisper`
   （`src/server/game/Entities/Player/Player.cpp:9461`），里面有脚本钩子
   `sScriptMgr->OnPlayerCanUseChat(...)`，返回 false 即中止发送。
2. **`PlayerbotAI::TellMasterNoFacing`**（PlayerbotAI.cpp:2970）——手工构
   `CHAT_MSG_WHISPER` 包 + `master->SendDirectMessage`，**绕过**上述钩子。
   组队刷屏（"Hello"、"I will there soon"、各种状态汇报）主要走这条。

**修复**：

- `modules/mod-playerbots/src/Script/Playerbots.cpp` `OnPlayerCanUseChat`：
  发送方有 botAI 且 `!IsRealPlayer()`（真 AI bot），接收方为真人
  （无 botAI 或 `IsRealPlayer()`）→ return false。真人挂机玩家（带 botAI 但
  IsRealPlayer）发悄悄话不受影响；bot→bot 悄悄话（RPG 模拟）不受影响。
- `modules/mod-playerbots/src/Bot/PlayerbotAI.cpp` `TellMasterNoFacing`：
  master 为真人时直接 return false（在 Say 兜底之后、IsTellAllowed 之前）。

注意：`PlayerbotAI.cpp:1010` 的 "debug " 命令回包（玩家显式发 debug 命令时按原
频道回复）未封堵——属于玩家主动索取的响应，且是调试工具。

## 问题二：等待匹配期间 bot 被传送到玩家身边

- LFG 陪打 bot 以**随机 bot** 身份登录（`LfgGroupBotMgr::LoginBot` →
  `AddPlayerBot(guid, 0)`），且等级被同步为玩家等级 ±5。
- `RandomPlayerbotMgr::ProcessBot`（RandomPlayerbotMgr.cpp:1613）周期性检查
  "teleport" 事件，到期即 `Refresh(bot) + RandomTeleportForLevel(bot)` ——
  按 bot 等级段随机选 grind/城市/RPG 落点。bot 等级=玩家等级 → 落点就在玩家
  练级区 → 表现为"传送到玩家身边"。死亡复活路径 `Revive` →
  `RandomTeleportGrindForLevel` 同理。
- 所有随机传送最终都汇入
  `RandomPlayerbotMgr::RandomTeleport(Player*, std::vector<WorldLocation>&, bool)`
  （RandomPlayerbotMgr.cpp:1641）——天然统一拦截点。

**修复**：

- `LfgGroupBotMgr` 新增 `bool IsLfgBot(ObjectGuid)`（h/cpp）：bot 被跟踪且
  state < TO_LOGOUT 即 true（带 m_mutex 短锁）。
- `RandomTeleport(Player*, locs, hearth)` 开头新增守卫：
  `if (sLfgGroupBotMgr.IsLfgBot(bot->GetGUID())) return;`
  覆盖 ForLevel / GrindForLevel / ForRpg / hearth 全部分支。

未改动：等待中 bot 的 "follow" 策略（跨图 MoveTo 基本失败、原地不动，不构成传送）
和 ProcessBot 的 randomize（同级重随装备，不换位置）。

## 部署 / 验证 / 回滚

- modules 为 static 编译，需重新编译 worldserver 生效（本次本地未编译验证）。
- 验证：
  1. 组队带 bot / 排随机本 → 玩家不再收到任何 bot 悄悄话（队伍频道、系统消息不受影响）。
  2. 排随机本长时间不匹配 → 日志不再有等待中 bot 的 "Random teleporting bot ..."；
     bot 原地等待直到匹配或超时清理。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Script/Playerbots.cpp src/Bot/PlayerbotAI.cpp src/Bot/LfgGroupBotMgr.cpp src/Bot/LfgGroupBotMgr.h src/Bot/RandomPlayerbotMgr.cpp`

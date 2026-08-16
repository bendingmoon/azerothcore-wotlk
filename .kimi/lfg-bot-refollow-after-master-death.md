# LFG bot 主人死亡后不再跟随修复（2026-08-16）

## 症状

随机本真人队长带 4 个陪打 bot，队长死过一次后 bot 永远站原地不跟随（战斗中照打，脱战后杵着）。

## 根因链路（逐行核实）

1. 队长点"释放灵魂" → 核心把 `CMSG_REPOP_REQUEST` 转发给所有以他为 master 的 bot
   （PlayerbotAI.cpp:175 masterIncomingPacketHandlers 注册 "release spirit"）。
2. 每个 bot 的常驻 "default" 策略（即 WorldPacketHandlerStrategy，StrategyContext.h:71）
   触发 `ReleaseSpiritAction::Execute`（ReleaseSpiritAction.cpp:19-32）：bot 活着且非战场
   → 说 "I am not dead, will wait here" + `ChangeStrategy("-follow,+stay", NON_COMBAT)`
   —— 集体摘掉跟随原地待命（此行为本身合理：防止 bot 跟幽灵主人乱跑引怪，予以保留）。
3. 唯一自动恢复路径：`ReviveFromCorpseAction::Execute`（ReviveFromCorpseAction.cpp:26-40）
   —— 仅当主人跑尸复活发 `CMSG_RECLAIM_CORPSE` 且 bot 距队长 ≤ farDistance
   （默认仅 20 码，PlayerbotAIConfig.cpp:93）时才 `+follow,-stay`（说 "Welcome back!"）。
4. 恢复失败情形（都常见）：主人被奶活（`CMSG_RESURRECT_RESPONSE`，根本不发 RECLAIM 包）；
   复活点离 bot > 20 码。之后全库再无任何自动加 follow 的路径（只剩聊天命令/表情/重新邀请）
   → 永久卡死。

## 修复内容

`modules/mod-playerbots/src/Bot/LfgGroupBotMgr.cpp` `CheckAndCleanup` IN_DUNGEON 分支
（1 秒轮询），在"master 失联 60s 清理"之后新增恢复兜底：

- 条件：`master && master->IsAlive() && bot->IsAlive()`
  && `HasStrategy("stay", NON_COMBAT)` && `!HasStrategy("follow", NON_COMBAT)`；
- 动作：`ChangeStrategy("+follow,-stay", NON_COMBAT)` + LOG_INFO
  （"master is alive again, resuming follow"）。
- 不限地图/距离：主人任何方式活过来 1 秒内恢复；距离由既有追赶传送兜底
  （FollowActions.cpp:219，>50 码直接传送，见 lfg-bot-follow-stuck-fix.md）。
- 主人仍是尸体/幽灵 → 不恢复（待命语义保留）；bot 自己死了 → 等救活后下轮恢复。

未动 ReleaseSpiritAction（待命行为保留）、未动 ReviveFromCorpseAction 的 20 码判定
（1s 轮询兜底已覆盖，老路径保留作秒级快通道）。只影响 LFG 陪打 bot，小号/随机 bot 不变。

## 附带确认的死 bot 链路（未修，记录在案）

- 副本内死 bot：`AutoReleaseSpiritAction::ShouldAutoRelease`（ReleaseSpiritAction.cpp:182-188）
  因"真人 master+队长同图+副本图"按设计不释放躺尸等救；核心 6 分钟强制释放在副本图
  被禁用（PlayerUpdates.cpp:358-359 `!GetMap()->Instanceable()`）；LFG 传送兜底只拉活 bot
  （LfgGroupBotMgr.cpp 原 1127 行 IsAlive 检查）。队长释放时转发的 REPOP 包会让死 bot
  跟着释放变幽灵跑尸，但跨图进本找尸体不可靠。若后续出现"团灭后 bot 回不来"再处理。

## 关键认知

- 玩家死亡状态机：Alive→JustDied→Corpse（KillPlayer Player.cpp:4479），释放后**仍是
  Corpse**（核心从不对玩家 setDeathState(Dead)，仅 Creature 有；Player.cpp:1055 FIXME
  亦提及）→ 幽灵主人在 `PartyMemberToResurrect` 里仍是合法复活目标（DeathState::Corpse
  匹配），奶妈 bot 会对跑尸中的幽灵主人读复活。
- 复活路径与客户端包：跑尸复活=CMSG_RECLAIM_CORPSE；接受复活术=CMSG_RESURRECT_RESPONSE；
  灵魂医者=gossip。三者只有第一种会被转发给 bot 触发恢复。
- master 包转发只发 registered opcode（PlayerbotAI.cpp masterIncomingPacketHandlers），
  且只发给以该玩家为 master 的 bot。

## 部署 / 验证

- modules static 编译，需重编 worldserver（本地未编译验证）。
- 验证：排随机本，队长送死一次后分别用 (1) 奶妈 bot 复活 (2) 跑尸复活（>20 码），
  观察 bot 是否在队长复活 1 秒内恢复跟随；日志应出现 "master is alive again, resuming follow"。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.cpp`

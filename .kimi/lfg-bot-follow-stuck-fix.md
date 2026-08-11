# LFG 陪打 bot 站着不动不跟随修复 + 进队问候 + master 失联清理（2026-08-11）

> 症状：随机本匹配的陪打 bot 有时候不跟着真人移动、站着不动。

## 根因（代码逐行核实）

1. **等待匹配期 bot 会打野怪**：bot 以随机 bot 身份单人排 LFG，`UpdateAIGroupMaster`
   （PlayerbotAI.cpp:461）因"无队伍+随机 bot"清掉 master 并重置策略 →
   `AiFactory::AddDefaultNonCombatStrategies` 按无队伍随机 bot 给 grind/new rpg
   （AiFactory.cpp:604 分支）；又 LFG 队列中 bot 强制激活（PlayerbotAI.cpp:4830）
   → 等待期间真实游走打怪。
2. **接受 proposal → 传送完成有窗口期**（真人点确认的几秒~几十秒），bot 仍在战斗。
3. **核心自动传送对战斗/死亡/坠落中的 bot 直接失败且无重试**：
   `LFGMgr::TeleportPlayer`（LFGMgr.cpp:2245-2263）只发错误包。
4. **被落下的 bot 永久杵在野外**：还在 LFG 队伍里，LfgGroupBotMgr 按队伍状态标
   IN_DUNGEON 不清理；master 在另一张图 → follow 的 ChaosFormation 返回空位置 →
   跟随永久无效；无 grind 策略（已组队）→ 原地不动整场。唯一恢复路径是真人再发
   CMSG_LFG_TELEPORT 中转（不会发生）。

## 改动内容（modules/mod-playerbots）

- `src/Bot/Factory/AiFactory.cpp`：`IsLfgBot` 的 bot 跳过 grind/new rpg/move random/
  start duel（等待期原地站岗，传送瞬间永不进战斗；bot 当 LFG 队长时也不再乱跑开怪）。
- `src/Bot/LfgGroupBotMgr.cpp` `CheckAndCleanup`：
  - IN_DUNGEON 且队伍 LFG 状态为 DUNGEON 时，bot 所在地图 ≠ 副本地图且存活/非战斗/
    非传送中 → 补发 `CMSG_LFG_TELEPORT(out=false)` 拉进本（15s 轮询兜底，任何原因
    落单都能自愈；死亡 bot 等其复活后下轮拉入）。
  - IN_DUNGEON 且 master 持续为空 > 60s → CleanupBot 登出清理（AI 每 tick 会重新
    寻找 master，持续为空 = 队里没真人了；60s 宽限覆盖传送/短暂重登）。
  - IN_QUEUE→IN_DUNGEON 转换时 `SayToParty("我是AI，竭诚为你服务！")`（每个 bot
    进队一次；只发给队内真人，不走被禁的悄悄话通道）。
- `src/Ai/Base/Actions/LfgActions.cpp`：`LfgLeaveAction` 对 IsLfgBot 直接 return
  （原来 "seldom" 随机触发器会让等待中的陪打 bot 自己退出队列）。
- `src/Ai/Base/Actions/FollowActions.cpp` `FollowAction::Execute`：LFG 陪打 bot 与
  master 同图、距离 > `AiPlayerbot.LfgBotCatchUpTeleportDistance`（默认 50 码，0 关闭）
  且双方非传送中、bot 存活且非战斗 → 直接 `TeleportTo` 到 master 位置（追赶传送；
  旧的距离传送代码在 MovementActions.cpp 里被注释禁用，此处只对陪打 bot 恢复）。
  配置见 `src/PlayerbotAIConfig.h/.cpp`、`conf/playerbots.conf.dist`。

## 已知残余（未修）

- 副本内跟不上 master 的其余原因：战斗/吃喝/拾取会暂停跟随（设计如此）；
  50 码以内的绕远路靠走路追赶（阈值可调小，但太小会在正常走廊跟随时频繁传送）。
- 等待期 bot 被野怪主动攻击进战斗 → 传送仍可能失败 → 由兜底传送在脱战后拉进本。

## 部署 / 验证 / 回滚

- modules 为 static 编译，需重新编译 worldserver 生效（本次本地未编译验证）。
- 验证：
  1. 排随机本，等待期观察 bot 原地站岗不打怪、不悄悄话；
  2. 匹配成功后 4 个 bot 全部进本，队伍频道各发一句"我是AI，竭诚为你服务！"；
  3. 本内踢人后补位 bot 正常进本跟随；
  4. 全员真人退队/离线超 60s → bot 自动登出清理（日志 "has no master for over 60s"）。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/Factory/AiFactory.cpp src/Bot/LfgGroupBotMgr.cpp src/Bot/LfgGroupBotMgr.h src/Ai/Base/Actions/LfgActions.cpp src/Ai/Base/Actions/FollowActions.cpp src/PlayerbotAIConfig.h src/PlayerbotAIConfig.cpp conf/playerbots.conf.dist`

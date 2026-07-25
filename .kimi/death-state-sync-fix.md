# 死亡状态前后端不同步修复（2026-07-25）

## 症状

玩家死亡后：卡死不弹释放灵魂框、无灵魂状态、左上角血条残留血量。

## 根因（两边判断逻辑差异）

服务端语义（无自定义改动，与上游一致）：

- 死亡瞬间 `Unit::setDeathState` → `SetHealth(0)`（Unit.cpp:11405），不设 GHOST flag。
- 释放灵魂 `Player::BuildPlayerRepop` → SMSG_PRE_RESURRECT + 幽灵 aura 8326（设 PLAYER_FLAGS_GHOST）+ **`SetHealth(1)`**（Player.cpp:4370）。
- 即：尸体 HP=0，灵魂 HP=1。

前端原逻辑的三处缺陷（WEntity.cs）：

1. `IsDead` 属性阈值 HP<=0，而 `UpdateAttrDead` 判死阈值 HP<=1 —— 灵魂态 HP=1 落在夹缝里，被认为"活着"。
2. `UpdateAttrDead` 判死后不直接置 `Attr.IsDead`，只发 `WEvent_Dead` 事件（且要求状态机就绪）；真正置标志+弹框在 `WDeadComponent.OnStart`。事件无重试，丢一次永久卡死。
3. `WEntity.Update` 的死亡事件兜底重发只对非玩家（`IsDead && !this.IsPlayer`），玩家被排除。

放大器：玩家开过 auto-pilot/挂机后挂 PlayerbotAI，死后 DeadStrategy 的 `AutoReleaseSpiritAction` 直接服务端调 `HandleRepopRequestOpcode` 抢先释放（HP 变 1），把客户端推进夹缝状态；之后客户端再发 CMSG_REPOP_REQUEST 被 MiscHandler.cpp:65 静默 return。

## 修复内容

前端 `D:\Unity\clientproj\Assets\HotUpdate\MoonClient\WEntity\WEntity.cs` + `WPlayer.cs`：

- **关键认知：活人 HP=1 合法存在**——决斗失败（Unit.cpp:1350）、牧师救赎之魂（HandleSpiritOfRedemption）、破釜沉舟类 aura 结束（SpellAuraEffects.cpp:4469）。区分依据是 PLAYER_FLAGS 的 GHOST 位（0x10，`PlayerFlags.PLAYER_FLAGS_GHOST`，WUnitFlags.cs:240）：尸体 HP=0 无 GHOST；灵魂 HP=1 有 GHOST；活人 HP=1 无 GHOST。
- `WEntity.HasGhostFlag` 新属性（267）：从 `_attr.Attrs[PLAYER_FLAGS]` 读 GHOST 位。
- `IsDead` 属性（275-281）：玩家 `HP<=0 || (HP<=1 && HasGhostFlag)`；非玩家 HP<=0。
- `UpdateAttrDead`（1245-1278）：玩家 `hp<=0 || (hp<=1 && hasGhost)` → isDead（直接置 `Attr.IsDead=true`；带 GHOST 时同时驱动 `InSoul=true` 补强），否则 isLive；GHOST 位优先取本次包里的 PLAYER_FLAGS，回退读已存 attr。
- `WEntity.Update` 兜底（1001）：`IsDead && (!IsPlayer || !ToPlayer.InSoul)`，未释放灵魂时每秒重发 `WEvent_Dead`；`!InSoul` 守卫必须保留，否则灵魂跑尸无法走 `Move()`。
- `WPlayer.InitPlayerDead`（917-937）：登录重建同样加 GHOST 判断（防止决斗败后小退再上误判成灵魂）。
- `InSoul` setter（WPlayer.cs:90）自带值变守卫，重复赋值安全。

服务端 mod-playerbots（对真人玩家 `botAI->IsRealPlayer()` 跳过死后自动行为，真 bot 不变）：

- `ReleaseSpiritAction.cpp` `AutoReleaseSpiritAction::isUseful()` 开头早退。
- `ReviveFromCorpseAction.cpp` `ReviveFromCorpseAction::Execute()` 与 `FindCorpseAction::isUseful()` 开头早退。

未动：`RepopAction`（卡死自救）、`SpiritHealerAction` 手动路径、`SelfResurrectAction`、引擎切换。

## 关键认知

- 前端灵魂状态 `InSoul` 不看 `PLAYER_FLAGS_GHOST`，只有两个来源：登录重建 `InitPlayerDead`、收 `SMSG_PRE_RESURRECT`（WEntityMgr.cs:354，有 `!IsDead → return` 守卫）。
- 左上角血条清零完全依赖 `Attr.IsDead` 的 setter；每帧推送条件是 `!IsDead`。
- `IsRealPlayer()`（PlayerbotAI.h:555-562）对 auto-pilot 真人返回 true，是模块内区分真人/真 bot 的标准写法。

## 待验证

实机跑一遍"死亡→弹框→释放→跑尸→复活"全流程（含开过挂机的情况）。

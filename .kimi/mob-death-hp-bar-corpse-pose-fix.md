# 怪物死亡卡血条 + 尸体重建站立修复（2026-08-24）

> 纯客户端修复（Unity 客户端 `D:\Unity\clientproj`），服务端零改动。
> 症状：①怪物死亡后血条不归0（卡在死亡前的血量）；②出副本再进后，部分怪物尸体不是躺着而是站着（死亡定格丢失）。

## 链路背景（排障先读）

- 判死：`WEntity.UpdateAttrDead`（WEntity.cs:1267）——非玩家 `HP<=0` 或 DYNAMIC_FLAGS 带 DEAD/LOOTABLE → `Attr.IsDead=true` + 发 `WEvent_Dead`（状态机不在 kStateDead 时）。创建块（出副本再进的全量重建）同样走 `UpdateAttrFields → UpdateAttrDead`，尸体创建当帧即判死。
- 服务端死亡只保证发 `HP=0`（Unit.cpp:11441 `SetHealth(0)`）；`UNIT_DYNFLAG_DEAD` 只有假死 aura 才设（SpellAuraEffects.cpp:3025），`LOOTABLE` 仅对有拾取的尸体（Unit.cpp:14240）——客户端判死的唯一可靠信号就是 HP=0。
- 血条推送：`WAttrComponent.UpdateHPSPToLua()`（WAttrComponent.cs:748）——`HPSPChanged` 守卫，推 `SetHealthBar`（悬浮血条）+ `WEvent_HP_Change` + Lua `SetTargetHP`（目标框）。调用点只有 `WEntity.Update` 一处。
- 死亡定格：`WDeadComponent.OnStart` 时模型未加载 → `_alreadyDead=true` 闭锁，`PlayAnim` 永远不再播任何动画；尸体姿势只剩两个断言口：`WEntity.OnModelLoaded`（WEntity.cs:1117）与 `WEntity.Update` 每秒兜底（WEntity.cs:1048），两者都调 `WAnimator.PlayLastFrame(STATE_DEAD)`。

## 根因

**① 卡血条**：`WEntity.Update` 的推送门控是 `!IsDead`。判死与 HP=0 同帧到达（UpdateAttrValue 先置 `HPSPChanged`，UpdateAttrDead 随后置 IsDead），门控把**最终的 0 血推送永久吞掉**——悬浮血条靠 `IsDeadHide` 藏了看不出来，但 Lua 目标框没有任何清零路径，永远停在死亡前的百分比。

**② 尸体站立（竞态）**：`M2RuntimeAnimator.PlayLastFrame` 在 `model/driver` 未就绪（`InitAnimator` 未完成）时 `GetAnimationSequence` 返回 null **静默丢请求**；而 `WAnimator.PlayLastFrame` 的回调是同步无条件触发的（WAnimator.cs:653-663），`SetPlayStateName("Death")` 照常说 `_playStateName="Death"` → `InState(STATE_DEAD)` 变 true → `WEntity.Update` 每秒兜底**停止重试**。之后 `InitAnimator` 跑 `PlayDefaultAnimations→PlayIdle`，尸体永久站 idle。 dungeon 重建时大量模型并发加载，加载慢的尸体（>1s）必踩——所以是"有些怪物"。

## 修复内容（均在客户端）

- `WEntity/WEntity.cs` `Update()`：推送门控 `!IsDead && Attr!=null` 改为 `Attr != null && (!IsDead || !IsPlayer)`。非玩家死亡后放行最终 HP=0 推送（HPSPChanged 守卫保证推完稳态零开销）；玩家保持原守卫（灵魂态 HP=1 不重复推送，玩家清零仍靠 IsDead setter 的 setHPPercent(0,0)）。
- `WAsyncObj/M2RuntimeAnimator.cs`：新增 `_pendingLastFrameId`；`PlayLastFrame` 在 `model==null || driver==null` 时登记待放而非静默丢弃；`InitAnimator` 完成时补放（其末尾 `++_baseVersion`+停 base 层保证先行 idle 被覆盖/作废，最终姿势与先后无关）。

## 关键认知

- `WAnimator.InState` 只看 `_playStateName` 簿记，与 M2 实际播放状态无关；簿记被乐观回调毒化后，所有"不在 Death 状态就补播"的兜底全部失效。本次修复把"请求必达"（不丢）做实，簿记才重新可信。
- `WAnimator.PlayLastFrame` 在 `_m2Animator==null` 时直接 return（不触发回调），此窗口无毒化，每秒兜底会一直重试到组件出现——只有"组件在但未 InitAnimator"的窗口才是致命窗口。
- `WDeadComponent._alreadyDead` 一旦闭锁，状态机层面永不补播，尸体姿势只能靠 PlayLastFrame 路径，别再指望状态机重放。
- 模型本身缺 Death 序列（seq==null，初始化完成后）仍无姿势可定格，属于数据局限，未处理。

## 验证清单

1. 编辑器编译 HotUpdate 程序集通过（本次改动未经编译）。
2. 打死怪看目标框/Lua 血条归 0；悬浮血条正常隐藏。
3. 副本内杀怪→出本→再进：所有尸体（含加载慢的大型怪）初始即躺地，无站立尸体。
4. 玩家死亡/释放/复活流程无回归（玩家推送守卫未动）。
5. 活体怪 idle/跑/技能动画无回归（PlayDefaultAnimations 与 pending 补放的覆盖顺序已验证：无论 idle 先播还是定格先落地，最终都是定格）。

# 技能特效寿命修复（特效该消失不消失 / 偶发错乱）（2026-07-27）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。

## 症状

技能释放特效有时候该消失了却一直不消失；偶发特效错乱（一个特效把另一个顶没）。

## 根因（三层错位）

特效只有两种死法：`WFxMgr.Update` 对 `DestroyTime>0` 倒计时，或 `DestroyFxBySpellId` 强杀（仅技能自然结束/`CastSkillFailed` 三处调用）。`DestroyTime` 在 `WFx.loadFinish` 里取 `M2RuntimeAnimator.CurrentClipLength` → 旧 Animation clip 总长 → 兜底 16s。改动动画管线（M2RuntimeAnimator 直接异步加载 `.m2anim.bytes`，无 AnimationClip）后这条取值链断了：

1. **时序错位**：`loadFinish` 在 `SpellObjects.Load` 协程末尾同步读 `CurrentClipLength`，但该值要等 spawn 序列（animId=0）经 `M2RuntimeAnimationManager.LoadAsync` 异步加载完才赋值（`M2RuntimeAnimator.InitSpellAnimator`）。缓存未命中（首次施放）时读到 0 → 16s 兜底 → 特效挂 16 秒；缓存命中才正常。这就是"有时候"不消失的来源。
2. **残留值不复位**：`CurrentClipLength` 是普通 auto-property，`ReleaseAllSequences()`（池回收）不清零。
3. **两套寿命互不通气**：非 loop 的 `SpellObjects` 自己有 anim0 播完→`TryDestroy`→播 159 消退→`ForceDestroy`→直接回池的自毁链，**不通知 WFx**。WFx 仍持有已回池（甚至已复用成别的特效）的 GameObject 引用，到期 `fx.Destroy()` 会误伤新特效。

## 改动文件

| 文件 | 改动 |
|------|------|
| `WEffect/Fx/WFx.cs` | `loadFinish` 里 M2 特效**不再读 `CurrentClipLength`**：有 `M2RuntimeAnimator` 即走自毁链，只留 16s 兜底（`FX_BACKSTOP_TIME`）；legacy Animation 路径保留；加载失败（gameObject 为 null）补 1s 兜底防 `_fxDict` 死记录；`LoadAsync` GLTF 分支把 `_fxId` 透传给 `WSpellEffectMgr.Load` |
| `WAsyncObj/M2RuntimeAnimator.cs` | `InitSpellAnimator` 入口与 `ReleaseAllSequences()` 均复位 `CurrentClipLength = 0` |
| `WoW/Components/SpellObjects.cs` | 新增 `OwnerFxId`（`OnRecycle` 清零）；`UpdateAnimation` 的 `ForceDestroy` 分支：`OwnerFxId>0` 时改走 `WFxMgr.DestroyFx(fxId)`，让 WFx 自己清理挂点/回调/spellId 映射，销毁统一收口 |
| `WModel3D/Mgr/WSpellEffectMgr.cs` | `Load` 增加 `ownerFxId = 0` 可选参数并写入 `SpellObjects.OwnerFxId`（唯一调用方是 `WFx.LoadAsync`） |

## 寿命流向（改后）

- 非 loop M2 特效（技能施放/命中/区域）：anim0 播完 → `TryDestroy` → 159 消退 → `ForceDestroy` → `WFxMgr.DestroyFx(OwnerFxId)` → `WFx.Destroy` → 回池。16s 兜底仅兜底。
- loop 特效（仅 `WBuff.cs` 用 `loop=true`，playTime=1000000）：不变，靠 buff/aura 移除销毁。
- 编辑器预制体（带 FxTimer / 旧 Animation clip）：不变。

## 遗留问题（未动，后续按优先级做）

1. **结束路径覆盖不全**：`DestroyFxBySpellId` 仍只有 3 个调用点（`WSkillComponent.OnUpdate` ×2 + `CastSkillFailed`）。`OnSkillEnd`（服务端通知结束）、`OnSkillAutoEnd`（新技能顶旧）、`OnSkillChannelUpdate(Time<=0)`（引导被打断）、`OnDetachFromHost` 都不销毁特效——这些场景下特效要等自毁链/兜底才消失（引导被打断后特效还会转几秒）。官方语义是 cast/channel 终止事件立即撤 kit。
2. **异步加载竞态**：`WFx.Destroy` 无法取消 GLTF 路径的加载（无任务句柄），特效在加载完成前被销毁时，迟到的 `loadFinish` 会在已回池/已复用的 WFx 上重建 GameObject → 孤儿特效永久残留。需要给 `WFx` 加销毁标志/代数校验，`loadFinish` 开头作废。
3. **spellId 映射粒度**：`_spellIdToFxIds` 按 spellId 全局索引不分施法者，A/B 两怪同技能会互相误杀；目标命中特效也挂在施法者 spellId 下。官方是 per-unit kit。
4. `HandleChannelEffect` 过滤 `StartEvent == 11 || EndEvent == 12` 是 OR（应为按官方语义区分"引导开始挂的 kit"与"引导结束撤的 kit"）。
5. `HandleSkillEffect` 把 `StartEvent==1||EndEvent==2` 全排除了，官方这类是"施法开始挂、施法结束撤"的持续 kit，等于持续型施法特效没播。

## 追加修复：怪物侧 debuff/被击特效残留（同日）

> 症状：玩家自身特效正常，怪物身上灼烧 debuff 已消失但火焰特效一直在（一团大火持续叠加）。

根因两条：

1. **被击特效无寿命、可叠加**：`WBeHitComponent.HandleImpactEffect`（StartEvent==6→EndEvent==13 的 impact 特效）在**每次伤害日志**（SpellNonMeleeDamageLogResponse）和**光环应用**（AuraUpdateResponse）时各触发一次，`FireTargetEffect(target, effects)` 不传 spellId（不注册映射）、playTime=0。部分 impact M2 没有 anim0 序列 → 自毁链不触发 → 每个吃 16s 兜底；连发火系技能时多个火焰特效叠在怪物身上，看起来就是"一直不消失"。
   修复：同技能 1s 内去重（`_lastImpactSpellId`/`_lastImpactTime`）；`FireTargetEffect` 显式传 `IMPACT_FX_DURATION = 2.5f` 确定短寿命（有 anim0 时自毁链仍会更早收掉）。
2. **`WBuff.StopFx` 用错管理器（确定的永久泄漏）**：buff 循环特效经 `WModel.CreateFx` 注册在 `WFxMgr._fxDict`，而 `StopFx` 用 `MFxMgr.DestroyFx` 销毁——两个独立单例各自记账、id 空间相同，销毁不到本尊（还可能误杀 MFxMgr 里同 id 的特效），随后又把 `_fxIdList[i]` 置 -1 丢追踪，loop 特效（playTime=1000000s）永久残留。触发路径：实体隐藏/恢复（`hideBuffFx`）、上坐骑。
   修复：`StopFx` 改回 `WFxMgr.singleton.DestroyFx`。

## 追加修复：死亡后特效残留 + 技能终止路径补销毁（同日）

> 症状：单位死亡后身上的施法/引导/buff 特效还在。

1. **死亡清理**：`WEvent_Dead`（WEntity 在 update-object 判死时立即触发，另有 1s 兜底重发，均幂等可重入）新增两个订阅：
   - `WSkillComponent.OnDead` → `StopAllSkill()`：终止当前施法/引导并销毁其特效（怪物读条/引导中被打死，施法光效不再留在尸体上）。
   - `WBuffComponent.onDead` → 遍历 `_buffDict` 调 `StopFx()`：只收视觉不动 buff 数据（数据等服务端光环移除包）。注意：经死亡清过的 buff 若实际持续到复活（极少数，如灵魂石类），其循环特效不会自动重播——已知的可接受取舍。
2. **技能终止路径统一补 `DestroyFxBySpellId`**（原"遗留问题 1"的大部分）：
   - `EndSkill`（服务端通知结束 `OnSkillEnd`、实体离场 `OnDetachFromHost`→`StopAllSkill`、死亡 `OnDead` 都经此）。
   - `OnSkillAutoEnd`（新技能顶替旧技能、状态自然结束的 AfterFinish；与自然结束路径重复销毁，幂等无害）。
   - `OnSkillChannelUpdate(Time<=0)`：引导结束/被打断立即收引导特效，不再等 TimeDuration 走完。
   至此 `WSkill.Cease` 的所有入口都有特效销毁；剩余未做的是"遗留问题 2"（GLTF 在途加载竞态产生孤儿特效）和"遗留问题 3"（spellId 映射不分施法者）。

## 追加修复：一次性特效"循环两遍"（同日）

> 症状：打在怪物身上的一次性特效（如灼烧命中火焰）会循环两遍才销毁。

根因：`SpellObjects` 自毁链是 anim0 播完 → `TryDestroy` → 播 159 消退 → `ForceDestroy` 销毁，但 `InitSpellAnimator` 同时在 layer 2 循环播放 `Loop_0`、layer 1 循环播放 158，**进入消退阶段后 Loop_0 没人停**，一直循环到对象销毁——视觉上就是火焰完整多跑了一遍。
修复：`SpellObjects.TryDestroy` 入口先调 `m2Animator.StopLoopLayer()`（新增，`driver.Stop(2)`）停掉循环层，只留 159 消退，符合官方"kit 结束→停持续循环→播 Death 序列→移除"的语义。
注意：若之后仍看到"放一次出两个特效"，那是 OnSkillGo 目标事件 + 伤害日志 impact 两条路径同播的数据问题，需要按具体法术排查（用户未反馈具体法术时暂不改）。

## 诊断日志（已于修复验证通过后全部撤除）

修复过程中用过一批 `[FxLife]` 临时日志定位问题（`WFx.Destroy` 定时器销毁、`SpellObjects.TryDestroy` 消退、`M2RuntimeAnimator.InitSpellAnimator` 无 anim0/加载失败、`WSpellEffectMgr.FireTargetEffect` 来源、`WBuff.PlayFx/Stop/StopFx` buff 特效生灭），验证通过后已全部撤除。若日后需再排查特效生命周期，可按相同点位恢复。

另注意一个链停摆条件：`M2RuntimeAnimator.CheckVisibility` 把离屏/超 100m 的 `animator.enabled` 置 false 后，`M2AnimationDriver.Update` early-return，`M2AnimationState` 不走时间，anim0 完成回调不会触发——离屏特效只能靠定时器。

## 追加修复：命中特效循环过多 + 到期硬切（同日）

> 症状：单发火球，怪物身上 moltenblast 命中火焰循环 4 次（5s 定时器硬切）。

诊断结论：moltenblast 有 anim0 序列（无"无 anim0"日志）但 anim0 本身很长（>5s），完成回调永远轮不到，Loop_0 循环层一直转。官方命中火焰是"爆发 ~1 闪即收"。

修复（通用，不针对单个 M2）：

- `WFx.TryStartDecay()` + `WFxMgr.Update`：定时器剩余寿命进入 `DECAY_WINDOW`（1.5s）时，先让 `SpellObjects.TryDestroy()` 播 159 消退再销毁，循环动画不再被硬切；消退比窗口长时仍由定时器硬杀兜底。
- `_decayEnabled`：初始寿命 ≤1.5s 的短特效（如施法者 0.667s 的 M2）不触发，避免跳过爆发直接播消退；`Init` 和 `loadFinish`（16s 兜底/legacy 时长赋值处）都会设置。
- `SpellObjects._inDecay`：TryDestroy 防重入（anim0 完成回调 vs 定时器消退触发竞态），`OnRecycle` 复位。
- `IMPACT_FX_DURATION` 5s → 2.5s（官方命中一闪即收；链正常的 M2 会更早自毁）。

验证日志确认过的事实：火球命中只有一发（IsBullet 守卫 + DoT 去重生效）；"一直循环"是霜甲术（6136）被怪近战触发反弹，每次触发的特效生命周期完整。

## 追加修复：引导法术手部特效提前消失（同日）

> 症状：暴风雪引导 8 秒，手上特效 1~2.5 秒就消失（ice_precast_med_hand 按 playTime=2.5 被收，ice_precast_low_hand.prefab 自带 FxTimer ~1.5s）。

根因：`HandleChannelEffect` 用 `playTime=0` 放引导特效，寿命与引导时长完全脱钩。官方语义：引导 kit（StartEvent==11）引导开始装上、引导结束（EndEvent==12）才撤。
修复：`HandleChannelEffect` 增加 duration 参数（`OnSkillChannelStart` 传 `_skill.TimeDuration`），`FireTargetEffect`→`Fx`→`ProcessFx`→`PlayFx` 全链透传 `playTime=duration` + 新增 `loop=true`（不走 anim0 自毁链，同 buff 特效模式）；引导结束/打断由之前加的 `OnSkillChannelUpdate(0)`→`DestroyFxBySpellId` 即时收掉；编辑器预制体通道特效（FxTimer 1.5s）因 playTime 非默认不再被 FxTimer 截短，也能活满引导。

另发现（数据侧，未改）：`clearcasting_impact_chest`、`decisivestrike_impact_chest` 等 M2 缺 `0_x.m2anim.bytes`（日志"anim0 序列加载失败"），自毁链不会触发，靠 2.5s 定时器+消退窗口收场；要正确的爆发动画需重烘焙这些 M2 的序列。另注：`SpellObjects.TryDestroy` 的"anim0 播完进入消退"日志现在也会由消退窗口触发（不区分来源，临时日志无所谓）。

**闭环验证（单发火球）**：怪物侧仅一发 moltenblast，创建 1s 后由消退窗口触发 `TryDestroy`（日志"anim0 播完进入消退"），自然淡出无循环；无 `BuffLoopFx 创建` 日志 → 火球 DoT 未配 buff 循环特效，"debuff 假说"排除。玩家侧火焰一次即消（施法者 kit 自带 ~1s 显式寿命）。临时 `[FxLife]` 日志待用户确认视觉效果后撤除。

# M2 动画被抢后定格修复（idle/持械姿势奔跑、宠物之眼以 idle 跑/朝向卡死）（2026-08-21）

> 纯客户端修复（Unity 客户端 `D:\Unity\clientproj`），服务端零改动。
> 症状：①玩家奔跑中"突然执行持械动作"，之后就以 idle/持械姿势跑，必须停下再跑才恢复；②猎人野兽之眼控制宠物，宠物以 idle 动作滑步；③野兽之眼控制后宠物模型朝向卡死（保持控制前的旧朝向，横着/朝后滑步），位移正常但模型不转向。

## 链路架构（排障先读）

- **单位动画组件是 `M2RuntimeAnimator`**（`WAsyncObj/M2RuntimeAnimator.cs`，挂在 mesh 上，`ModelRenderer.cs:628` 创建），底层 `M2AnimationDriver`（`Assets/UniTaskAOT/AnimationRuntime/`，PlayableGraph+AnimationJob 采样 .m2anim）。**`WAsyncObj/M2Animator.cs`（Animancer 版）是废弃代码**，别再往那里改。
- 门面：`WModel.Ator` = `WAnimator`（`WAsyncObj/WAnimator.cs`），`Play("Run")` → `WSkillDataMgr.GetAnimationIdByName` → `M2RuntimeAnimator.Play(animId, loop, speed)`。
- 层模型：layer 0 = base（跑/走/待机/持械姿态循环），layer 1 = action（攻击/跳跃等一次性动作），layer 2 = 模型自带 Loop。一次性动作经 `PlayActionFullBody` 把 base 层也抢去播同一动作，播完 onEnd 无条件 `PlayIdle`/`PlayBaseLoop(持械)`。
- 移动动画触发：**只在 `WMoveComponent.SetMoving` 的 `_isMoving` 边沿触发一次**（`WMoveComponent.cs:150` → `Machine.Current.PlayAnim(null)`），持续移动期间无任何重试/矫正；`MStateMachine.LateUpdate` 只在 `MarkTriggered` 时重播。
- 控制模式（野兽之眼/心控/象棋）：`WPlayerInfo.ControlMode.cs`，possess 系摇杆经 `player.ModelFarSight = 被控单位模型` 驱动——`WEntity.Model`/`VehicleOrModel` 均重定向到被控模型（`WEntity.cs:164-172`），位移/朝向/动画都落在被控单位身上，但**发动作的是玩家实体的状态机**。

## 根因

1. **base 层被一次性动作抢走无人补回**：奔跑中放瞬发技能/被击闪避/跳跃落地 → `PlayActionFullBody` 抢 base 层 → 播完无条件回 idle/持械 → `_isMoving` 恒 true 不再触发 SetMoving → 定格。停止+重跑的边沿才重新播 Run。
2. **被控单位两个写入方打架**：玩家侧（边沿触发一次 Run）+ 被控单位自身状态机（按自己的 `IsMoving=false` 算 StateName=Idle，任何 `MarkTriggered` 都重播 Idle 盖掉 Run）。
3. **被控单位朝向被自身 LagSyncUpdate 每帧顶回旧方向**：`SetIsSyncPos(false)` 只挡 `Position` 写入（`MGameObject.cs:141`），`Rotation` 无门控（`:153`）；possess 期间玩家驱动把宠物推离残留的服务端 `_targetPos` >0.1m，宠物自己的 `LagSyncUpdate` 误判"没走到"→ `SetMoving(true)` → 每帧 `_entity.Rotation = _toTargetQuat`（最后一次服务端移动的旧朝向）顶掉玩家侧 `PlayerUpdate` 写的 `Forward`；位置写入被吞所以永远"到不了"，循环永不终止。（已核实服务端不回显被控单位移动包给控制者：`MovementHandler.cpp:395` `SendMessageToSet(&data, _player)`，排除回包顶朝向的嫌疑。）

## 修改文件清单（均在客户端 `Assets/HotUpdate/MoonClient/`）

- `WComponents/Action/WDefaultActionComponent.cs` — `OnUpdate` 加 0.2s 周期矫正 `RepairMovingAnim()`（移动中重新断言 StateName；稳态走 `M2RuntimeAnimator` 去重直接返回，开销可忽略；动作层有一次性动作在播则让位；骑乘/载具/尸体不干预）；`PlayAnim` 入口加 `IsControlledByLocalPlayer` 门控（被控单位自身状态机不再播）。
- `WComponents/Common/WMoveComponent.cs:158` — `SetMoving` 动画播放加 `!IsControlledByLocalPlayer` 门控（`_isMoving` 簿记照常更新，退出控制无缝恢复）。
- `WComponents/Common/WMoveComponent.cs` `Update` 开头 — 被控单位直接 return：自身 LagSyncUpdate 不再用残留服务端朝向顶掉玩家驱动的 Forward（修症状③）。
- `WEntity/WEntity.cs:158` — 新增 `IsControlledByLocalPlayer`（`WPlayerInfo.IsControlMode && ControlGuid == UID`）。
- `WAsyncObj/WAnimator.cs:122` — 新增 `IsActionPlaying` / `IsBaseOneShotPlaying` 透传。
- `WAsyncObj/M2RuntimeAnimator.cs:66` — 新增 `IsActionPlaying`（在途判定 + driver layer 1 `IsAnyStatePlaying`，含暂停 pose，死亡定格不会被矫正复活）与 `IsBaseOneShotPlaying`（layer 0 非循环在播）。

## 已知边界/遗留

- 被抢后最多 0.2s 姿势不对（矫正间隔），接受。
- 移动中放技能：一次性全身动作在播期间矫正让位，动作播完才拉回 Run——行为变为"移动中动作能播完"，与官方端"移动优先"略有差异，实测观察手感。
- **矫正让位必须覆盖"在途"判定**（回归修复）：初版只查 layer 1 在播状态，跳跃(JumpStart 等非 base 动画)异步加载在途时层上还是空的，矫正的 Play 会经 `OnPlayActionEnd` 递增 `_actionVersion` 把在途加载作废（跳跃被吞/只播个开头）。`IsActionPlaying` 现含在途判定（`currentAnimId` 指向非 base 动画即视为忙），另加 `IsBaseOneShotPlaying` 覆盖 base 层一次性动画。
- 备选治本方案（未做）：`M2RuntimeAnimator` 加 `BaseAnimResolver` 回调，onEnd 时问实体该播什么——需管理池化 WAnimator/ModelFarSight 换模型的生命周期，且不覆盖宠物双写入方场景，故未选。

## 验证清单

1. 编辑器编译 HotUpdate 程序集通过（本次改动未经编译，需先过编译）。
2. 奔跑中放瞬发技能/跳跃落地：0.2s 内自动恢复奔跑动作。
3. 移动中技能动作能播完不被立刻掐断。
4. 野兽之眼控制宠物：前进/停止/转向正常（模型朝移动方向）；宠物进战斗后不被盖回 idle；控制前宠物朝向不正（横着/朝后）时启动控制并移动，朝向应立即纠正为移动方向。
5. 死亡/尸体、骑乘状态动画无异常回归。

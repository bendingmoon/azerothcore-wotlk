# M2 动画被抢后定格修复（idle/持械姿势奔跑、宠物之眼以 idle 跑）（2026-08-21）

> 纯客户端修复（Unity 客户端 `D:\Unity\clientproj`），服务端零改动。
> 症状：①玩家奔跑中"突然执行持械动作"，之后就以 idle/持械姿势跑，必须停下再跑才恢复；②猎人野兽之眼控制宠物，宠物头朝后以 idle 动作滑步跑，朝向与奔跑动作都不对。

## 链路架构（排障先读）

- **单位动画组件是 `M2RuntimeAnimator`**（`WAsyncObj/M2RuntimeAnimator.cs`，挂在 mesh 上，`ModelRenderer.cs:628` 创建），底层 `M2AnimationDriver`（`Assets/UniTaskAOT/AnimationRuntime/`，PlayableGraph+AnimationJob 采样 .m2anim）。**`WAsyncObj/M2Animator.cs`（Animancer 版）是废弃代码**，别再往那里改。
- 门面：`WModel.Ator` = `WAnimator`（`WAsyncObj/WAnimator.cs`），`Play("Run")` → `WSkillDataMgr.GetAnimationIdByName` → `M2RuntimeAnimator.Play(animId, loop, speed)`。
- 层模型：layer 0 = base（跑/走/待机/持械姿态循环），layer 1 = action（攻击/跳跃等一次性动作），layer 2 = 模型自带 Loop。一次性动作经 `PlayActionFullBody` 把 base 层也抢去播同一动作，播完 onEnd 无条件 `PlayIdle`/`PlayBaseLoop(持械)`。
- 移动动画触发：**只在 `WMoveComponent.SetMoving` 的 `_isMoving` 边沿触发一次**（`WMoveComponent.cs:150` → `Machine.Current.PlayAnim(null)`），持续移动期间无任何重试/矫正；`MStateMachine.LateUpdate` 只在 `MarkTriggered` 时重播。
- 控制模式（野兽之眼/心控/象棋）：`WPlayerInfo.ControlMode.cs`，possess 系摇杆经 `player.ModelFarSight = 被控单位模型` 驱动——`WEntity.Model`/`VehicleOrModel` 均重定向到被控模型（`WEntity.cs:164-172`），位移/朝向/动画都落在被控单位身上，但**发动作的是玩家实体的状态机**。

## 根因

1. **base 层被一次性动作抢走无人补回**：奔跑中放瞬发技能/被击闪避/跳跃落地 → `PlayActionFullBody` 抢 base 层 → 播完无条件回 idle/持械 → `_isMoving` 恒 true 不再触发 SetMoving → 定格。停止+重跑的边沿才重新播 Run。
2. **被控单位两个写入方打架**：玩家侧（边沿触发一次 Run）+ 被控单位自身状态机（按自己的 `IsMoving=false` 算 StateName=Idle，任何 `MarkTriggered` 都重播 Idle 盖掉 Run）。

## 修改文件清单（均在客户端 `Assets/HotUpdate/MoonClient/`）

- `WComponents/Action/WDefaultActionComponent.cs` — `OnUpdate` 加 0.2s 周期矫正 `RepairMovingAnim()`（移动中重新断言 StateName；稳态走 `M2RuntimeAnimator` 去重直接返回，开销可忽略；动作层有一次性动作在播则让位；骑乘/载具/尸体不干预）；`PlayAnim` 入口加 `IsControlledByLocalPlayer` 门控（被控单位自身状态机不再播）。
- `WComponents/Common/WMoveComponent.cs:158` — `SetMoving` 动画播放加 `!IsControlledByLocalPlayer` 门控（`_isMoving` 簿记照常更新，退出控制无缝恢复）。
- `WEntity/WEntity.cs:158` — 新增 `IsControlledByLocalPlayer`（`WPlayerInfo.IsControlMode && ControlGuid == UID`）。
- `WAsyncObj/WAnimator.cs:122` — 新增 `IsActionPlaying` 透传。
- `WAsyncObj/M2RuntimeAnimator.cs:66` — 新增 `IsActionPlaying`（driver layer 1 `IsAnyStatePlaying`，含暂停 pose，死亡定格不会被矫正复活）。

## 已知边界/遗留

- 被抢后最多 0.2s 姿势不对（矫正间隔），接受。
- 移动中放技能：一次性全身动作在播期间矫正让位，动作播完才拉回 Run——行为变为"移动中动作能播完"，与官方端"移动优先"略有差异，实测观察手感。
- 宠物"头朝后"若仍复现，则查 `MGameObject.Forward` 的 ±90° 修正对该模型是否适用（本次未动朝向逻辑，驱动期间朝向由玩家 `PlayerUpdate` 每帧写 `Forward`）。
- 被控单位死亡动画不走 `WDefaultActionComponent.PlayAnim`（走 `WDeadComponent`），不受门控影响。
- 备选治本方案（未做）：`M2RuntimeAnimator` 加 `BaseAnimResolver` 回调，onEnd 时问实体该播什么——需管理池化 WAnimator/ModelFarSight 换模型的生命周期，且不覆盖宠物双写入方场景，故未选。

## 验证清单

1. 编辑器编译 HotUpdate 程序集通过（本次改动未经编译，需先过编译）。
2. 奔跑中放瞬发技能/跳跃落地：0.2s 内自动恢复奔跑动作。
3. 移动中技能动作能播完不被立刻掐断。
4. 野兽之眼控制宠物：前进/停止/转向正常；宠物进战斗后不被盖回 idle。
5. 死亡/尸体、骑乘状态动画无异常回归。

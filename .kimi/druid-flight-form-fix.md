# 德鲁伊飞行形态支持修复（2026-08-12）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。三个问题：姿态技能缺拦截 → 变身后不能起飞 → 飞行中前进/后退被吞。

## 关键 ID（WotLK 3.3.5a，已逐条核实）

- `33943` 飞行形态（Flight Form）→ 形态 `FORM_FLIGHT = 0x1D`
- `40120` 迅捷飞行形态（Swift Flight Form）→ 形态 `FORM_FLIGHT_EPIC = 0x1B`
- 两技能 Effect #3 均为 `Apply Aura: Can Fly`（SPELL_AURA_FLY，aura 201）——aowow 数据库核实

## 服务端链路（无需改动，仅作依据）

- `SPELL_AURA_FLY` → `AuraEffect::HandleAuraAllowFlight`（SpellAuraEffects.cpp:3442）→ `Unit::SetCanFly(true)` → 对客户端控制玩家直发 `SMSG_MOVE_SET_CAN_FLY`（Unit.cpp:16610）
- 客户端 `WMoveRegister.MoveSetCanFlyResponse` 已处理该包 → `WEntity.CanFly = true`（变身瞬间即就位）
- 反作弊 `WorldSession.cpp:1140` 只剥无 fly 光环者的 FLYING/CAN_FLY 标志，德鲁伊有光环不受影响
- 鸟点 taxi 全程不对玩家 SetCanFly → 用 CanFly 判定不会在鸟点误触发
- 注意：Unity 客户端从不回 `CMSG_MOVE_SET_CAN_FLY_ACK`，服务端 `_pendingFlightChangeCounter` 不清零但无副作用（传送/登录时按 aura 复核，有光环即保留）

## 修复 1：姿态技能拦截缺失（WSkillDataMgr.cs）

- `InitAttitudeSkill()`（:1552）`_attitudeSkills` 列表加入 33943/40120（不加则 `GetAttitudeSkills()` 过滤掉，`DoCastAttitudeSkill` 开头即 return false，拦截进不去）
- `DoCastAttitudeSkill()`（:1652 起）按原有模式补两个拦截块：`FORM_FLIGHT` → `WCancelAuraRequest(33943)`，`FORM_FLIGHT_EPIC` → `WCancelAuraRequest(40120)`；再按一次只取消变身不重复施放

## 修复 2：变身后不能起飞（WMoveComponent.cs）

根因：`WEntity.IsFly`（WEntity.cs:254）= `IsRideVehicle && VehicleCtrlComp.IsFlyVehicle`，只认飞行坐骑；德鲁伊是变身非骑乘，`IsFly` 恒 false，飞行逻辑全部进不去。

修复：新增私有属性并替换文件内全部 6 处 `_entity.IsFly` 判定（不可移动豁免/跳跃起飞/上下键升降/状态进出/水面钳制）：

```csharp
// WMoveComponent.cs:109
private bool HasFlightAbility { get { return _entity.IsFly || _entity.CanFly; } }
```

刻意不改全局 `IsFly`（15+ 调用点含 Lua），影响面只限移动组件。坐骑时 IsFly/CanFly 同 true，行为不变。

## 修复 3：飞行中前进/后退被吞（WPlayer.cs + WMoveComponent.cs + WNavigationMgr.cs）

根因：摇杆输入门槛 `MVirtualTab.cs:249` 为 `!InJumping || IsFly`；而 `WPlayer.UpdateVertical`（站立重力检测，:639）每 2s 发现 `!isGrounded` 就置 `InJumping = true`，悬停永不落地 → 德鲁伊（IsFly=false）摇杆被吞。上下/空格走 `TriggerUpDown/TriggerJump` 无门槛所以正常。坐骑悬停同样误置但靠 IsFly 放行，故坐骑没事。

- `WPlayer.cs:656`：检测加 `&& !IsFly && !CanFly`（根因修复：合法悬空不再误判为跳跃/下落）
- `WMoveComponent.cs:624`：`Ground→MountedGround` 切换时清 `_entity.InJumping = false`（覆盖跳跃中/空中变身边界）
- `WNavigationMgr.cs:257`（CheckGroundLoaded）：高空 2000m 探测特例从 `IsFly` 扩到 `IsFly || CanFly`——否则德鲁伊高空 26.6m 探测打不到地面，`MoveModel`（WEntity.cs:2371）会把所有移动清零

取消形态 → `SMSG_MOVE_UNSET_CAN_FLY` → CanFly=false → 恢复重力正常下落，行为不变。

## 待验证

- 变身→空格起飞→悬停几秒→摇杆前后左右；高空飞行；近地自动着陆；空中取消形态下落
- 若变身按空格无反应，先查客户端日志是否收到 `SMSG_MOVE_SET_CAN_FLY`（`MoveSetCanFlyResponse`），没收到则查服务端光环

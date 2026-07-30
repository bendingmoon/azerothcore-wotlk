# 死亡卡死防御性修复（2026-07-29）

## 背景

接 `death-state-sync-fix.md`（2026-07-25）之后仍偶发：玩家死后不弹"释放灵魂"框、左上角血条残留血量（如 228/3965）、倒地动画正常。复现困难，按可疑断点做防御性修复。

## 分析结论（三症状单一根因）

`WAttrComponent.IsDead` setter（WAttrComponent.cs:76）原顺序：先 `Model.SwitchToSoul()` / `RoleHudCom.IsDeadHide()`（均可空），**最后**才 `_isDead = value` 和 `setHPPercent(0,0)` 清血条。死亡瞬间模型/HUD 未就绪 → NRE → 标志永不置位、血条永不清零；异常穿透 FireEvent 炸穿 `WDeadComponent.OnStart`（弹框在置标志之后）→ 按钮不弹；状态机已先入 kStateDead → Update() 兜底只循环补播倒地动画。

## 修复内容

### 客户端（D:\Unity\clientproj）

- `WComponents/Attribute/WAttrComponent.cs` IsDead setter：`_isDead=value` + 清血条**提到最前**（血条清零加 Player null 判），模型/HUD 操作整体 try-catch + 逐级空判（Model/RoleHudCom/CreatureHealthBarComp/NpcHealthBarComp/PetBarComp）。
- `WEntity/WEntity.cs` `UpdateAttrValue`：改名 `UpdateAttrValueInternal`，新增同名 try-catch 包装（原函数无保护，异常会阻断 `UpdateAttrFields` 后续的 `UpdateAttrDead`）。
- `WEntity/WEntity.cs` `UpdateAttrEquipVisible`：同样包装（原有 try-catch 曾被注释掉，现已恢复为包装形式并清掉旧注释）。
- `WEntity/WEntity.cs` `UpdateAttrDead` catch 日志标签纠正：`"UpdateAttrQuest Exception"` → `"UpdateAttrDead Exception"`（原标签误导排查）。
- `WEntity/Models/WUpdateObjectInfo.cs` `GetUpdateValues`：空 catch 加错误日志（解析异常会损坏 ReadIndex 弄丢同包后续对象，原来无声无息）。
- `WEntity/WEntity.cs` `Update()` 死亡兜底：本玩家 IsDead 且未 InSoul 时，每秒补调 `WPlayerInfo.singleton.ShowRepopConfirm()`（Lua `DungeonTargetCtrl.OnDead` 纯 SetActiveEx(true)，幂等）——覆盖"死亡瞬间 Lua 主 UI 未激活导致 ON_DEAD_DISPATCH 丢失"的断点。

### 服务端（modules/mod-playerbots/src/Ai/Base/Actions/ReleaseSpiritAction.cpp）

- `RepopAction::isUseful()`：加 `botAI->IsRealPlayer()` 早退（原来无守卫，会把坠落致死的真人尸体传送墓地）。
- `SelfResurrectAction::isUseful()`：加 `botAI->IsRealPlayer()` 早退（原来无守卫，会自动用掉真人的灵魂石/复生）。
- 未动 `ReleaseSpiritAction::Execute`（手动路径，与上次修复口径一致）。

## 未覆盖的残余风险

- 若整个死亡 values 包在解析层丢失（HP=0 从未写入 Attrs），客户端 getter 判死不成立、兜底也不会启动；服务端无死亡重发机制，最后靠 6 分钟强制释放（PlayerUpdates.cpp:359-367）兜底。`GetUpdateValues` 加日志后可在复现时确认此路径。
- 复现后查日志关键词：`IsDead setter Exception`、`UpdateAttrValue Exception`、`UpdateAttrDead Exception`、`GetUpdateValues Exception`、`[WNetEventHandler][HandleWorldReceiveNetEvent]`。

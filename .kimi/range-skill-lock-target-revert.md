# 范围技能点按自动锁定最近敌方 —— 回归修复（2026-08-12）

> 纯前端（Unity 客户端 `D:\Unity\clientproj`）。症状：暴风雪等地面选点技能，点按按钮不再自动吸附最近敌方单位，落点默认变成自己脚下。

## 根因

客户端 SVN r963（jzb，2026-08-11 21:17，主题=高空飞行下选点施法/范围特效修复）改了 4 个文件，其中
`UISystem/Ctrl/Skill/MSkillDoubleDiskHandler.cs` OnDown 范围分支把锁敌结果丢弃了：

```csharp
// r963 改成的（坏）：
skillCtrl.EffectOffset = Vector3.zero;
skillCtrl.CastingOffset = Vector3.zero;
```

- 找目标代码没坏：分支里 `FindOneTarget(true, UpRange, ...)` 仍能找到最近敌方并算出 `CastingOffset = 目标-玩家`、置 `EffectLockTarget`；但紧接着两行清零把结果抹掉。
- 后果 1：指示圈初始在自己脚下（`EffectOffset=0`）。
- 后果 2：松手释放 `OnUp` 里 `castPoint = GetCastPoint(target, CastingOffset)`；暴风雪 `NeedTarget=false`（其 `_targetFlag` 带 0x1 位，TargetType 被判成 kSkillType_Self），RING 走 `player.Position + castingOffset` → 落点=自己位置。

## 修复

`MSkillDoubleDiskHandler.cs:802-806` 还原为 r963 前的逻辑（与 SVN r961 pristine 逐字核对过）：

```csharp
skillCtrl.EffectOffset = skillCtrl.EffectLockCenter
    ? player.VehicleOrModel.Forward * 0.1f
    : skillCtrl.CastingOffset;
```

摇杆拖动超 10px 自动解除锁定的兜底逻辑（OnDrag）原本就在，拖放选点不受影响。

## r963 其余 3 个文件的评估（均无需动）

- `WComponents/Staffs/WSkillRangeComponent.cs`：`onShowSkillRange` 里 `_effectOffset = Vector3.zero` 只是清上次残留，OnDown 中 ShowSkillRange 先于 UpdateSkillRange 触发，锁敌 offset 随后照常写入；`SetFxActive/IsFxActive`（修 Renderer 被禁用的显示 bug）、`SetFxOnGround/SetFxBelowEntity`（只改 Y 轴贴地，X/Z 语义不变）都是显示层修复，保留。**遗留问题**：`WSkillRangeComponent.cs:238` 有一行 r963 留下的逐帧调试日志 `AddYellowLogF("[SkillRange] RING特效 ...")`，拖圈时刷屏，建议删除。
- `WSkill/WSkillCore.cs`：`IsNeedRange()` 从只看 `_targetFlag` 的 DEST_LOCATION 位，扩为同时按每个 effect 的 ImplicitTarget0/1 是否 DEST 类目标判断（注释里承认 _targetFlag 是枚举 ID 当位或，不能判 DEST）。更多选点技能能正确出圈，与选目标无关。
- `WTransfer/WNavigationMgr.cs`：`CheckGroundLoaded` 飞行时加大射线长度估地面，与技能无关。

## 调查方法备注

客户端工作副本在 SVN 下（`Assets/HotUpdate/MoonClient/.svn`），机器无 svn CLI；可用 Python sqlite3 查 `.svn/wc.db` 拿 pristine SHA，再到 `.svn/pristine/<前两位>/<sha>.svn-base` 取任意历史版本 diff（pristine 不会随更新删除旧版）。

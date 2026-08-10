# 战士姿态 buff 图标不显示修复（2026-08-10）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。

## 症状

战士切换姿态（战斗姿态 2457 / 防御姿态 71 / 狂暴姿态 2458）后，buff 栏不显示姿态状态图标。小德形态、暗影形态显示正常。

## 根因

buff 图标显示链路：`SMSG_AURA_UPDATE(_ALL)` → `WAttackRegister.AuraUpdateResponse` → `WEvent_BUFF` → `WBuffComponent.OnBuffAdd` → `WBuff.Init` 里 `_isVisible = _data.IsAuraShow(...)` → `GetBuffInfoByLua()` 过滤不可见 buff → Lua `BuffMgr.UpdateBuffInfo` → `MainRoleInfoCtrl` 玩家 buff 栏。

`WSkillCore.IsAuraShow`（`WSkill/WSkillCore.cs:410`）按 **retail 语义**检查 `SPELL_ATTR1_NO_AURA_ICON = 0x10000000`，但客户端 SpellMiscWoW 表是 **WotLK 3.3.5a 数据**（Attr0=0x29050010 等与原版一致；retail 早已删除 2457），该位在 WotLK 是 UNK 位，三个姿态都带 → `showIcon=false` → 图标被过滤。

实测 dump（`var/dump_stance_tables.py`，解析 TableData/*.bytes FlatBuffers，int 字段 XOR eCrypt）：

- 2457/71/2458：Attr1=0x90000400 / 0x10000400 / 0x10000400，均含 0x10000000；Effect 均含 Aura=36（MOD_SHAPESHIFT）
- 对照小德形态（768/5487/9634/24858）、暗影形态（15473）：Attr1 不含该位 → 正常显示
- 全表 4825 个法术带该位，其中带变形效果的仅 11 个（3 个战士姿态 + 7164/7165/7366 等 NPC 姿态变体）

官方 WotLK 行为：姿态显示为 buff 图标（[wowhead wotlk spell=2457](https://www.wowhead.com/wotlk/spell=2457/battle-stance) 标注 Buff）。

## 修复

`WSkillCore.IsAuraShow`：变形/姿态光环豁免 NO_AURA_ICON 检查——

```csharp
else if (noAuraIcon && !hasShapeshift) showIcon = false;
```

与同函数已有的 `isPassive && !hasShapeshift` 豁免同一模式。影响面仅 11 个变形法术，其余 4814 个带该位的法术维持现状（不大开图标闸）。

## 下游确认（无需改动）

- `SpellWoW` 表有 2457/71/2458 行 → `WBuff.Init → GetSkillCore` 不会空
- 图标：`SpellIconFileDataID` 有效（132349/132341/132275），`InitItem/InitTips` 用 `info.icon` 全路径加载
- 时长：姿态无限时长（DurationIndex=21，服务端不带 AFLAG_DURATION → totalTime=0），`MainRoleInfoCtrl.InitItem` 对 totalTime<=0 不跑倒计时/进度条 → 无 "0s" 残留
- 取消：`CancelBuff` 走 `WCancelAuraRequest(spellId)`，buff tips 删除按钮对姿态可用（官方允许右键点掉姿态）

## 待验证

用户进游戏确认姿态图标显示（首次切姿态/登录自带姿态两条路径：SMSG_AURA_UPDATE 与 SMSG_AURA_UPDATE_ALL→`WEntityMgr.CheckAuraInfo`）。

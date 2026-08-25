# 圣洁之书复活亨兹·法奥克后无法对话修复（2026-08-24）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。

## 症状

圣骑士任务链「圣洁之书」（1786）：对亨兹·法奥克（creature 6172）使用生命符记（物品 6866 → 法术 8593）复活后，点击他无法对话，任务交不掉。

## 服务端链路（确认无问题）

- `creature_template` 6172：模板 npcflag=2（QUESTGIVER）、unit_flags2=2048（FEIGN_DEATH）、SmartAI。
- `smart_scripts`：Reset 时挂装死光环 29266 并清 npcflag=0；被 8593 命中 → actionlist 617200：1s 面向/说话，**1.5s 设 npcflag=2**，120s 后 evade 复原。
- C++ `src/server/scripts/Spells/spell_quest.cpp:1457` `spell_symbol_of_life_dummy`：移除装死光环、清 UNIT_FLAGS2、回血 50%。
- `NPCHandler.cpp:140` `HandleGossipHelloOpcode`：只要求 npcflag != 0 → 施法 1.5 秒后服务端即允许对话。

## 根因

客户端「能否对话」绑死在 spawn 瞬间的 npcflag 快照：

1. 亨兹 spawn 时 npcflag=0 → `WEntityMgr.cs:875` 分类建成 `WCreature`（非 `WNpc`），`_objectType` 构造时写死不变。
2. 1.5 秒后服务端推 `UNIT_NPC_FLAGS=2`，`WEntity.cs:1170` `UpdateAttrFields` 无该字段分支，值只进 `_attr.Attrs` 字典（`WEntity.cs:1603`），实体不重新分类。
3. 点击入口 `TouchObject.cs:468` `if (entity.IsNpc)` 恒 false → `WNPCMgr.OnSelectNPC` 不执行 → hello 包发不出去。

排除项：装死状态不挡对话（点击链路不查 IsDead，光环移除后正常复活）。

## 改动文件

| 文件 | 改动 |
|------|------|
| `Input/TouchObject.cs` | `setPlayerTarget` 对话判据改为 `entity.IsNpc \|\| HasNpcFlags(entity)`；新增私有静态方法 `HasNpcFlags`：实时读 `Attr.GetAttr(UNIT_NPC_FLAGS) != 0`（GetAttr 缺 key 返回 0，安全） |

`WNPCMgr.OnSelectNPC`（`WNPCMgr.cs:193`）本身不限制实体类型（petitioner 特判也是读实时 flag），收到调用即连发 CMSG_GOSSIP_HELLO + CMSG_QUESTGIVER_HELLO，无需改动。

## 链路要点

- 属性更新链路：SMSG_UPDATE_OBJECT → `WEntityMgr.UpdateFields` → `WEntity.UpdateAttrFields` → `UpdateAttrValueInternal` 写 `_attr.Attrs`（= `WAttrComponent._dictAttrValue`）→ `GetAttr` 读同一字典，实时值可信。
- `IsNpc` = `_objectType == NPC`（`WObject.cs:293`），spawn 时由 create 包 UNIT_NPC_FLAGS>0 一次性决定（`WEntityMgr.cs:875`），终生不变。
- 验证方式（未改代码也可验）：复活后跑出视野再回来，实体重建时 npcflag=2 建成 WNpc，即可对话。

## 遗留（未动）

- `WCreature` 形态的这类单位仍不挂 `WNpcFxComponent`，头顶不会出任务感叹号/问号（需改 `UpdateAttrFields` 加 UNIT_NPC_FLAGS 分支做重建/动态换 `_objectType` 才能解决，本次选低风险方案未做）。
- `JudegeType` 里 NPC 优先 break（`TouchObject.cs:302`）仍只看 `IsNpc` 快照，重叠选中优先级对动态加旗标单位不生效（影响极小）。
- 施法后 1.5s 窗口期内点击仍无反应（服务端此期间也未设旗标，行为一致）。

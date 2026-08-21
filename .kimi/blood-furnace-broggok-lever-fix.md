# 鲜血熔炉老二（布洛戈克）栅栏打不开 / BOSS 无法击杀 —— 根因与修复

> 症状：鲜血熔炉（map 542）老二布洛戈克（Broggok）前的栅栏点不开、BOSS 免疫无法击杀。
> 根因：**客户端没给拉杆发包**——`TouchObject.cs` 的 GOOBER(type 10) 白名单要求 `Data2 != 0`，
> 而"牢房大门控制杆"(entry 181982) 模板 `Data2=0`，点击被静默吞掉，`CMSG_GAMEOBJ_USE`(177) 从未发出。
> 服务端事件链本身完好，零改动。修复在客户端一行（已改，2026-08-21）。

## 一、关键对象

| 项 | entry / guid | 说明 |
|---|---|---|
| 拉杆"牢房大门控制杆" | GO **181982** / guid 150441 | **type=10 GOOBER**，ScriptName=`go_broggok_lever`；坐标 (456.6, 54.4, 9.6)；模板 Data0=0,Data1=0,**Data2=0**,Data3=3000 |
| 老二前栅栏 | GO **181819** / guid 22297 | type=0 DOOR，displayId 6863 (Doodad_Hellfire_DW_PrisonEntry04)；坐标 (456.3, 34.2, 23.8)；DB state=1(关)；addon **flags=32 (GO_FLAG_NOT_SELECTABLE)** |
| 老二前门 | GO 181822 / guid 22300 | `DOOR_TYPE_ROOM`（BOSS 进战才关） |
| BOSS 布洛戈克 | NPC 17380 / guid 138115 | 坐标 (455.3, -1.8, 9.6)，初始 `UNIT_FLAG_NON_ATTACKABLE`+全免疫 |
| 牢房兽人 | NPC 17398 (Nascent Fel Orc) | 4 间牢房（181821/181818/181820/181817），按坐标分箱存储 |

位置沿 y 轴一线：BOSS(-2) → 栅栏(34) → 拉杆(54) → 牢房(76~123) → 前门(149)。

## 二、服务端事件链（全部读码验证，无 bug）

1. 点拉杆 → 客户端发 177 → `HandleGameObjectUseOpcode`（`SpellHandler.cpp:327`，距离走默认 `INTERACTION_DISTANCE`）→ `GameObject::Use`。
2. `Use()` 入口处 `sScriptMgr->OnGossipHello`（`GameObject.cpp:1473`）→ `go_broggok_lever::OnGossipHello`（`boss_broggok.cpp:101`）：BOSS 状态 `NOT_STARTED` → `ACTION_PREPARE_BROGGOK` → `SetInCombatWithZone` + `SetBossState(IN_PROGRESS)`；脚本返回 false 后继续走 GOOBER 默认分支（拉杆动画/冷却，纯表现）。
3. `SetBossState(DATA_BROGGOK, IN_PROGRESS)` → `ActivateCell(CELL1)`（`instance_blood_furnace.cpp:117`）：开 1 号牢房、兽人解除免疫进战斗。
4. 每波全灭 → `PrisonerDied` 开下一牢房（cell2→3→4）；第 4 波灭 → `ActivateCell(DATA_BROGGOK_REAR_DOOR)`（:214-219）：**开栅栏 181819 + `ACTION_ACTIVATE_BROGGOK`**（BOSS 转 aggressive、解除免疫、排技能，`boss_broggok.cpp:74-91`）。
5. 栅栏同时是 `DATA_BROGGOK` 的 `DOOR_TYPE_PASSAGE` 门（`instance_blood_furnace.cpp:28`）：BOSS 非 DONE 时 `InstanceScript::UpdateDoorState` 强制关、DONE 后强制开——与脚本开门不冲突。
6. 灭团/重置：`BossAI::_Reset` → `SetBossState(NOT_STARTED)` → `ResetPrisons()`（兽人复活重上免疫、牢房全关）+ `DoRespawnGameObject(LEVER)`（拉杆复活）。

### 为什么"点栅栏"永远没反应（设计使然，不是 bug）

- 栅栏带 `GO_FLAG_NOT_SELECTABLE` → `GameObject::Use` 第一行 return（`GameObject.cpp:1463`）；
- PASSAGE 门 BOSS 死前被副本脚本钉死。它**只能**由第 4 波后的脚本打开。

### GM 命令无法模拟拉杆

`.gobject activate` 直接调 `UseDoorOrButton`（`cs_gobject.cpp:85`），**绕过 `Use()` 的脚本钩子**——只会播拉杆动画，不会启动事件。验证全链必须真的发 177。

## 三、根因（客户端）

`TouchObject.cs`（`D:\Unity\clientproj\Assets\HotUpdate\MoonClient\Input\`）点选 GO 发包是白名单制（`setPlayerTarget`，原 :489-517）：

- `type == 0`（门）：≤5 码发 177；
- `type == 10`（GOOBER）：要求 `Data0==0 && Data1==0 && Data2 != 0` 才发 177 —— **拉杆 Data2=0，被此条件拦死**。

点击前的 `IsActivate` 门槛（`WGameObject.cs:59`）不影响本案例：拉杆非任务 GOOBER，服务端 dynFlags=0 → 客户端 `dynFlags==0` 视同可交互，能选中，只是不发包。

后续连锁：不点拉杆 → 无兽人波次 → 栅栏永远钉死 → BOSS 永远 `NON_ATTACKABLE`+免疫。与现象完全吻合。

## 四、修复（2026-08-21，已改）

**客户端 `TouchObject.cs`**：GOOBER 分支去掉 `Data2 != 0`，未上锁（Data0/Data1==0）的 GOOBER 点击即发 177。服务端 `Use()` 对脚本/gossip/动画全权裁定，装饰性 GOOBER 被点无副作用；顺带修好其他副本同类拉杆。

```diff
- (type == 10 && Data0 == 0 && Data1 == 0 && Data2 != 0)
+ (type == 10 && Data0 == 0 && Data1 == 0)
```

备选（未采用）：DB 把拉杆 type 10→1 (BUTTON) 走 `type==1 && Data1==0` 分支——服务端脚本钩子与类型无关照样触发，但与上游模板数据分叉，不如改客户端干净。

## 五、验证清单（客户端热更后实测）

1. 点拉杆 → 拉杆动画 + 1 号牢房开、兽人扑出（BOSS 开始喊话/in-combat）。
2. 连清 4 波兽人 → 栅栏 181819 自动开。
3. 布洛戈克可选中可攻击，正常放技能（毒云/毒箭/软泥喷）。
4. 杀掉 BOSS → 栅栏保持开（PASSAGE 门 DONE 语义）。
5. 灭团重来：拉杆应复活可再点，兽人/牢房复位。

## 六、相关文件

服务端（`D:\UnityWow\azerothcore\azerothcore-wotlk`）：
- `src/server/scripts/Outland/HellfireCitadel/BloodFurnace/boss_broggok.cpp`（拉杆脚本 / BOSS 免疫与激活）
- `src/server/scripts/Outland/HellfireCitadel/BloodFurnace/instance_blood_furnace.cpp`（doorData、牢房波次、开栅栏）
- `src/server/scripts/Outland/HellfireCitadel/BloodFurnace/blood_furnace.h`（DATA/GO/NPC 枚举）
- `src/server/game/Entities/GameObject/GameObject.cpp:1460`（`Use`：NOT_SELECTABLE 早退 + 入口脚本钩子）
- `data/sql/base/db_world/gameobject_template.sql:11179`（拉杆模板）、`gameobject.sql:66381/13133`（拉杆/栅栏 spawn）

客户端（`D:\Unity\clientproj\Assets\HotUpdate\MoonClient`）：
- `Input/TouchObject.cs`（点 GO 发白名单，本次修复点）
- `WEntity/WGameObject.cs:59`（`IsActivate`：dynFlags 含 ACTIVATE 或 ==0 即可交互）

关联文档：`gameobject-door-system-analysis.md`（门系统全链路、177 无锁校验、PASSAGE 门语义）。

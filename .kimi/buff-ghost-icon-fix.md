# 幽灵 buff（别人/过期 buff 图标显示在自己头像下）修复（2026-08-10）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动（服务端行为经核实为标准语义）。

## 症状

玩家头像 buff 栏/详情 tips 里有时出现"不该有"的 buff 图标（如战士身上挂强效力量祝福、精神祷言、无荣誉目标），间歇性，多在组队/转场后出现。

## 根因（三条叠加，均为客户端）

协议事实（服务端 `Player::GetAurasForTarget` / `AuraApplication`）：
- `SMSG_AURA_UPDATE`（单槽增删）经 `SendMessageToSet` 发送——单位不在集合内（转场/加载中）时移除包会丢。
- `SMSG_AURA_UPDATE_ALL` 是**全量快照**（只含当前可见光环，登录/换图/单位进视野时发），语义上应整体替换。

客户端缺陷：
1. **UpdateAll 非原子**：`WAttackRegister.AuraUpdateAllResponse` 与 `WEntityMgr.CheckAuraInfo` 只做增量添加，从不清旧。漏收移除包留下的幽灵 buff 永久残留。
2. **槽位幽灵挡新**：`WBuffComponent.OnBuffAdd` 对已被占用的槽位直接忽略新 buff（`!ContainsKey` 才加）——幽灵占槽后，服务端复用该槽发新 buff 被丢弃，幽灵继续冒充显示。
3. **快照队列不写回**：`WEntityMgr.AddAuraUpdate` 里 `updateInfo = info` 只改局部变量没写回字典，同一实体多个排队快照永远用第一份（更旧）。

Lua 侧已排除：`BuffMgr.UpdateBuffInfo` 按 `tostring(uid)==tostring(WPlayerInfo.UID)` 路由自己/目标全局表，`WPlayerInfo.UID=_serverCharacter.GUID` 可靠；事件总线按实体分发，无跨实体污染。

## 修复（自愈式，未变的 buff 不动、循环特效不中断）

| 文件 | 改动 |
|------|------|
| `WSkill/Buff/WBuffComponent.cs` | `OnBuffAdd`：槽位被**不同 id** buff 占用时先移除再加（同槽必幽灵，附 log）；新增 `RemoveBuffsNotInSnapshot(snapshot)`：移除不在快照中或同槽 id 不同的旧 buff（附 log） |
| `WSkill/Registers/WAttackRegister.cs` | `AuraUpdateAllResponse` 处理前用快照调 `RemoveBuffsNotInSnapshot` |
| `WEntity/Mgr/WEntityMgr.cs` | `AddAuraUpdate` 改为 `_aurasToUpdate[target] = info` 直接覆盖；`CheckAuraInfo`（实体生成后回放排队快照）同样加快照同步 |

设计取舍：不用 `WEvent_Buff_ClearAll` 全清（会杀掉未变 buff 的循环特效且 UpdateAll 路径 playBeginFx=false 不重播），改用差集移除——无 FX 闪烁。

## 验证方式

两条针对性 log，复现时看控制台：
- `Buff slot {slot} stale ghost {oldId} replaced by {newId}` —— 幽灵占槽被新 buff 顶掉
- `Buff snapshot removes stale ghost: slot {slot} spell {id}` —— 快照清幽灵

若复现确认机制后，这两行 log 可撤（参照 spell-fx 的 [FxLife] 惯例）。

## 遗留（未动）

- `SMSG_AURA_UPDATE` 单包在实体未创建时直接丢弃（无队列）；依赖随后的 UpdateAll 兜底。
- `WEvent_Buff_ClearAll` 无任何 C# 触发方（死事件），保留未清理。

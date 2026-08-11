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

## 第二轮：实测仍有问题的深挖（同日补充）

> 用户实测日志：进出地下城后 buff 图标列表仍错。日志中幽灵替换在触发（机制有效），但暴露了两个更深的时序/收敛缺陷。

日志解读：168=霜甲术（法师 bot 自施法）、6756=野性印记、1244=真言术：韧、19742=智慧祝福、18950=隐形与潜行侦测、71328=地下城冷却时间——日志是**多个实体（玩家+ bots）混流**，替换日志多发生在 bot 实体上（属正常自愈）。

新发现的缺陷：

1. **排队快照回放时机乱序（主因）**：`CheckAuraInfo` 原先只在 `WEntity.OnUObjLoaded`（**模型加载完**）调用。窗口期：UpdateAll 到达（实体未注册）→ 排队 → 实体注册后 live 单条增删已直接生效 → 模型加载完旧快照才回放 → 把新状态踩回旧状态（幽灵复活/新 buff 被清）。模型走缓存时回调时机更不可控。
   修复：`WEntityMgr.PrepareEntity` 在 `AddObject(ret)` 注册后立即 `CheckAuraInfo(ret.UID)`（组件在 Initialize 阶段已挂好，事件订阅早于注册，安全）；OnUObjLoaded 的调用保留作兜底（队列为空时无操作）。
2. **空快照/全不可见快照不触发刷新**：没有任何增删事件时 `_needRefresh` 不置位，Lua 全局表不重建，转场前的旧图标列表一直显示。且 `onUpdateBuffCallLua` 在 Player 未就绪时落空还照清 `_needRefresh`，推送永久丢失。
   修复：`WBuffComponent` 新增 `ForceRefreshBuffLua()`，两条快照路径（直接/回放）处理完都调用；`Update` 里推送改为成功才清标志（玩家未就绪下帧重试）。
3. 诊断日志升级：`Aura comes` 带 slot/target，新增 `Aura remove` 日志（单包/快照两条路径都有），便于按实体区分流。

至此闭环：注册即回放（顺序正确）→ 快照差集清幽灵 → 槽位幽灵替换兜底 → 处理后强制推 Lua → 推送失败下帧重试。

## 第三轮：实测截图分析（同日补充）

> 截图（仍是第一轮修复的 build，日志为旧格式可证）：霜甲术 168 同时显示两个（23m/19m）,71328 地下城冷却时间不显示。

**71328 不显示 —— 数据确诊**：其 Attr1=0x100000A8 同样带 0x10000000 位（retail NO_AURA_ICON 语义），且非变形效果，姿态豁免覆盖不到。服务端 `Aura::CanBeSentToClient` 只按 passive/area 过滤，不管该位；WotLK 官方该位无隐藏语义。但**不能全量删该检查**：实测扫描（`var/scan_auraicon_bit.py`，已删）带此位且会上光环的非被动法术有 2844 个，含暴风雪/奥术飞弹/钓鱼/追踪类等引导与追踪法术，全删会误放大量垃圾图标。
修复：`WSkillCore.IsAuraShow` 引入 `_auraIconForceShow` 白名单（71328），与变形豁免合并为 `auraIconExempt`。

**霜甲术显示两个 —— 机制**：服务端对同法术同施法者保证单槽（`AuraApplication` 构造器槽位复用），刷新（重施放）走 `SetNeedClientUpdate` 推**同槽同 id** 的包；客户端原来对"同槽同 id"直接忽略 → 时长永不刷新（19m 陈旧残留），再叠加迟到快照乱序/漏移除 → 另一槽位又添一个 → 双图标。
修复：`WBuffComponent.OnBuffAdd` 同槽同 id 时改为刷新 `LeftTime/TotalTime`（`_needRefresh` 置位），不再静默丢弃。

## 第四轮：真正根源确诊——网络层响应对象复用 + Auras 跨包累积（同日补充）

> 第三轮实测日志（带 slot/target 新格式）暴露：快照反复到达、无任何 remove 日志、18950 与 168 在 slot 0 交替却无替换日志——用"漏收移除"无法解释。钻土虫酸液(18070)倒计时"结束"后进副本复活、眩晕(1604)凭空出现。

**根源（两层）**：

1. `WPacketsHandler.Handle`：每个 opcode 的响应对象**单例复用**(`item.Reponse = new U()` 注册时建一次，每包 `Load()` 重填），且 `Load` 不调 `Reset()`。
2. `WAuraUpdateAllResponse.LoadData` **从不清空 `Auras` 列表** → 每个 `SMSG_AURA_UPDATE_ALL` 包都向同一列表**追加** → 列表累积了历史上所有实体的所有光环。无论直接处理还是排队回放，都会把"别的实体的 buff + 早已过期的 buff"当成当前目标的快照应用。

这一个 bug 解释了全部历史症状：别人(buff)图标挂自己头像（第一张截图的强效力量祝福/精神祷言来自 bots 的快照）、霜甲术双图标（列表里有两条不同槽位/不同时长的 168)、酸液复活+眩晕突现（累积列表里的过期货被反复重放）。

**修复**：

- `WAuraUpdateAllResponse.LoadData` 开头 `Auras.Clear()`（根源一票）。
- `WEntityMgr` 挂起队列重构 `PendingAuraUpdates`（快照+单条增删列表），**入队存拷贝**（共享实例会被后续包改写）；快照到达时丢弃其之前到达的单条（TCP 保序，快照涵盖）；新增 `AddAuraSingleUpdate`——单条增删在实体未创建时不再丢弃，排队保序回放（修转场窗口丢包，如眩晕迟到）。
- 回放路径补 `args.flags`（原来丢 AFLAG_NEGATIVE，debuff 会被当成 buff 显示）。
- 回放单条带 `Aura replay` 日志。

**同类隐患（未修，记录在案）**：响应单例复用模式下，凡带 List 字段的响应类都有同样的累积风险，已见 `WSocialHandlerResponse.Contacts`、`ItemHandlerResponse` 的 ItemStats/Damages/Spells/Sockets、`WAttackStateUpdateResponse.SubDamages/Absorbs/Resists`、`WPartyCommandResultResponse.RaidInstances` 等；纯值字段类若 LoadData 有提前 return 分支（如 `WAuraUpdateResponse` 的 `AuraId<=0 return`），未写字段会残留上一包的值。排查其他协议显示异常时优先查这里。

**早前各轮修复仍然有效且互补**（漏收移除的槽位幽灵自愈、快照差集、注册即回放、强制刷新、时长刷新、白名单），但根源是本轮的累积 bug。

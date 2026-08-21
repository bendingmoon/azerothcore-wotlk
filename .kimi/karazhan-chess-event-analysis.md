# 卡拉赞象棋事件（v2 重构方向讨论记录）（2026-08-17/18 排查 → 2026-08-19 回滚重议 → 08-20 定方向）

> **状态总览**：v1 客户端修改**已全部回滚**；服务端仅剩 `OnCharmed(false)` 沉默修复（已随 `bc0dcad73` 提交）。v2 方向已定（**统一"控制模式"= 被控单位技能替换主技能栏**，象棋/野兽之眼/心控三玩法同构），**数据层已证零缺口无需注入**。具体实现由用户逐步引导，尚未开始写代码。
> **v1 最大教训（先读这条）**："象棋技能不在客户端表里、只能硬编码"是误判——当时解析的 `SpellDbc.bytes` 是零消费方的死数据；真正的技能表（`SpellWoW`/`SpellMisc`/`SpellEffect`/`SpellRange`/`SpellName`）全是全量表且包含全部象棋技能。**下任何"表缺数据"结论前，先从消费方代码反查实际读哪张表。** 详见 §四-8/10/11。
> 服务端仓库：`D:\UnityWow\azerothcore\azerothcore-wotlk`；客户端：`D:\Unity\clientproj`（C# 在 `Assets/HotUpdate/MoonClient`，Lua 在 `Assets/Scripts/Lua`）。

## 一、官方机制速览（理解一切修改的前提，v1 排查结论仍有效）

- 点棋子 gossip "Control" → 玩家对棋子放 **30019 Control Piece**（Bind Sight + Periodic Dummy + **Charm**）→ `CHARM_TYPE_CHARM` 魅惑（**非 POSSESS**，SpellAuraEffects.cpp:3754）。
- Bind Sight → `SetViewpoint(棋子)` → **PLAYER_FARSIGHT=棋子 GUID** 发给控制者（Player.cpp:13232）。**m_mover 恒为玩家**（CHARM 不切 SetClientControl）→ 棋子 GUID 的移动包在 `MovementHandler.cpp:366` 静默丢弃——**摇杆永远不能移动棋子，这是官方设计**。
- `OnCharmed(true)`：移除控制者的 GAME_IN_SESSION 沉默 + 手工发 SMSG_PET_SPELLS 宠物条（boss_chess_event.cpp:1517-1559）。宠物条 4 键 = creature_template_spell 表：①Move（37146=8码 兵/象/车/王；37144=15码 马；37148=20码 后；37151-3 未用）②ChangeFacing（30284=100码）③④攻击技。
- **移动唯一途径 = Move 法术**：地面选点、瞬发、1sCD、3 码半径 Dummy 命中格子触发器（NPC 22519 ×64，带 NOT_SELECTABLE 不可点选）→ `SpellHitTarget` → `HandlePieceMove`（校验格子+步数：兵/象/车/王 1 格、后 3 格、马 2 格）→ `MovePoint` 走位 + 移动冷却（boss_chess_event.cpp:1903-1924）。
- 服务端 `CMSG_PET_CAST_SPELL` 链路完整（PetHandler.cpp:990-1061：caster==GetCharm() 通过、SpellCastTargets 支持 dest、敌方单体无目标时兜底用玩家选中目标 :1040）。
- 结束路径：王死→`HandlePieceJustDied`（赢=DONE+宝箱 / 输=NOT_STARTED）；Restart gossip；REINIT 时棋子 RemoveAllAuras → 解魅惑。

## 二、问题清单与根因（v1 全部定位，仍有效）

| # | 现象 | 根因 |
|---|------|------|
| 1 | 棋子不会走路只能攻击 | 官方就走法术不走摇杆（m_mover=玩家）；客户端宠物条施法不带目标，Move 无地面目标静默失败 |
| 2 | 结束沉默(39331)不消失，只有死了才消 | `OnCharmed(false)` 无条件重挂 GAME_IN_SESSION（**已修，见§三**） |
| 3 | 宠物条全闪烁 | 服务端手工建宠条全给 `ACT_ENABLED=0xC1`（autocast+castable，CharmInfo.h:60-68），Lua 把 193 当"自动释放开"闪；象棋无自动释放概念 |
| 4 | 单击施放延迟 0.8s | Lua 双击判定定时器压单击 |
| 5 | 点"控制"后 gossip 菜单不关 | `OnWNPCGossipComplete`（WNetClient.cs:1795）只调空回调，不关 TalkDlg2 |
| 6 | 相机不切棋子 | LoadFarSight 的 `IsPet\|\|IsCreature` 门槛；棋子创建时 WNpc、魅惑后重分类 Creature（**分类不稳定**，实测两次断点一真一假） |
| 7 | 魅惑中玩家本地可滑动脱同步 | 服务端只加 UNIT_FLAG_DISABLE_MOVE，客户端不解析该 flag |
| 8 | 攻击技能点了没反应 | 原 `if(IsCreature) cast else if(FarSightGuid==0) WPetActionRequest`：棋子为 WNpc 时**两分支都不进，零发包** |
| 9 | Move 按钮按下无反应 | Lua 绑定是生成式（MoonClient_*Wrap.cs），新 C# 方法未注册 → Lua 调用报 attempt to call nil |
| 10 | 有 Begin 日志但无目标圈 | 范围圈组件只挂玩家（WPlayer.cs:704），圈画在玩家脚下；相机切棋子后玩家出画面 |
| 11 | 控兵选不中敌子（控王可以） | ①点选射线不跳过 64 个隐形格子触发器（NOT_SELECTABLE），缓冲 32 无序，Target 被设成隐形触发器；②`setPlayerTarget` 有 30 码距离限制（chooseTargetMaxDistance=30，以玩家为基准），敌子 40-55 码外静默丢弃 |

## 三、v1 方案与回滚状态

**v1 已回滚的部分**（客户端全干净，git/grep 已核实）：

- ~~`WPetGroundCastHandler.cs`（新增选点控制器）~~ 已删
- ~~`WPlayerInfo.HandlePetAction` 门控修复 + 3 个 Lua 桥接~~ 已还原
- ~~`WPetCastSpellRequest` DEST 打包~~ 已还原（协议缺口仍在）
- ~~`WPlayer.LoadFarSight` 象棋 entry 分支 / `_isFarSightWatchNpc`~~ 已还原
- ~~`WNetClient` gossip 关窗、~~`TouchObject` NOT_SELECTABLE 跳过与 30 码豁免~~ 已还原
- ~~`QuickPetSkillTemplate.lua` 象棋模式 + 单击即时化~~ 已还原
- ~~`MoonClient_WPlayerInfoWrap.cs` 手工注册~~ 已还原

**v1 保留的部分**：

- 服务端 `boss_chess_event.cpp` `OnCharmed(false)`：仅当 游戏阶段∈{PVE/PVP_WARMUP, INPROGRESS_PVE/PVP} 且 事件∈{IN_PROGRESS, SPECIAL} 才重挂 GAME_IN_SESSION（双条件缺一不可：只查 phase 输棋 REINIT 时 phase 未更新会漏；只查 event 则 Restart 路径 REINIT 时 event 未更新会漏）。WARMUP 保留重挂防热身期解除后砍敌方王。RECENTLY_INGAME(10s) 照旧。**已随 `bc0dcad73` 提交，未编译验证**。

**v1 方向为什么被否（诊断共识）**：

1. 同一份象棋数据硬编码三处（C# 技能表 + Lua `chessGroundCastSpells` + 12 棋子 entry 表），改数值要同步三地
2. 特殊分支撒进 ~10 个通用系统（TouchObject/LoadFarSight/gossip/距离检查……），全是全局行为变更，回归风险大
3. 与客户端既有数据驱动管线对着干：客户端本有完整地面选点管线，象棋用不上只是因为技能不在客户端表里，v1 却另起并行系统

## 四、v2 探索新事实（客户端管线，2026-08-19 核实）

1. **通用魅惑视角基建已存在**（BWL 拉佐格尔所建）：`LoadFarSight`（WPlayer.cs:1623-1646）接受 `IsPet||IsCreature` → 切相机 + 挂 CharacterController + `SetIsSyncPos(false)`；`WEntity.Model` getter 返回 `ModelFarSight`（WEntity.cs:151-159）→ 摇杆驱动被控单位；所有 `SendMove*` 在 `_farSightGuid>0` 时改发被控单位 GUID（WPlayer.cs:800-849）；`HandlePetAction`（WPlayerInfo.cs:542-553）把宠物条技能路由 `PetCastSkill(FarSightGuid, ...)`。象棋大部分可复用，唯服务端会丢棋子 GUID 移动包（官方设计，§一）。
2. **玩家 AOE 地面选点管线完全数据驱动**：`WSkillCore.IsNeedRange()`（:420-460，查 ImplicitTarget/Targets 的 DEST 类）、`UpRange`（SpellMisc.RangeIndex→SpellRange，:1034-1071）、`WSkillRangeComponent` 范围圈（事件 WEvent_ShowSkillRange/Pos/Hide，:447-518，组件在玩家身上 WPlayer.cs:704）、`MSkillDoubleDiskHandler` OnDown/OnDrag/OnUp 摇杆瞄准（thumbDis→`CastingOffset=mag*UpRange/thumbDis*dir`，天然钳在射程内，:812-926）、`WWaitingDataMgr.DoCastSkill` → `WMobileCastSpellRequest` 带 DEST 打包（transportGuid+xyz，`ToServerVector()=(-z,x,y)`）。注意：effectShape/effectRange=6f 硬编码在调用处（:737-742）。
3. **`WPetCastSpellRequest` 只有单位目标**（WPetHandlerRequest.cs:153-166，`Append(targetGuid)` 无 DEST 分支）——协议层真实缺口，任何方向都要补；打包格式照抄 `WCastSpellRequest.cs:41-63`。
4. **SMSG_PET_SPELLS 已带每棋子正确的 4 个技能 ID**（`WPlayer.UpdatePetActionSkills` :1313-1350 → `PetActionSkills`）→ 客户端**不需要**棋子 entry→技能映射（v1 的 entry 表多余）；缺的只有射程/半径/图标/名字。
5. **闪烁可服务端源头修**：宠条 `MAKE_UNIT_ACTION_BUTTON(spellId, ACT_ENABLED)` → 改 `ACT_DISABLED(0x81)`（castable 无 autocast = Lua 的 Type 129），Lua 零改动。
6. 客户端不解析 `UNIT_FLAG_DISABLE_MOVE` → 魅惑中本地滑步；通用修复=客户端认该 flag 禁摇杆（优于象棋特判 IsMovable=false）。
7. ~~表缺技能→图标空~~ **已修正（08-20）**：图标查 `SpellMiscWoW`（全量 51129 行）→ `FileDataManager.GetFilePath(IconFID)` → `Assets/StreamingAssets/Data/FileData.bin`（V2 格式 'FDT2'，155 万条）→ png 在 `artres/Resources/BakedWoW/interface/icons/`。**象棋技能图标已实测全链路可达**（37146→inv_boots_cloth_03.png 等）。
8. ~~GetSkillCore 对表外技能返回 null~~ **已修正（08-20）**：`GetSkillCore`/`AttachSkillInfo`（WSkillComponent.cs:1886-1991）用的是 `SpellWoW`（**全量 49387 行**，含全部象棋/宠物/心控技能）+ `_spellMiscEntities`（全量）→ **象棋技能原生可构建 WSkillCore，数据层零缺口，无需任何注入**。
9. 点选链路：`TouchObject.JudegeType` 射线 32 缓冲无序（:230），隐形触发器会截胡；`setPlayerTarget` 30 码限制（:455-458，`MGlobalConfig.chooseTargetMaxDistance=30`，:161）以玩家为基准。
10. 表格式：FlatBuffers，**每表独立 XOR key**（行类各带 `enum eCrypt`：SpellDbc=501319815、SpellMisc=256354909、SpellName=256431514、SpellEffect=1049755150、SpellRange=-1025156982、SpellWoW=501325505、SpellTargetRestrictions=-1305013164）。**重要：`SpellDbc.bytes`（4523 行子集）是全客户端零消费方的死数据**（grep `GetTableItem<SpellDbc>` 无命中——v1"象棋技能不在客户端表里、只能硬编码"的结论源于误解析此文件）；真正的 `SpellWoW.bytes`=49387 行全量（仅 ID/NameSubtext/Description/AuraDescription 四字段的文本描述表）。检查工具：`var/check_chess_client_tables.py`（多表覆盖+DEST 检测，只读）。
11. **象棋技能数据完整性（08-20 全量验证，37 个技能）**：SpellWoW/SpellMisc/SpellEffect/SpellRange/SpellName/SpellTargetRestrictions 全部有行；**Move 37146(8y，RangeIdx=137 自定义行存在)/37144(15y)/37148(20y)/30284(100y) 的 ImplicitTarget0=8（TARGET_UNIT_DEST_AREA_ENTRY）→ 客户端 `IsNeedRange()` 原生判定为 DEST 地面选点**；攻击技为单位/自我目标；图标纹理全部存在（FileData.bin 索引+png 实测）。技能清单来源：`creature_template_spell`（12 棋子 entry 见 karazhan.h:122-135）；Growl 2649、Claw 16827、心控 605、野兽之眼 1002 也均在。
12. **服务端控制统一性核实（08-20）**：EotB(1002)=HandleModPossessPet→`CHARM_TYPE_POSSESS`（SpellAuraEffects.cpp:3720）；心控(605)=HandleModPossess→`CHARM_TYPE_POSSESS`（:3686，玩家 caster）；象棋(30019)=HandleModCharm→`CHARM_TYPE_CHARM`（:3754）。POSSESS 分支（Unit.cpp:14769-14776）：UNIT_STATE_POSSESSED + UNIT_FLAG_POSSESSED + **控制者 UNIT_FLAG_DISABLE_MOVE** + `SetClientControl(被控,true)`（摇杆移动被控单位合法）+ `PossessSpellInitialize`（Player.cpp:9615，SMSG_PET_SPELLS 带 BuildActionBar、无冷却）；CHARM 分支（:14777-14799）：`m_seer != 棋子` 才 CharmSpellInitialize——象棋因 Bind Sight 使 m_seer==棋子而跳过，改由脚本手工发宠条。解除=`SendRemoveControlBar`=SMSG_PET_SPELLS guid=0（Player.cpp:9772）。三者客户端发包统一为 `CMSG_PET_CAST_SPELL(guid=被控单位)`，服务端 PetHandler 统收。
13. **主栏机制（换栏落点，08-20 调查）**：主栏=144 槽 `MainUISkillSlots`（SMSG_ACTION_BUTTONS 固定解析，WActionButtonsResponse.cs:29-37；接收时**不刷 UI**）；姿态"换栏"=纯客户端页偏移 `GetSkillActionSlotOffset`（WPlayer.cs:1259-1272）+ `WGlobalEvent_RefreshSkill`→`refreshSlots` 全量重建；**无任何 possess/bonus bar 机制**（C#/Lua 零命中）→ possess 换栏是**换数据源**（PetActionSkills）而非换页。槽位渲染门槛=`GetSpellMiscBySpellID≠null`（全量表 ✓）；表外技能 OnDown/OnUp 静默 return、`refreshCDs` 每帧刷错误日志（换栏时要拦）。施法拦截先例：`WWaitingDataMgr.DoCastSkill` :234-241（1002 取消 EotB / 姿态路由）。
14. **宠物施法反馈现状**：SMSG_PET_COOLDOWN 服务端不存在；SMSG_PET_CAST_FAILED 客户端仅枚举无 handler；SMSG_PET_SPELLS 的 Cooldowns 列表被解析但 `UpdatePetActionSkills` 不读；宠物施法 CD 落在被控实体的 SkillComponent（主栏读玩家的 → 看不到）。CD/失败反馈属可选 polishing。
15. `UpdatePetActionSkills`（WPlayer.cs:1314）会把 `PetGuid` 改写成 SMSG_PET_SPELLS 的 guid（附身时=被控体）——EotB 期间影响依赖 PetGuid 的逻辑，换栏方案要处理。

## 五、v2 方向：**统一"控制模式"= 主技能栏替换**（用户 2026-08-20 拍板）

**核心决策**：象棋 / 野兽之眼 / 心控 统一为官方 possess 语义——**被控单位技能直接替换当前角色主技能栏**（不是挂宠物栏）。用户判断"以前野兽之眼没搞完善"，要求三者一起完善。本节为可行性结论（已答用户），实现由用户逐步引导。

**可行性结论：可行。** 依据 = §四-12（服务端三者同构）+ §四-13（主栏可换数据源）+ §四-8/11（数据层零缺口——象棋技能在客户端全量表中原生齐全）。

**控制模式检测（已定稿并实现）**：纯客户端（用户指示服务端零改动）。进入 = 宠条包 guid≠0 且（`FarSightGuid==该 guid` 或玩家带 `UNIT_FLAG_DISABLE_MOVE`）；退出 = 空宠条包（解除魅惑必发，Unit.cpp:14961）或信号消失。宠条包是直连消息可能先于属性同步到达 → 判定为持续幂等重估（`WPlayerInfo.RefreshControlMode`），触发点：宠条包/farsight 变化/UNIT_FIELD_FLAGS 变化。普通宠物条缓存恢复（退出时经 IsPet 实体校验防信号滞后污染，野兽之眼结束自动还原猎人宠条）。

**改造模块清单（骨架，顺序待用户定）**：

1. ~~数据层：占位行注入~~ **已证不需要**（08-20：`SpellDbc.bytes` 是死数据；`SpellWoW`/`SpellMisc` 等主表全量且含全部象棋技能——见 §四-8/10/11，连图标纹理都实测存在）。用户曾指示"代码写死特殊处理"，因无缺口而不需要任何注入/写死
2. 主栏控制模式——**已实现（08-20，纯客户端，服务端零改动，未实机验证）**：
   - 新增 `WInfo/WPlayerInfo.ControlMode.cs`（partial WPlayerInfo）：状态机（IsControlMode/ControlGuid/ControlSlots）+ `OnPetSpellsReceived`/`RefreshControlMode` + `CastControlSkill`（DEST 带坐标/单体带目标/自身裸发）+ `CastControlCommand`（Type 7 命令钮走 WPetActionRequest）+ `GetSkillFxEntity()`（控制实体自动补挂 WSkillRangeComponent）
   - `WPlayer.cs`：`PetActionSkills` setter 放开（private→public）；`UpdatePetActionSkills` 尾部接 `OnPetSpellsReceived`；`UpdateAttrPlayerFarSight` 尾部接 `RefreshControlMode`；`UpdatePlayerFields`/`UpdateAttrFields` 对 UNIT_FIELD_FLAGS 变化接 `RefreshControlMode`
   - `MSkillDoubleDiskHandler.cs`：`initSkillSlot` 控制模式分支 → 新方法 `InitControlModeSlot`（技能钮复用原生 OnDown/OnDrag/OnUp 瞄准流程，WSkillInfo 仅承载 spellId；命令钮按下即发；空槽/Add 钮隐藏）；OnDown 控制模式跳过自动目标筛选（防误选隐形触发器）；OnUp 在 SetWaitSkill 前加控制模式分支直发 CastControlSkill（**绕过 CanSkill/等待队列**——野兽之眼引导态 kStateSinging 会被闸门挡死）；CheckValidOnDown 控制模式豁免 NeedTarget 空目标拦截；CheckValidOnUp 控制模式豁免空气墙（施法点在被控单位周围）；范围圈 Show/Pos/Hide 三处改发 `GetSkillFxEntity()`
   - `WSkillCore.cs` GetCastPoint：控制模式且 `_firer==player` 时以被控单位为原点（RING/LONG/SECTOR/ARROW 全部分支）
   - `WPlayerInfo.cs`：`GetPetActionSkills` 控制模式返回 null（宠物面板自动收起，零 Lua 改动）；`HandlePetAction` 门控 `IsCreature`→`entity != null`（修 WNpc 棋子零发包，问题#8）
   - `UISkillController.cs`：控制模式禁止技能栏翻页
   - **实机一轮反馈修复（08-20）**：①`BuildControlSlots` 过滤误杀——停留命令 Action=0(Type 7) 被当空槽滤掉、3 个反应状态钮(Type 6) 被主动滤掉；改为只滤真空槽（SkillId==0 且 Type∉{6,7}），命令/反应钮都走 CastControlCommand（按下即发，WPetActionRequest 透传 type）。②CD 蒙版——服务端不下发宠物技能 CD（无 SMSG_PET_COOLDOWN；SMSG_PET_SPELLS 冷却列表只建栏时携带且 possess 不带）→ `CastControlSkill` 施法后按 SpellCoolDownsWoW 的 max(RecoveryTime, CategoryRecoveryTime) 本地预测写入 `player.Skill.SetSkillCD`，`refreshCDs` 加控制模式分支 `RefreshControlModeSlotCD` 读它。③宠物技能条布局权威版（CharmInfo.cpp:52-65 InitPetActionBar）：槽 0-2=攻击(2)/跟随(1)/停留(0) Type=7；槽 3-6=法术；槽 7-9=反应(2/1/0) Type=6；空法术槽=Action 0+ACT_PASSIVE(0x01)
   - **象棋实测反馈修复（08-20 五轮）**：①棋子施法不走——`GetCastPoint` 判定顺序：象棋 Move 隐式目标=8（TARGET_UNIT_DEST_AREA_ENTRY）被当 flag 位 OR 进 _targetFlag 后命中 UNIT_PARTY(0x8) 位 → NeedTarget 误判 true → 施法点被取成目标位置（选敌子=敌子脚下/不选=(0,0,0)）；修为控制模式（IsControlMode && _firer==player）下 **IsNeedRange 优先于 NeedTarget**（非控制模式保持原顺序，防冲锋类双语义技能回归）。②目标圈不显示——范围圈 FX 异步加载，被控单位的组件随用随建，首次 Show 先于加载完成被静默跳过；修为 `WSkillRangeComponent.LateUpdate` 形状 FX 加载完成后补显（_showRange 且 _effectType≠NONE 时重新激活）+ 控制模式进入时 `GetSkillFxEntity()` 预热组件。③棋子"有行走动作但不走"——**模型位置同步被 LoadFarSight 关掉**（`SetIsSyncPos(false)` 是给 possess 客户端驱动设计的；象棋是服务端驱动 MovePoint 走格，同步关了服务端的位移到不了模型）；修为 `UpdateMoveLock` 里按驱动模式切换被控单位模型同步：possess（有 DISABLE_MOVE）关同步，魅惑（象棋，无 DISABLE_MOVE）开同步（`SetIsSyncPos(!hasDisableMove)`）。**同步修复后棋子可走格（用户实测通过）**。④目标圈仍不出+无任何 [SkillRange] 日志——**根因（用户实测发现）：其余形状预制体全是空预制体**，只有 YuanXingFanWei_03 有内容；组件的 `_shapeFxLoaded` 全局闸门要求所有形状 FX 都加载出 UObj 才放行 → 空预制体永远加载不出 → 闸门卡死 → onSkillRangePos 整体跳过（无日志）。修复：`onSkillRangePos` 与 LateUpdate 补显均改为**按当前形状的 FX 各自判断 UObj**，不再依赖全局闸门（WSkillRangeComponent.cs）。诊断日志已在验收通过后删除（保留 RefreshControlMode 的异常捕获日志）。⑤目标圈仍不显示的最终根因（诊断日志定位）：`SetFxOnGround` 地面探测从选点上方 +200 码向下单发射线，象棋厅是室内场景，射线命中头顶百码外的上层结构顶面（日志 pos.y=318+ vs 棋子 y=220）；且棋盘区域地板 RoamScene 射线根本打不到（多命中取最近仍选到 ~350 上层）→ 修法双保险：`GetOptimizedGroundHeight` 改 `RaycastNonAlloc` 多命中取离参考高度最近者 + `SetFxOnGround` 加"命中点高于实体 10 码以上按实体高度显示"兜底（高空公共飞行/瞄准低处不受影响）
3. 协议：`WPetCastSpellRequest` 补 DEST 打包分支——**已完成（08-20）**：`WPetHandlerRequest.cs:153` 目标段改为与服务端 `SpellCastTargets::Read`（Spell.cpp:125）逐段对应（UNIT/ITEM→`AppendPacketGuid`，SOURCE/DEST→打包 transportGuid+xyz），`PetCastSkill` 增 `pos` 可选参（内部 `ToServerVector()`）。逐字节核对通过：guid 裸 uint64/castFlags=0 不读附加段/空 transportGuid=单字节 0x00/xyz 三 float。旧调用方（targetFlag=0，拉佐格尔）线上字节变化=少 8 个服务端从不读取的尾随字节，行为不变
4. 移动：**已实现（08-20）**——①`UpdateMoveLock`：控制模式且玩家无 DISABLE_MOVE（象棋类魅惑）→ 锁摇杆；possess 系放行。②被控单位模型同步按驱动模式切换（possess 关/象棋开——修掉"棋子有动作不走"）。③**移动改道 ControlGuid 化（心控补完）**：`WPlayer.MoverGuid` 聚合属性替换全部 13 处 `_farSightGuid>0?...:UID`——控制模式+DISABLE_MOVE 时移动包发改被控单位 GUID（possess 服务端只受理被控单位移动）；新增 `UpdateMoveDrive`（ControlMode.cs）：possess 且无远见（心控）时给被控单位补挂 CharacterController+关同步+设 ModelFarSight（带所有权标记，只拆自己装的；野兽之眼有远见走 LoadFarSight 不重复接管）。象棋类不驱动。注意教训：replace_all 误伤了属性自身回退行造成递归，已修——批量替换后要复查命中数
5. ~~服务端小修：象棋宠条 `ACT_ENABLED`→`ACT_DISABLED`~~ **用户指示不动服务端**——控制模式下宠物面板被抑制，闪烁问题随之不复存在；沉默修复已在（问题#2）
6. 宠物栏联动：**已随模块 2 实现**（GetPetActionSkills 门控 + 缓存恢复）
7. 可选：宠物施法 CD 蒙版（读被控实体 SkillComponent）/ SMSG_PET_CAST_FAILED handler
8. 相机（问题#6）：**已修复（08-20）**——`LoadFarSight` 与解除清理的门控从 `IsPet||IsCreature` 放宽为 `IsPet||IsCreature||IsNpc||IsUnit||IsRole`（WPlayer.cs 两处），不再受棋子 WNpc/Creature 分类不稳定影响（分类来源：属性组件类 WAttrNPC/WAttrCreature 在创建期按 gossip 旗标启发式选择，魅惑摘除 gossip 后会变）

**待实机/实现期确认**：

- 象棋控制中玩家 DISABLE_MOVE 的来源（脚本与魅惑代码均未直接设置；v1 称实测玩家带该 flag——可能来自 30019/39331 的光环，需 `.list auras`+抓 flags 确认）。注意：检测主信号是 farsight==barGuid（象棋有 Bind Sight 必中），DISABLE_MOVE 只是心控的备选信号
- ~~象棋图标纹理是否在 artres~~ **已验证存在**（08-20：FileData.bin 有索引、png 在 `artres/Resources/BakedWoW/interface/icons/`）
- SpellCastTimes/Duration/Levels/CoolDowns/ClassOptions 等小表是否也全量（WSkillCore.Init 的可选依赖，缺了只是细节降级不致命）
- 姿态页偏移与控制模式叠加的边缘 case（如战士被心控）
- 问题#4（单击延迟）、#11（点选触发器/30 码）修法随主栏方案定
- **主栏控制模式实机验收清单**：①象棋控制→主栏变 4 技能+宠物栏不弹；②Move 按住→棋子脚下出射程圈、拖动瞄准（圆心=棋子）、松手走格；③攻击技（先点选敌子——点选问题#11 未修，控王距离内可测）；④解除控制→主栏还原；⑤野兽之眼→主栏变宝宝技能+攻击/跟随/停留命令钮，结束→宠条还原；⑥心控→主栏变怪物技能（无 farsight，走 DISABLE_MOVE 信号）
- **主栏控制模式已知遗留**：Type 6/7 钮无"当前状态"高亮标记（IsDefault 未渲染）；控制技能 CD 是本地预测（可能与服务端真实 CD 有出入，以服务端为准）；**敌我识别已修（08-20 六轮）**：象棋阵营模板 1689/1690（Faction 964/963）只彼此敌对，玩家阵营与它们全友好（FriendGroup 0x1 相交）→ 敌子此前被识别成友方；修法=`WPlayerInfo.GetReactionTo` 在**仅被控单位本身是象棋阵营**时改用其阵营模板判定（用户纠偏：不能全场景替换，否则心控会把队友误判成敌人）。**配套（08-21）血条不显示问题**：`WWoWHUDComponent.InitBaseData` 里血条只对 敌对/冷淡 显示（友好/中立不显示），且反应只在组件创建/CAMP_Change 事件时算一次 → 棋子初始对玩家全是友好=无血条，进入控制后不重算；修法=控制模式进出时 `RefreshAllHudReactions()` 对所有 NPC/怪物发 `WEvent_CAMP_Change` 触发重算（敌方棋子变红+出血条）。**补充（08-21 实测反馈）**：官方默认"友方无血条"导致己方棋子无血条——象棋里己方棋子血量是核心信息，`InitBaseData` 加"控制模式+目标是象棋阵营（1689/1690）→ 强制 _showHpBar=true"（实测敌方红条/己方绿条均正常）点选的两个物理阻碍（隐形触发器挡射线、30 码距离限制）**用户明确不让改**，若实测点选仍不灵再议。**象棋攻击技目标问题（08-21 查实修复）**：用户实测"选中目标放技能却打别的"。结论分两类：①**大部分攻击技（锥形 60/源范围 22,7/附近 38）本来就是服务端自动选目标（官方设计）**——象棋操作就是走位+朝向（Change Facing 调），选中目标对这些技能无意义；②单体技（主教 37455/37456、后 37462 等，隐式目标=25 TARGET_UNIT_TARGET_ANY）本该用选中目标但没生效——双重原因：客户端 `NeedTarget` 对原始目标 ID 按位或误判（25 含 0x1 位→误判 false）→ 裸发包没带目标；服务端 PetHandler 的选中兜底只用于 CheckPetCast 校验、不进 m_targets → 实际执行打棋子当前近战目标（GetVictim）。修复：`WSkillCore.IsNeedUnitTarget()`（按隐式目标类型精确判定：6/21/25/35/45/57/90/95），`CastControlSkill` 单体分支改 `NeedTarget || IsNeedUnitTarget()`，选中目标随 UNIT 段正确进包。**EotB 超距解除=服务端原生机制（08-20 三轮查实）**：`Creature.cpp:822-826`（Creature::Update 中被魅惑单位离 charmer/owner 超出地图视距 → `RemoveCharmAuras()`，视距=conf 的 Visibility.Distance，大陆默认 100 码/副本 170）→ 级联全自动（解附身+空宠条包+farsight 清+引导结束）。**客户端牵引方案（UpdateControlModeLeash）已废弃删除**（用户指出应是服务端通知，查实后确认）。RefreshControlMode 已加整体 try-catch + 进出/PetSpells 包的 `[ControlMode]` 诊断日志（验收后删）
- **"宠物跑远消失+失控"根因（08-20 四轮查实）**：不是服务端解除，是**客户端模型距离裁剪**——`WModelDisplayComponent.getCameraTargetDisplay` 以**玩家**为圆心裁剪（`_CameraCullingDistance=60`，WComponents/Model/WModelDisplayComponent.cs:194），被控宠物跑出 60 码 → `DisplayType=ENone` 模型隐藏 → CharacterController 失效 → 摇杆驱动不了（"走不动"）→ 服务端宠物停在原地（不越视距就不触发 Creature.cpp:823）→ 魅惑吊到引导自然结束。**修复**：远见目标/控制目标（farsight==UID 或 ControlGuid==UID）豁免裁剪返回 ECompelete（:186-191）。修复后宠物可持续操控至超出服务端视距 → 原生解除级联自然发生

**用户指示**：方向已定；实现步骤由用户一步一步引导，不擅自动工。

## 六、排障索引（速查）

- 服务端：`boss_chess_event.cpp`（事件全部逻辑；宠条构建 :1517-1559；OnCharmed 沉默修复 :1517-1585；HandlePieceMove :1903-1924）、`instance_karazhan.cpp:226-271`（沉默施放/清除）、`SpellInfoCorrections.cpp:4349`（39331 属性修正）、`PetHandler.cpp:990/150/340`、`Spell.cpp:125`（SpellCastTargets 线格式）、`Unit.cpp:14759-14800`（魅惑类型分支）、`SpellAuraEffects.cpp:3686/3720/3754`（Possess/PossessPet/Charm 入口）、`Player.cpp:9543/9615/9718/9772`（Pet/Possess/Charm/SendRemoveControlBar 四个 SMSG_PET_SPELLS）、`Player.cpp:12900/13226`（SetClientControl/SetViewpoint）、`CharmInfo.h:60-68`（ACT_* 状态位）。注意：world 库 `spell_dbc` 表只是覆盖/补充层（4491 行），法术真值在服务端二进制 DBC。
- 客户端：`WPlayer.cs`（UpdateAttrPlayerFarSight :1120-1153、LoadFarSight :1623-1646、SendMove* :800-849、UpdatePetActionSkills :1313-1350）、`WPlayerInfo.cs:542`（HandlePetAction）、`WPetHandlerRequest.cs:153-166`（WPetCastSpellRequest）、`WCastSpellRequest.cs:41-63`（DEST 打包参照）、`WSkillCore.cs`（IsNeedRange :420-460、UpRange :1034-1071）、`MSkillDoubleDiskHandler.cs`（OnDown :622/OnDrag :812/OnUp :928）、`WWaitingDataMgr.cs:221-258`（DoCastSkill）、`WSkillRangeComponent.cs:447-518`（范围圈事件）、`TouchObject.cs:199/450`（点选/距离）、`WNetClient.cs:1795`（gossip）、`QuickPetSkillTemplate.lua`（宠物条；双击 :163-228、闪烁 :230-252）、`PlayerInfoMgr.lua:801-835`（GetPetActionSkillByIdx）。
- 客户端表数据：`Assets/artres/Resources/Wow/TableData/*.bytes`（FlatBuffers，**每表独立 XOR key** 见 §四-10；行类 `Table/WoW/Tables/`；通用查询器 `var/query_spelldbc.py`）。
- GM 辅助：`.go xyz -11106.92 -1843.32 229.626 532`、`.list auras`（看 39331）、`.die`（杀选中棋子速胜/速败）、`.instance getbossstate 9`。

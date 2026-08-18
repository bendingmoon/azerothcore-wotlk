# 卡拉赞象棋事件全链路（排查 + 双端修复 + 交接）（2026-08-17/18）

> **状态总览**：根因全部定位，双端修复已全部写入代码，**实机验证只做了部分**（用户反馈"功能还没做好"，剩余问题待新会话继续）。
> 服务端仓库：`D:\UnityWow\azerothcore\azerothcore-wotlk`；客户端：`D:\Unity\clientproj`（C# 在 `Assets/HotUpdate/MoonClient`，Lua 在 `Assets/Scripts/Lua`）。

## 一、官方机制速览（理解一切修改的前提）

- 点棋子 gossip "Control" → 玩家对棋子放 **30019 Control Piece**（Bind Sight + Periodic Dummy + **Charm**）→ `CHARM_TYPE_CHARM` 魅惑（**非 POSSESS**，SpellAuraEffects.cpp:3754）。
- Bind Sight → `SetViewpoint(棋子)` → **PLAYER_FARSIGHT=棋子 GUID** 发给控制者（Player.cpp:13232 AddGuidValue 实现）。**m_mover 恒为玩家**（CHARM 不切 SetClientControl）→ 棋子 GUID 的移动包在 `MovementHandler.cpp:366` 静默丢弃——**摇杆永远不能移动棋子，这是官方设计**。
- `OnCharmed(true)`：移除控制者的 GAME_IN_SESSION 沉默 + 手工发 SMSG_PET_SPELLS 宠物条（boss_chess_event.cpp:1517-1559）。宠物条 4 键 = creature_template_spell 表：①Move（37146=8码 兵/象/车/王；37144=15码 马；37148=20码 后；37151-3 未用）②ChangeFacing（30284=100码）③④攻击技。
- **移动唯一途径 = Move 法术**：地面选点、瞬发、1sCD、3 码半径 Dummy 命中格子触发器（NPC 22519 ×64，带 NOT_SELECTABLE 不可点选）→ `SpellHitTarget` → `HandlePieceMove`（校验格子+步数：兵/象/车/王 1 格、后 3 格、马 2 格）→ `MovePoint` 走位 + 移动冷却（boss_chess_event.cpp:1903-1924）。
- 服务端 `CMSG_PET_CAST_SPELL` 链路完整（PetHandler.cpp:990-1061：caster==GetCharm() 通过、SpellCastTargets 支持 dest、敌方单体无目标时兜底用玩家选中目标 :1040）。
- 结束路径：王死→`HandlePieceJustDied`（赢=DONE+宝箱 / 输=NOT_STARTED）；Restart gossip；REINIT 时棋子 RemoveAllAuras → 解魅惑。

## 二、问题清单与根因（全部已定位）

| # | 现象 | 根因 |
|---|------|------|
| 1 | 棋子不会走路只能攻击 | 官方就走法术不走摇杆（m_mover=玩家）；客户端宠物条施法不带目标（PetCastSkill targetFlag/targetGuid 恒 0），Move 无地面目标静默失败 |
| 2 | 结束沉默(39331)不消失，只有死了才消 | `OnCharmed(false)` 无条件重挂 GAME_IN_SESSION；赢棋路径王的 RemoveCharmedBy 在全清之后、输棋路径 REINIT 在全清之后 |
| 3 | 宠物条全闪烁 | 服务端全给 ACT_ENABLED(193)，Lua 把 193 当"自动释放开"闪；象棋无自动释放概念 |
| 4 | 单击施放延迟 0.8s | Lua 双击判定定时器压单击 |
| 5 | 点"控制"后 gossip 菜单不关 | `OnWNPCGossipComplete`（WNetClient.cs:1795）只调空回调，不关 TalkDlg2 |
| 6 | 相机不切棋子 | LoadFarSight 的 `IsPet||IsCreature` 门槛；棋子创建时 WNpc、魅惑后重分类 Creature（**分类不稳定**，实测两次断点一真一假） |
| 7 | 魅惑中玩家本地可滑动脱同步 | 服务端只加 UNIT_FLAG_DISABLE_MOVE，客户端不解析该 flag |
| 8 | 攻击技能点了没反应 | 原 `if(IsCreature) cast else if(FarSightGuid==0) WPetActionRequest`：棋子为 WNpc 时**两分支都不进，零发包** |
| 9 | Move 按钮按下无反应 | Lua 绑定是生成式（MoonClient_*Wrap.cs），新 C# 方法未注册 → Lua 调用报 attempt to call nil |
| 10 | 有 Begin 日志但无目标圈 | 范围圈组件只挂玩家（WPlayer.cs:704），圈画在玩家脚下；相机切棋子后玩家出画面 |
| 11 | 控兵选不中敌子（控王可以） | ①点选射线不跳过 64 个隐形格子触发器（NOT_SELECTABLE），缓冲 32 无序，Target 被设成隐形触发器；②`setPlayerTarget` 有 30 码距离限制（chooseTargetMaxDistance=30，以玩家为基准），敌子 40-55 码外静默丢弃 |

## 三、已落地的修改

### 服务端（`azerothcore-wotlk`，未编译验证）

- `src/server/scripts/EasternKingdoms/Karazhan/boss_chess_event.cpp` `OnCharmed(false)`：**仅当 游戏阶段∈{PVE/PVP_WARMUP, INPROGRESS_PVE/PVP} 且 事件∈{IN_PROGRESS, SPECIAL} 才重挂 GAME_IN_SESSION**。双条件缺一不可（只查 phase 输棋路径 REINIT 时 phase 未更新会漏；只查 event 则 Restart 路径 REINIT 时 event 未更新会漏）。WARMUP 保留重挂防热身期解除后砍敌方王。RECENTLY_INGAME(10s) 照旧。

### 客户端 C#（`Assets/HotUpdate/MoonClient`）

- `WSkill/WPetGroundCastHandler.cs`（**新增**）：象棋地面选点控制器（MSingleton）。
  - 硬编码：`_spells`（spellId→射程/半径：37146/37151/37152/37153=8y/3，37144=15y/3，37148=20y/3，30284=100y/4）；`_pieceEntries`（12 个棋子 entry，`IsChessPiece(uint)`）。
  - 双轮盘式交互：`Begin`（按下进选点+挂棋子范围圈组件+发 Show 事件+建驱动GO）/ `UpdateAim`（拖动射线投地+以棋子为圆心 clamp+发 Pos 事件到棋子）/ `Confirm`（松手校验 FarSightGuid 未变后 `PetCastSkill(TARGET_FLAG_DEST_LOCATION, dest.ToServerVector())`）/ `Cancel`。未拖动松手=取消。
  - `_rangeComps` 按棋子 GUID 缓存 WSkillRangeComponent（Detached 自动重建）；Show/Pos/Hide 全 fire 到棋子实体（缺失回退玩家）。
  - `WPetGroundCastDriver` 每帧检查控制解除（FarSightGuid 变化/清空）自动 Cancel。
  - **临时诊断日志**：Begin 首行 `AddErrorLog("[WPetGroundCast] Begin spellId=... farSightGuid=...")`，验收后删。
- `WInfo/WPlayerInfo.cs`：`HandlePetAction` 施法门控改 `entity != null`（修零发包），有 `player.Target` 时显式传 `TARGET_FLAG_UNIT + Target.UID`；新增 3 个 Lua 桥接 `PetGroundCastDown/Drag/Up`。
- `WEntity/Models/Request/WPetHandlerRequest.cs`：`WPetCastSpellRequest` 增 transportGuid/pos 可选参；目标段 UNIT/ITEM 打包 GUID、SOURCE/DEST 打包 GUID+xyz（对齐服务端 SpellCastTargets::Read）；targetFlag=0 旧字节兼容。
- `WSkill/WSkillComponent.cs`：`PetCastSkill` 透传 transportGuid/pos。
- `ThirdParty/MoonClient_WPlayerInfoWrap.cs`：手工补注册 `PetGroundCastDown/Drag/Up`（**新生成绑定也会自动带上；给 Lua 调用的 C# 新方法都必须注册 Wrap**）。
- `WEntity/WPlayer.cs`：
  - LoadFarSight 新增**象棋 entry 优先分支**（置于 IsPet||IsCreature 驱动分支之前）：只切相机 + `_isFarSightLoaded` + `_isFarSightWatchNpc=true` + `IsMovable=false`；不加 CC/不劫持模型/不关同步/不发 WFarSightRequest（棋子移动是服务端驱动）。
  - 新字段 `_isFarSightWatchNpc`；`UpdateAttrPlayerFarSight` 清除分支据此还原 `IsMovable=true`（相机恢复原有）。
- `WNetwork/ApplicationLayer/WNetClient.cs`：`OnWNPCGossipComplete` 补 `CallTableFunc("UIMgr","DeActiveUI","TalkDlg2")`（gossip 关窗）。
- `Input/TouchObject.cs`：点选循环跳过 `UNIT_FLAG_NOT_SELECTABLE` 实体；`setPlayerTarget` 控制象棋棋子时跳过 30 码距离检查。

### 客户端 Lua（`Assets/Scripts/Lua`）

- `UI/Template/QuickPetSkillTemplate.lua`：
  - 新增 `chessGroundCastSpells` 硬编码表；OnSetData 扫页识别**象棋模式**（含任一选点法术即整页）。
  - 象棋模式：选点法术按钮→`SetupGroundCast`（MUIEventListener onDown/onDrag/onUp 直驱 C#，`AddClick(nil)` 摘点击壳）；攻击技能→纯 AddClick 即时施放；**整页不闪烁、无双击切自动、无 IsDefault 标记**；RefreshQuick 开头清残留监听。
  - 非象棋宠物条：`HandleClick` 第一次点击**立即施放**（原压 0.8s 定时器），双击窗口保持 0.8s 仅用于识别双击切自动（代价：双击切换时多放一次，用户认可）。

## 四、待实机验证清单（新会话优先做）

前置：重编服务端 + 客户端 C# + **重新编译 Lua（bytes）**。GM 进房：`.go xyz -11106.92 -1843.32 229.626 532`，Medivh gossip 开局，点国王 gossip 控制。辅助：`.list auras`（看 39331）、`.die`（杀选中棋子速胜/速败）、`.instance getbossstate 9`。

1. 控制 → 菜单关 + 相机切棋子 + 人物本地不能滑步。
2. 按住 Move → **棋子脚下出射程圈**；拖动 → 目标圈跟随（射程边钳制）；松手 → 棋子走格（1s 冷却）。各棋子射程：兵/车/象/王 8、马 15、后 20。点一下不拖=取消。
3. Change Facing（第 2 键）同法转向（100y 射程圈很大，知悉）。
4. 攻击技：自身 buff/AOE 直接放；敌方单体**先点选敌子**再放。点选：全场任意敌子可点（触发器不再截胡、无 30 码限制）。
5. DEBUFF：赢(`.die` 敌王)/输(`.die` 己王)/Restart 后控制者沉默消失；中途丢棋子/手动解除/热身期解除 → 仍沉默（原设计）。
6. 解除控制（再 gossip 棋子）→ 相机回玩家、人可走、宠物条消失、无闪烁残留。
7. 普通猎人宠物条回归：单击即时、双击（0.8s 内）切自动、闪烁正常。

## 五、已知风险 / 未做事项

- **范围圈挂棋子未实测**：WND上和 Creature 实体挂 WSkillRangeComponent 的行为、fx 异步加载首帧延迟。
- 方向 B（摇杆自由移动）**明确不做**（服务端 m_mover=玩家是官方设计；强行做会让 _boards 棋盘格状态与实际位置脱节）。
- `AddClick(nil)` 传 nil 清点击列表：实测未报错（若有异常，地面选点按钮会静默失效，换新方案=注册空函数）。
- 点选跳过 NOT_SELECTABLE 的影响面：理论安全（语义即"不可选择"），但属全局行为变更，回归时注意可互动物体。
- `_petActionSkillMap` 旧条目不清空（存量 bug）：象棋条 4 技能时第 5 槽可能有上一条技能栏残留，象棋模式扫描可能误判（极端情况）。
- 服务端改动未编译；codestyle-cpp.py 需 python3（本机只有 py2.7 未跑）。
- 客户端 SpellDbc.bytes / 服务端 spell_dbc 表均为 ~4.5k 行子集，不含象棋技能——象棋数据只能硬编码（C# 与 Lua 两处 + 棋子 entry 表），服务端法术表数据用客户端 FlatBuffers 解析器 `var/parse_client_spelldbc.py`（路径 `Assets/artres/Resources/Wow/TableData`）。

## 六、关键索引（排障速查）

- 服务端：`boss_chess_event.cpp`（事件全部逻辑）、`instance_karazhan.cpp:226-271`（沉默施放/清除）、`SpellInfoCorrections.cpp:4349`（39331 属性修正）、`PetHandler.cpp:990/150/340`（宠物施法/动作）、`Spell.cpp:125`（SpellCastTargets 线格式）、`Unit.cpp:14759-14800`（魅惑类型分支）、`Player.cpp:12900/13226`（SetClientControl/SetViewpoint）。
- 客户端：`WPlayer.cs`（FarSight/相机/Target setter :132）、`WPlayerInfo.cs:542`（HandlePetAction）、`WPetGroundCastHandler.cs`（选点）、`TouchObject.cs:199/450`（点选/距离）、`WNetClient.cs:1795`（gossip）、`WComponents/Staffs/WSkillRangeComponent.cs:449`（范围圈事件）、`QuickPetSkillTemplate.lua`（宠物条）。
- 客户端表数据：`Assets/artres/Resources/Wow/TableData/*.bytes`（FlatBuffers，int XOR 501319815；行类 `HotUpdate/MoonClient/Table/WoW/Tables/`）。
- 世界库验证：棋子/触发器 flags、creature_template_spell 已用 `mysql -uacore -pacore acore_world` 核实。

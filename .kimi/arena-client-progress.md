# 竞技场（Arena）客户端功能开发 — 进度交接

> 用途：跨会话交接。新会话开始时读本文件即可继续。
> 客户端工程：`D:\Unity\clientproj`（MoonClient，C# hot-update + tolua/Lua）
> 服务端工程：`D:\UnityWow\azerothcore\azerothcore-wotlk`（AzerothCore WotLK 3.3.5a，本目录）

## 总体路线（四阶段）

1. **协议层 + WArenaMgr（纯 C#/Lua，无 UI）** ✅ 已完成
2. **PvP 主面板 UI**（战队页签、成员管理、排队入口）— 下一阶段
3. 比赛与结算（结算面板、准备阶段 ready、被邀请弹窗）
4. 外围功能（观察他人竞技场页、建队 charter/petition 流程、体验细节）

## 第一阶段已完成内容（2026-07-29）

### 客户端新建文件

- `Assets/HotUpdate/MoonClient/WEntity/Models/Request/WArenaTeamRequest.cs`
  9 个请求包：Query / Invite / Accept / Decline / Leave / Remove / Disband / Leader / BattlemasterJoinArena（u64 guid + u8 slot + u8 asGroup + u8 isRated）。Roster 请求复用已有的 `WBattleQueryArenaTeamRequest`。
- `Assets/HotUpdate/MoonClient/WEntity/Models/Response/WArenaTeamResponse.cs`
  8 个响应包 + `WArenaTeamEvent` 枚举（0 JOIN / 1 LEAVE / 2 REMOVE / 3 LEADER_IS / 4 LEADER_CHANGED / 5 DISBANDED）。
- `Assets/HotUpdate/MoonClient/WInfo/WArenaMgr.cs`
  `MSingleton<WArenaMgr>`。按 teamId 缓存 `WArenaTeamInfo`；`GetSlotByType`(2/3/5→0/1/2)、`GetTeamBySlot`；邀请状态 `PendingInviterName/PendingTeamName` + `AcceptInvite()/DeclineInvite()`；全部发送方法；`GetLocalTeamId(slot)/GetLocalPersonalRating(slot)` 走 `WEntityMgr.singleton.Player.Attr.GetUnitIntAttr`（每槽 7 个 int32）。数据经 `MGame.singleton.LuaEngine.CallFunc("ModuleMgr.ArenaMgr.OnXxx", ...)` 推 Lua，复杂负载用 `JsonConvert` + 专用 payload 类（**不要直接序列化 response 包对象**，基类有公开 `Command`/`Content` 属性会污染 JSON）。
- `Assets/HotUpdate/MoonClient/WEntity/Registers/WArenaRegister.cs`
  8 个 handler 的注册 + 处理转发（仿 `WGuildRegister` 模式）。
- `Assets/Scripts/Lua/ModuleMgr/ArenaMgr.lua`
  `module("ModuleMgr.ArenaMgr", package.seeall)`；缓存 `g_teams/g_slotTeams/g_pendingInvite/g_inspectTeams`；8 个事件经 `EventDispatcher:Dispatch` 派发；`OnInit/OnReconnected`；`GetTeam/GetTeamBySlot/GetPendingInvite`。

### 客户端修改文件

- `WNetwork/ApplicationLayer/WNetClient.cs` — 竞技场注册已全部剥离（现无任何 Arena 代码）。
- `WEntity/Mgr/WEntityMgr.cs` — 挂载 `_arenaRegister`（字段/Init new/RegisterHandler 调用/Uninit 释放），同 `_guildRegister`。
- `WEntity/WEntity.cs` — `UpdateArenaTeam()`（~1331 行）roster 请求后加 `WArenaMgr.singleton.QueryArenaTeam(arenaTeamID)`。
- `Lua/ModuleMgr/MgrMgr.lua` — `_initMap` 加 `["ArenaMgr"] = 1`。

### Opcode 核对

17 个竞技场 opcode 数值已与服务端 `src/server/game/Server/Protocol/Opcodes.cpp` 逐一比对，**客户端 `WNetwork/Enums/WWorldOpcode.cs` 全部正确，未改**。⚠️ opcode 以 `WNetwork/Enums/WWorldOpcode.cs` 为准，`Servers/World/Models/Enums/WorldOpcode.cs` 是废弃副本，不要用。

### 两处与服务端源码核实过的实现细节

- `MSG_INSPECT_ARENA_TEAMS` 的 guid 是**原始 u64**（`operator<<(ByteBuffer, ObjectGuid)`），不是 PackedGuid；`SMSG_ARENA_TEAM_EVENT` 末尾 guid 同为原始 u64，用 `IsDataLeft()` 判断是否读取。
- `SMSG_ARENA_ERROR`：u32 unk(0) + u8 队伍类型（2/3/5），仅 unk==0 时有第二字段。

## 关键服务端协议速查（第二阶段要用）

槽位：0=2v2, 1=3v3, 2=5v5；队伍 type 值 = 2/3/5；每队容量 = type×2。

| 包 | 值 | 布局 |
|---|---|---|
| SMSG_ARENA_TEAM_QUERY_RESPONSE | 0x34C | u32 teamId, cstr name, u32 type, u32 bgColor, u32 emblemStyle, u32 emblemColor, u32 borderStyle, u32 borderColor |
| SMSG_ARENA_TEAM_STATS | 0x35B | u32 teamId, rating, weekGames, weekWins, seasonGames, seasonWins, rank |
| SMSG_ARENA_TEAM_ROSTER | 0x34E | u32 teamId, u8 unk=0, u32 count, u32 type; 每成员: guid u64, u8 online, cstr name, u32 isCaptain(0=队长), u8 level, u8 class, u32 weekGames, weekWins, seasonGames, seasonWins, personalRating |
| CMSG_BATTLEMASTER_JOIN_ARENA | 0x358 | u64 battlemasterGuid + u8 slot + u8 asGroup + u8 isRated（rated 必须 asGroup=1 且赛季进行中；asGroup=0 即练习赛） |
| SMSG_BATTLEFIELD_STATUS | 0x2D4 | 已有 handler；驱动排队 UI（含 arenaType、isRated） |
| MSG_PVP_LOG_DATA | 0x2E0 | 已有 handler，arena 分支已解析（两队 rating 变化/MMR/个人 KDA），结算 UI 直接消费 |
| PLAYER_FIELD_ARENA_TEAM_INFO_1_1 | — | 3 槽 × 7 int32：ID/TYPE/MEMBER(0=队长)/GAMES_WEEK/GAMES_SEASON/WINS_SEASON/PERSONAL_RATING |
| PLAYER_FIELD_ARENA_CURRENCY | — | 竞技场点数（上限 10000） |

服务端关键文件（需要改服务端行为时）：
`src/server/game/Battlegrounds/ArenaTeam.{h,cpp}`、`ArenaTeamMgr`、`ArenaSeason/ArenaSeasonMgr`、`Handlers/ArenaTeamHandler.cpp`、`Handlers/BattleGroundHandler.cpp:668 HandleBattlemasterJoinArena`、`Battlegrounds/Arena.cpp:207 EndBattleground`（Elo 结算）。

## 第二阶段任务清单（待做）

1. PvP 主面板：三个战队页签，显示队名/旗帜/队伍分/个人分/周战绩/赛季战绩/排名。UI 三件套模式：`UI/Panel/XxxPanel.lua`（绑定）+ `UI/Ctrl/XxxCtrl.lua`（`class("XxxCtrl", UI.UIBaseCtrl)`，`BindEvents` 里 `self:BindEvent(mgr.EventDispatcher, ...)`) + `UI/Template/`；面板名注册 `UI/UIConst.lua` 的 `CtrlNames`；`UIMgr.ActiveUI/DeActiveUI`。
2. 成员列表 + 操作按钮（邀请/踢人/让位/离队/解散，按 isCaptain 显隐）。可参考公会面板（`UI/Ctrl/` 下 Guild 系）和 `TeamCtrl.lua`。
3. 竞技场点数/荣誉显示（角色面板 PvP 页签或并入主面板）。
4. 排队入口：练习赛单排 / 评级赛组排。⚠️ `WBattleMgr.BattleJoin` 的 bgTypeId 硬编码=3（阿拉希），竞技场排队必须用新的 `WBattlemasterJoinArenaRequest`，不能复用 BattleJoin。
5. 排队状态 UI：复用现有战场排队弹窗（`SMSG_BATTLEFIELD_STATUS` 的 isRated/arenaType 已解析）。

## 战队登记表（Petition/Charter）流程 ✅ 已完成（2026-07-29，插队于第二阶段）

**问题根因**：竞技场组织者 Bip Nigstrom（entry 19861，加基森）npcflag=262144（纯 PETITIONER，无 GOSSIP 位），`gossip_menu_id=474` 在 DB 里不存在 → `PlayerGossip.cpp:42` 回退 menu 0 → 匹配到工会文案选项。官方客户端对"纯 petitioner 无 GOSSIP"的 NPC **不发 GOSSIP_HELLO，直接发 `CMSG_PETITION_SHOWLIST`**，所以官方直接弹购买窗口。服务端 petition 链路（购买/签名/交还/`ArenaTeam::Create`）完整，**零改动**。

**客户端改动（全部仿工会流程，工会路径逐字节不变）**：

- `WEntity/Models/Request/WGuildHandlerRequest.cs`：新增 `WGuildPetitionShowListRequest`（CMSG_PETITION_SHOWLIST，仅 u64 guid）；`WGuildPetitionBuyRequest` 加 `clientIndex=0` 可选参数（包尾第 10 个空串后的位置，服务端 `PetitionsHandler.cpp:60` 读它区分 2v2/3v3/5v5）；`WGuildTurnInPetitionRequest` 加 `arena=false` + 5 个队徽 uint32 可选参数（服务端竞技场分支 `:772` 必读，工会分支不读）。
- `WInfo/WNPCMgr.cs`：`OnSelectNPC` 加入口分岔——实体 npcflag 有 PETITIONER 且无 GOSSIP → 直接发 `WGuildPetitionShowListRequest`（工会注册员 786433 有 GOSSIP 位，不受影响）；`OnSelectGossipSpecial` 加假菜单 1627 分发：162701/2/3 购买（clientIndex=id-162700）、162711/2/3 提交（clientIndex=id-162710）。
- `WEntity/Registers/WGuildRegister.cs`：`GuildPetitionShowListResponse` 按列表 `CharterEntry` 分岔——含 23560/61/62 → 构造假菜单 1627（购买组文案带价格/签名数 + 提交组按 `WContainerMgr.GetItemByItemId` 扫背包动态生成），否则维持工会菜单 1626；`GuildTurnInPetitionResultsResponse` 按 `WGuildMgr.LastTurnInIsArena` 区分提示语。
- `WInfo/WGuildMgr.cs`：新增"竞技场战队登记表"region——静态helper `IsArenaCharterEntry/GetArenaClientIndexByCharter/GetArenaCharterEntry/GetArenaBracketName`；`ShowApplyCreateArenaConfirm(clientIndex)`（走 Lua `ModuleMgr.GuildMgr.ShowApplyCreateArenaConfirm`）；`ConfirmBuyArenaPetition(name, clientIndex)`；`SendTurnInArenaPetition(clientIndex)`（找背包里的表 → 队徽参数暂全 0，队徽 UI 后续做）；`LastTurnInIsArena` 属性。
- `WEntity/WItem.cs:66`、`WInfo/WContainerMgr.Equip.cs:24`：charter 特判从仅 GUILD_CHARTER 扩展到三张竞技场表（进包自动 PetitionQuery / 点表弹签名面板）。
- `WInfo/WTeamMgr.cs` + `ThirdParty/MoonClient_WTeamMgrWrap.cs`（手写补）+ `Lua/UnityLuaAPI/MoonClient_WTeamMgr.lua`：`ConfirmBuyArenaPetition(name, clientIndex)` 暴露给 Lua。
- `Lua/ModuleMgr/GuildMgr.lua`：新增 `ShowApplyCreateArenaConfirm(content, clientIndex)`（仿工会确认框，回调 `WTeamMgr:ConfirmBuyArenaPetition`）。

**签名环节**：`CMSG_OFFER_PETITION`/`CMSG_PETITION_SIGN`/`SMSG_PETITION_SHOW_SIGNATURES` 服务端与工会共用。

**服务端 bug 修复（2026-07-29，PetitionMgr）**：买表后若服务端重启，签名被静默拒绝（无任何错误包/提示）。根因：`World.cpp` 先 `LoadPetitions()`（为每张表创建空 Signatures 对象）后 `LoadSignatures()`，而后者开头 `SignatureStore.clear()` 把刚创建的空对象全清掉，只给 `petition_sign` 有行的表重建——无签名记录的表丢失签名对象，`HandlePetitionSignOpcode` 的 `!signatures` 分支静默 return（工会表同病）。修复：删除 `PetitionMgr::LoadSignatures` 里的 `SignatureStore.clear()`。排查用的分支日志已还原（2026-07-29 创建战队全流程验证通过）。

**签名面板（2026-07-29 第二阶段补做，竞技场独立于工会面板）**：

- 工会/竞技场判定依据：`SMSG_PETITION_QUERY_RESPONSE` 第 7 字段（工会=0，竞技场=2/3/5，服务端 `PetitionsHandler.cpp:297-309`）。客户端 `WGuildPetitionQueryResponse` 已扩展解析出 `CharterType` + `SignsNeeded`；`WGuildPetition` 加同名字段（`CharterType`：-1 未知 / 0 工会 / 2/3/5 竞技场赛制）。
- 时序处理：收到 `SMSG_PETITION_SHOW_SIGNATURES` 时若 `CharterType` 未知（被邀请方没持有登记表），`WGuildMgr.TryShowPetitionPanel` 先补发 `CMSG_PETITION_QUERY` 并置 `_pendingShowSign`，`SetPetition` 回调里再弹面板；换登记表时重置 `CharterType=-1`。
- 新面板三件套：`ArenaSignatures.prefab`（复制 GuildSignatures.prefab 改 YAML 文案：竞技场战队/竞技场战队队长/战队重命名/关闭/Tips；新 guid meta）、`UI/Panel/ArenaSignaturesPanel.lua`、`UI/Ctrl/ArenaSignaturesCtrl.lua`（按 `SignsNeeded` 只显示 1/2/4 个签名槽，多余隐藏；标题"X战队登记表"、名称值"X,(2v2)"）；`UIConst.lua` 注册 `CtrlNames.ArenaSignatures`。
- `GuildMgr.ShowGuildPetition` 按 `CharterType>0` 分岔打开 ArenaSignatures 或 GuildSignatures；新增 `CloseArenaSignaturesPanel`；`UpdatePetitionMember` 非本人签名后按类型关对应面板。
- ⚠️ `Resources/Lua/**.lua.bytes` 是构建产物，由 Unity 管线从 `Scripts/Lua` 生成，不要手改。
- 待做：重命名按钮（工会/竞技场面板都还没接 `MSG_PETITION_RENAME`）；~~交还时的队徽选择 UI（现默认 5 个 0）~~ 已做简化版（见下）。

**队标选择面板 ArenaEmblem（2026-07-29，简化版）**：

- 流程：点 162711/2/3 提交 → `WGuildMgr.SendTurnInArenaPetition` 不再直接发包，改为缓存 `_pendingArenaTurnInGuid` 并 `CallFunc("ModuleMgr.GuildMgr.ShowArenaEmblem")` → 面板点"接受" → Lua `WTeamMgr:ConfirmTurnInArenaPetition(底色,图标,图标颜色,镶边,镶边颜色)` → C# 发 `WGuildTurnInPetitionRequest(guid, true, 5参数)`。
- 面板三件套：`ArenaEmblem.prefab`（复制 ArenaSignatures.prefab 改文案：选择战队队标/图标样式/镶边样式/颜色选择/接受/取消；新 guid meta）+ `UI/Panel/ArenaEmblemPanel.lua` + `UI/Ctrl/ArenaEmblemCtrl.lua`；`UIConst.lua` 注册 `CtrlNames.ArenaEmblem`；`GuildMgr.ShowArenaEmblem` 打开。
- 简化版取舍（素材限制）：**客户端没有 WoW `Textures/GuildEmblems` 贴图集，无法渲染真实旗帜预览**。图标选择复用 RO 公会图标网格 `GuildIconSelect`（`GuildIconSelectCtrl` 加 `openType==2` 分支回调 `ArenaEmblemCtrl:SetIcon`，图标 id 取 `GuildIconTable`）；颜色为 7 个预定义色按钮（白/红/黄/绿/蓝/紫/金，id 取官方色表低段），富文本 `<color>■■■■</color>` 色块预览，两个颜色目标行（底色/图标颜色）点击切换编辑目标（▶ 标记）；**镶边样式/镶边颜色不提供选择，固定传 0**。服务端不校验这些值，仅存库。
- `WTeamMgr.ConfirmTurnInArenaPetition(int×5)` + 手写 wrap + EmmyLua 桩已同步。
- ⚠️ 文本行点击：`MLuaUICom.AddClick` 要求节点挂 Unity `Button` 组件（否则静默不生效）。已把 ArenaEmblem.prefab 里 9 个成员行文本 + 图标样式值文本的 `m_RaycastTarget` 改为 1；**需在 Unity 编辑器里给 `Name/Value` 和 `Members/Member1~9` 节点手动 Add Component → UI → Button（Transition 建议 None）**，之后现有 Lua 点击逻辑即可工作。
- 完整还原官方旗帜预览的前提：从 WoW 客户端数据导入 `Textures/GuildEmblems/*` 贴图。

**待实测**：点 Bip Nigstrom 直接弹 2v2/3v3/5v5 购买列表 → 买表 → 签名（2v2 需 1 人）→ 再点 NPC 出现"提交你的 XvX 战队登记表"→ 交还建队成功提示 + `WArenaMgr` 收到队伍数据。回归验证：工会注册员全流程不变。

## 关键服务端 petition 速查（调试用）

- `src/server/game/Handlers/PetitionsHandler.cpp`：`HandlePetitionShowListOpcode`:820（`IsTabardDesigner()` 分工会/竞技场）→ `SendPetitionShowList`:830；`HandlePetitionBuyOpcode`:32（clientIndex 在 :60 读取，1/2/3=2v2/3v3/5v5；要求满级 :94、该 bracket 无队 :122）；`HandleTurnInPetitionOpcode`:647（**不校验 NPC**；竞技场分支 :772 额外读 5 个 uint32 队徽 → `ArenaTeam::Create`:778 并拉入全部签名者）。
- charter 物品：工会 5863；竞技场 23560/23561/23562（`PetitionMgr.h:29`）。签名数=type-1（2v2 需 1 人）。价格 `worldserver.conf`：`ArenaTeam.CharterCost.2v2=80G / 3v3=120G / 5v5=200G`。
- charter 类型枚举 `WorldSession.h:274`：GUILD=9、2v2=2、3v3=3、5v5=5。

### 第二阶段开工前必做

- ~~**tolua wrap**~~ ✅ 已完成（2026-07-29，手写，未经 Unity 重新生成）：
  - 新建 `Assets/HotUpdate/MoonClient/ThirdParty/MoonClient_WArenaMgrWrap.cs`（仿 `MoonClient_WBattleMgrWrap`）和 `MoonCommonLib_MSingleton_MoonClient_WArenaMgrWrap.cs`（提供 `singleton`/`IsInited`/`OnLogout`）。
  - `LuaBinderOfMoonClient.cs` 在 WBattleMgr 两行后注册上述两个 wrap。
  - `MoonClientExportSettings.cs` 的 `CustomTypeList` 末尾加 `_GT(typeof(WArenaMgr))`（将来真正重新生成时不会丢）。
  - 新建 `Lua/UnityLuaAPI/MoonClient_WArenaMgr.lua`（EmmyLua 注解桩，仅供 IDE，运行时不加载）。
  - `Lua/Common/define.lua` 加 `WArenaMgr=MoonClient.WArenaMgr.singleton`（在 WBattleMgr 行后）。
  - Lua 可用 API：`QueryArenaTeam/QueryRoster/GetSlotByType(静态)/GetLocalTeamId/GetLocalPersonalRating/Invite/AcceptInvite/DeclineInvite/Leave/Remove/Disband/SetLeader/JoinArena` + 只读属性 `PendingInviterName/PendingTeamName`。
  - **未暴露** `GetTeam/GetTeamBySlot`（返回 `WArenaTeamInfo` 未 wrap；Lua 队伍数据走 `ArenaMgr.On*` 事件的 JSON 缓存）和 `On*` 消息处理方法（C# 内部用）。
  - 新 `.cs/.lua` 的 `.meta` 由 Unity 下次打开工程自动生成。

## 注意事项 / 坑

- 客户端有 RO 原栈（`M` 前缀 + protobuf）和 WoW 栈（`W` 前缀）两套，已有 "Arena/Pvp" 命名模块全是 RO 玩法，**不要复用也不要改 RO 栈**。
- C#→Lua 走 `LuaEngine.CallFunc("ModuleMgr.ArenaMgr.OnXxx", json)`；Lua Mgr 须在 `MgrMgr.lua` 注册否则 CallFunc 找不到模块。
- 未做任何构建/编译（按约定不主动编译）。改动未实测，首次跑起来后优先验证：进游戏有战队 → 看日志确认 ROSTER/STATS/QUERY 三个包到达 `WArenaMgr`。
- 邀请流程：收到 `SMSG_ARENA_TEAM_INVITE` → 弹确认 → Accept/Decline 发空包（0x351/0x352）。
- 离队限制：队长有队员时不能 leave（服务端报错），队长独自 leave=解散。

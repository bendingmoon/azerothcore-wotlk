# 竞技场（Arena）客户端功能开发 — 进度交接

> 用途：跨会话交接。新会话开始时读本文件即可继续。
> 客户端工程：`D:\Unity\clientproj`（MoonClient，C# hot-update + tolua/Lua）
> 服务端工程：`D:\UnityWow\azerothcore\azerothcore-wotlk`（AzerothCore WotLK 3.3.5a，本目录）

## 总体路线（四阶段）

1. **协议层 + WArenaMgr（纯 C#/Lua，无 UI）** ✅ 已完成
2. **PvP 主面板 UI**（战队页签、成员管理、排队入口）✅ 已完成（2026-07-30，未实测）
3. 比赛与结算（结算面板、准备阶段 ready、被邀请弹窗）— 下一阶段
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

## 第二阶段已完成内容（2026-07-30，未实测）

### C# 改动（客户端 `D:\Unity\clientproj`）

- `WInfo/WBattleMgr.cs` `BattleStatusChange`：
  - 阿拉希的"重复 StatusID 即清缓存"去重（:46-57）**仅对非竞技场生效**（竞技场状态包周期性下发，套用会误清）。
  - 新增 `STATUS_NONE`（简单状态包=队列移除）→ 调 `OnBattleChange`，Lua 侧 `ON_BATTLE_PANEL` 可关闭排队 UI。
  - `OnWaitInQueue` 增传 `ArenaType, IsRated`；`OnWaitForJoin` 增传 `ArenaType, IsRated`，且 `MapWoW` 查不到（竞技场地图 559/562/572/617/618）时 mapName 回退"竞技场"。
- `WInfo/WArenaMgr.cs` 新增 helper（均走 `Attr.GetUnitIntAttr`）：`GetLocalArenaPoints`（0x046A）、`GetLocalHonor`（0x0469）、`GetLocalTodayKills`/`GetLocalYesterdayKills`（0x0435 TWO_SHORT 低/高位）、`GetLocalLifetimeKills`（0x0438）、`GetLocalTodayHonor`（0x0436）、`GetLocalYesterdayHonor`（0x0437）。
- `ThirdParty/MoonClient_WArenaMgrWrap.cs` + `Lua/UnityLuaAPI/MoonClient_WArenaMgr.lua`：上述 7 个方法已注册/补桩。
- `WInfo/WNPCMgr.cs` **假菜单 1628（竞技场军官排队）**：
  - `OnSelectGossip` 的 `GOSSIP_OPTION_BATTLEFIELD(12)` 分支（原"开发中"提示）→ `TryShowArenaBattlemasterMenu(optionText)`：选项文本含"竞技场"/"arena" 才拦截（战场军官不受影响），构造假菜单 1628（162801/2/3=练习赛单排恒定 3 项；162811/2/3=评级赛，仅对应槽位 `GetLocalTeamId>0` 时生成）→ `DeActiveUI("TalkDlg2")` + `OnSetGossipMenuResponse`。
  - `OnSelectGossipSpecial` 分发 → `JoinArenaQueue(slot, isRated)`：评级赛校验 `WTeamMgr.GetMyGroupInfo().LeaderGuid == Player.Attr.UID`（非队长提示"评级赛需要由小队队长发起"）；发 `WArenaMgr.JoinArena(军官guid, slot, asGroup, isRated)` 后关对话。

### Lua 改动

- `ModuleMgr/BattleMgr.lua`：`OnWaitInQueue/OnWaitForJoin` 接收 arenaType/isRated；新增 `g_arenaQueueInfo` 缓存（ArenaQueue 面板初值）与 `GetArenaBracketName`；**竞技场且 Battle 面板未激活时**：WAIT_QUEUE 打开 `ArenaQueue` 面板，WAIT_JOIN 全局弹 YES_NO 确认（BattleConfirm true/false）。Battle 面板激活时维持原逻辑，AB 行为不变。
- `ModuleMgr/ArenaMgr.lua`：`CacheTeam` 改为**字段合并**（原实现 QUERY/STATS/ROSTER 三包互相覆盖缓存）；新增 `GetSlotDisplayInfo(slot)`（本地字段+缓存汇总）、`IsLocalCaptain(slot)`（roster `IsCaptain` 是 **bool**，true=队长）、`ShowPvpPanel()/ClosePvpPanel()`。
- **PvpArena 主面板三件套**（布局按官方 PvP 框：右侧竖排 PvP/战场两页签，仿 `SkillLearningCtrl` 模式但页签用普通按钮+金色高亮，未用 Unity Toggle）：
  - `artres/Resources/UI/Prefabs/PvpArena.prefab`（新 guid meta `85308420b8ab4bafa6d0b4cd887cbcdc`，620×860 窗口；41 个绑定组件；成员行/战队槽位/页签均带 Button）。
  - `UI/Panel/PvpArenaPanel.lua`、`UI/Ctrl/PvpArenaCtrl.lua`。
  - PvP 页：荣誉统计 5 行（本日/昨天/总计击杀+荣誉、荣誉点数、竞技场点数）+ 三个战队槽位行（点击进详情；无队点击提示去组织者建队）。
  - 战队详情：队名(赛制)头、队伍分/个人分/本周/赛季/排名、10 行成员列表（点击选中▶，显示名字[队长]/职业/本周/赛季/个人分/离线）、按钮：添加成员(队长)、踢出(队长+选中非己)、让位(同前)、离队(队长有队员时拦截提示)、解散(队长)、返回。
  - 战场页：阿拉希盆地行 + `BtnJoinSolo`/`BtnJoinGroup`（复用 `WBattleMgr:BattleJoin`，bgTypeId=3 不变）。
  - `OnActive` 对有队槽位补拉 `QueryArenaTeam`/`QueryRoster`；绑 `ON_ARENA_TEAM_QUERY/STATS/ROSTER/EVENT` 刷新；解散/离队后自动退回汇总。
- **ArenaQueue 排队小面板三件套**：`artres/Resources/UI/Prefabs/ArenaQueue.prefab`（新 guid meta `162bb2fc7e494a24a048d2e6ffe21e94`，400×220；绑定 Title/LabBracket/LabWaitTime/LabAvgWait/BtnLeave）+ `UI/Panel/ArenaQueuePanel.lua` + `UI/Ctrl/ArenaQueueCtrl.lua`（`Update()` 每帧累加已等待；离开→`BattleConfirm(false)`；`ON_BATTLE_PANEL`/`ON_BATTLE_JOIN` 自关）。
- `UI/UIConst.lua`：`CtrlNames` 加 `PvpArena`（:234）、`ArenaQueue`（:235）。

### 已知限制 / 待做

- ~~面板暂无主界面入口按钮~~ ✅ 已做（2026-07-30，见下"主界面 PVP 入口"）。
- 被邀请确认弹窗未做（第三阶段）；邀请后对方只能由队长告知，暂无法 Accept。
- PvpArena/ArenaQueue prefab 未经 Unity 打开验证（YAML 静态自检已过）；`.lua/.cs` 新文件 .meta 由 Unity 自动生成。
- 未编译（按约定）。首次实测清单：①点竞技场军官出 1628 菜单→练习赛单排→ArenaQueue 计时→弹确认进场；②评级赛（有队+组队，队长发起；非队长/无队拦截提示）；③PvP 面板荣誉区数值、三槽位、详情成员操作全链路；④战场页阿拉希单排/组排；⑤回归：AB MatchBtn、工会注册员、买表建队流程。

### 主界面 PVP 入口（2026-07-30）

- 原悬浮「参加匹配」按钮（`Battle.prefab` 的 `MatchObj/MatchBtn`，点击弹"单人匹配/队伍加入"对话框）已从主城/野外隐藏：`BattleCtrl.OnInitPanel` 改为仅 `MStageEnum.BattlePre`（战场等候区）显示。其阿拉希单排/组排功能已由 PvpArena 战场页签的 `BtnJoinSolo/BtnJoinGroup` 承接（`WBattleMgr:BattleJoin` guid=0，任意场景可排，同官方 PvP 框）。
- 新入口为 OpenSystem 数据驱动的主界面右竖排按钮（与"地下城"同区）：
  - `ModuleMgr/OpenSystemMgr.lua` 内联表新增 **Id=190**（Title="PVP"、SystemPlace=3、SortID=4 排在地下城后、FunctionOrder="OpenPvpArenaPanel"，图标暂用观战 `UI_Icon_Guanzhan02.png`，后续可换）。
  - `ModuleMgr/SystemFunctionEventMgr.lua` 新增 `OpenPvpArenaPanel()` → `ArenaMgr.ShowPvpPanel()`。
  - `ModuleMgr/SceneEnterMgr.lua` 野图 `MainIcon` 串尾加 `|190`（显隐过滤）。
  - ⚠️ 开放状态由 RO 服务端 `opensys_ids` 下发，190 无服务端来源，故 `ArenaMgr.OnInit` 里 `OpenSystemMgr.ForceSetOpenSystemState(190, true)` 本地强制开放（OnLogout 清空，每次登录重设）。
- 按钮文字由 `MainButtonTemplate` 取 OpenSystem.Title 直接显示"PVP"；点击经 `GetSystemFunctionEvent(190)` 分发（含 `IsSystemOpen` 检查，已被强制开放通过）。

## 第二阶段任务清单（原始，已全部完成）

1. ~~PvP 主面板~~ ✅（按用户要求改为 PvP/战场双页签 + 战队详情子视图）
2. ~~成员列表 + 操作按钮~~ ✅
3. ~~竞技场点数/荣誉显示~~ ✅（并入主面板 PvP 页荣誉区）
4. ~~排队入口~~ ✅（NPC 军官假菜单 1628；服务端 `BattleGroundHandler.cpp:688` 要求有效军官 guid，未做面板直排）
5. ~~排队状态 UI~~ ✅（独立 ArenaQueue 面板，复用 `SMSG_BATTLEFIELD_STATUS` 事件链）

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

## PvP 主面板官方化重建（2026-08-02，未实测）

**背景**：用户在 Unity 里按官方 PvP 框截图重建了 `PvpArena.prefab`（绑定已重排，`PvpArenaPanel.lua` 已由生成脚本同步，32 个绑定）。战队详情窗 `PvpArenaTeam.prefab` 由用户自己搭建（后续见"战队详情窗接通"一节）。`PvpArenaInvitation/Recruit/CreateRoom.prefab` 是 RO 面板复制品（素材 donor），勿动。

**prefab 结构**（节点树）：`TogPanel`(TogPvp/TogBattle，各带 ON/OFF 高亮子节点，UIToggleEx) + `Panel`(Title/BtnClose) + `PvpView`(HonorPanels：HonorTitle.Value=HonorPoint、HonorColumns 表头、HonorKillNum(Today/Yesterday/Sum)、HonorValue(Today/Yesterday/Sum)；ArenaPanels：ArenaTitle.Value=ArenaPoint、**Arena2V2Panel**(Empty/Team 完整子树：Name/Level/Columns/Bs·Sf·CcValue/FlagBox(Bg/Icon)/BtnArena2V2)、Arena3V3Panel(仅 Empty)、Arena5V5Panel(仅 Empty)) + `BattleView`(TogAB、BgTip、BattleDetailPanel、BtnCancel/BtnJoinGroup/BtnJoinSolo)。

**本次改动**：

- `UI/Ctrl/PvpArenaCtrl.lua` **全部重写**：
  - `SLOT_UI` 槽位→绑定名映射表（含 3v3/5v5 预留名，绑定不存在时判空跳过）。
  - PvP 页真实数据：荣誉点数/本日·昨天·总计击杀/本日·昨天荣誉（总计荣誉官方为 "-"）、竞技场点数；槽位显隐（Team/Empty）、队名/战队等级/本周比赛/胜-负/个人场次(百分比=个人本周场次÷队伍本周比赛)。
  - 页签：`TogPvp/TogBattle.TogEx.onValueChanged` 驱动 `UpdateMainTab`；`OnActive` 默认选中 PvP 并补拉各队 QUERY/ROSTER。
  - 战场页：`TogAB` 默认选中；`BtnJoinSolo/Group`→`WBattleMgr:BattleJoin`（排队中拦截）；`BtnCancel` 排队中→`BattleConfirm(false)` 离列，否则关面板；排队状态由 `ON_BATTLE_QUEUE`(新增 arenaType 参数过滤，只收 arenaType==0)/`ON_BATTLE_JOIN`/`ON_BATTLE_PANEL` 驱动，进页签时 `WBattleMgr:GetBattleStatus()` 主动拉状态。
  - 槽位点击：无队→建队提示；有队→"战队详情界面制作中"占位（详情窗由用户制作，做好后接 `OnSlotClick`）。
- `ModuleMgr/BattleMgr.lua`：①`OnWaitForJoin` 补 **AB（arenaType==0）在 Battle 面板未激活时的全局 YES_NO 进场确认**（原来只有竞技场分支，主城排 AB 没人弹确认）；②`ON_BATTLE_QUEUE` 派发加传 `arenaType, isRated`（既有监听 BattleCtrl/ArenaQueueCtrl 只用前两参，兼容）。
- C# `WInfo/WArenaMgr.cs` 新增 `GetLocalWeekGames(slot)`（字段索引 3=GAMES_WEEK）；`MoonClient_WArenaMgrWrap.cs` 手写注册；`UnityLuaAPI/MoonClient_WArenaMgr.lua` 补桩。
- **搁置**：`WPVPLogDataResponse` 竞技场分支解析与服务端实际布局不符（ratingLost/ratingWon/MMR + cstr 队名 + u8 teamId 玩家行），第三阶段修。

**用户在 Unity 侧待做**：

1. **BgTip 节点未绑定**：需挂 `MLuaUICom`（Name=`BgTip`）并把其 fileID 加进根 `MLuaUIPanel` 的 ComRefs，重新生成 Panel.lua（Ctrl 已判空，未绑定时只是不显示排队文字）。
2. ~~3v3/5v5 槽位 Team 子树~~ ✅ 已完成（见下节）。
3. ~~PvpArenaTeam.prefab 详情窗~~ ✅ 已完成（见下节）。

**未实测清单**：①PvP 页荣誉/点数/槽位数值与显隐（有队/无队）②页签切换与 ON/OFF 高亮 ③战场页单排/组排→全局确认进场→排队状态显示（BgTip 绑定后）→BtnCancel 离列 ④AB+竞技场同时排队时 `_bgQueued` 单标志可能误清（已知小边界）⑤回归：ArenaQueue 面板、BattleCtrl 战场内流程。

## 战队详情窗 PvpArenaTeam 接通（2026-08-02 第二轮，未实测）

**prefab 现状**（用户已搭好，52 个绑定；Panel.lua 已生成）：`Panel`(Title>`Name`(=TeamName绑定)+`Text`(=TeamType绑定，"(2v2)")/BtnClose) + `PvpWeekPanel`(PvpWeekColumns 四列表头[比赛/胜-负/级别/战队等级] + PvpWeekValue(TogDate(TogSeason/TogWeek 带 ON/OFF)、PvpWeekBs/Sf/Jb/DjValue)) + `LetterText`（成员表**表头行**：名字/职业/场次/胜-负/等级，静态勿动） + `ScrollView`(MemberItemParent>MemberItemPrefab 模板：MemberIsSelected 选中框/MemberName/MemberClass/MemberSession/MemberResult/MemberLevel) + `BtnsView`(BtnAddMember/BtnSetLeader/BtnQuitTeam)。注意绑定里 `BattleView` 是历史遗留、无逻辑。主面板 PvpArena 三槽位 Team 子树也已补齐（绑定名与 Ctrl `SLOT_UI` 预留完全一致，52 个绑定）。

**成员行模板** `UI/Template/PvpArenaMemberItemTemplate.lua`（用户已写，勿改）：数据字段 `MemberName/MemberClass/MemberSession/MemberResult/MemberLevel` + `isOnline`(bool，金色/灰蓝色字) + `IsSelected`(bool，选中框) + 点击走 `MethodCallback(data)`。模板根节点带 Unity Button（AddClick 可用）、`MLuaUIGroup`（Parameter 绑定来源）。

**本次改动**：

- `UI/Ctrl/PvpArenaTeamCtrl.lua`（在用户骨架上补全，保留其模板池/页签监听/输入对话框）：
  - `OnActive` 从 `self.uiPanelData.slot` 取槽位（`UIMgr:ActiveUI(CtrlNames.PvpArenaTeam, { slot = slot })` 打开）；无队提示并自关；补拉 QUERY/ROSTER；默认 TogWeek。
  - `UpdatePvpValuePanel(tab)`：标题队名+赛制；表头 比赛/胜-负/级别(个人等级分)/战队等级（周=Week 值，赛季=Season 值）；联动刷成员列表+按钮。
  - `UpdateTeamList(info)`：roster→模板数据；队长名字加"（队长）"后缀；成员的场次/胜-负随本周/赛季切换。
  - 成员行点选 `_selectedGuid`（再点取消）；`_findMember(guid)` 兜底。
  - 按钮：`BtnAddMember`（队长可见）输入框→`WArenaMgr:Invite`；`BtnSetLeader`（队长可见，需选中）确认框→`WArenaMgr:SetLeader`；第三按钮 `BtnQuitTeamText` 双模式——默认"退出战队"（→`Leave`；队长有队员时前端拦截提示先移交/踢出，**队长独自离队即服务端解散**），队长选中非自己成员时变"踢出战队"（→`WArenaMgr:Remove`）。**不用 `Disband`**（官方无解散按钮，语义由 Leave 覆盖）。
  - `BindEvents` 四个 ON_ARENA_* 事件刷新；`ON_ARENA_EVENT` 时本队不存在（GetLocalTeamId<=0）自动关窗。
- `UI/Ctrl/PvpArenaCtrl.lua` `OnSlotClick`：有队 → `UIMgr:ActiveUI(UI.CtrlNames.PvpArenaTeam, { slot = slot })`（替换原"制作中"占位）。
- **进场自关**：C# `WBattleMgr.BattleStatusChange` 的 `STATUS_IN_PROGRESS` 分支新增 `CallFunc("ModuleMgr.BattleMgr.OnEnterBattle", ArenaType)`；`BattleMgr.lua` 新增 `OnEnterBattle` → 派发 `ON_BATTLE_ENTER`（原声明未用，复用）；`PvpArenaCtrl` 监听后关闭主面板并连带关闭 PvpArenaTeam。ArenaQueue 面板原有 ON_BATTLE_JOIN/ON_BATTLE_PANEL 自关不变。
- **已知缺口**：无（踢出已接：队长选中非自己成员后第三按钮变"踢出战队"）。

**未实测清单**：①主面板点 2v2/3v3/5v5 槽位开详情 ②标题/表头本周↔赛季切换 ③成员列表在线配色/队长后缀/选中框 ④添加成员（非队长按钮隐藏）⑤提升队长 ⑥退出/解散后详情窗自关、主面板槽位回 Empty ⑦回归：PvpArena 主面板、工会面板。

## 军官排队流程服务端化（2026-08-02 第三轮，未实测）

**问题**：点竞技场军官（麦克斯·卢纳）gossip"我想加入战场。"弹"开发中"。根因：第二阶段按**选项文本**（含"竞技场"/"arena"）拦截，该军官文本是通用战场文案，拦截失败。

**官方流程**（服务端源码核实）：gossip 选项 12(GOSSIP_OPTION_BATTLEFIELD) → 服务端 `PlayerGossip.cpp:383` 按 NPC entry 查 `GetBattleMasterBG` → 直接回 `SMSG_BATTLEFIELD_LIST`（竞技场军官 bgTypeId=6=BATTLEGROUND_AA，阿拉希=3）。与买战队登记表同思路：**客户端不猜 NPC，转发服务端、按回包分流**。

**改动**：

- C# `WNPCMgr.OnSelectGossip` GOSSIP_OPTION_BATTLEFIELD：删除文本拦截/开发中提示，改为 `NpcGossipMis` 转发服务端。`TryShowArenaBattlemasterMenu(文本)` 改为 **`ShowArenaBattlemasterMenu(ulong guid)`**（public，去文本判断）。
- C# 新增 `WBattleGroundListResponse`（WBattleGroundResponse.cs 顶部，SMSG_BATTLEFIELD_LIST=573）：u64 guid + u8 fromWhere + u32 bgTypeId + 奖励字段（实例列表不解析）。`WNetClient` 注册 handler → `WBattleMgr.OnBattleGroundList`：**bgTypeId==6 → 假菜单 1628**（练习赛 3 项恒定、评级赛按本地槽位生成，点选后 JoinArenaQueue 逻辑不变）；**否则 → `CallFunc("ModuleMgr.ArenaMgr.ShowPvpPanel", 2)` 跳 PvpArena 战场页**（阿拉希军官不再做新界面）。
- Lua：`ArenaMgr.ShowPvpPanel(tab)` 加可选页签参数（已打开→`ctrl:SelectMainTab(tab)`；未打开→`ActiveUI(name, {tab=tab})`）；`PvpArenaCtrl.OnActive` 读 `uiPanelData.tab` 选初始页签；新增 `SelectMainTab(tab)`。主界面 PVP 按钮（无参调用）行为不变。
- C# `WNPCMgr.JoinArenaQueue` 评级赛**前置校验**（对应服务端静默失败项，服务端源码见 `BattleGroundHandler.cpp:668`/`Group.cpp:1940`）：①非小队队长拦截（原有）②小队人数>赛制人数拦截（`Group.cpp:1951`）③队员不全在同一支该赛制战队拦截（`Group.cpp:1986`，roster 未缓存则先 `QueryRoster` 并提示稍后再试）。**无法前端校验的剩余静默项**：赛季未开启（`Arena.Season.InProgress`，`BattleGroundHandler.cpp:815`）——全部校验通过仍排不了时查这个配置。
- C# `WBattleMgr.OnJoinResponse` **修"加入战场失败（战场索引：6）"误报**：`SMSG_GROUP_JOINED_BATTLEGROUND` 的 int32 **正值=BattlemasterList 索引=排队成功**（`SharedDefines.h:3890` 注释；成功时服务端给每个队员发 bgTypeId=6，`BattleGroundHandler.cpp:890`），旧代码 default 分支把正值当失败。现正值提示"你的队伍已加入竞技场/战场队列"，负值走原错误码表。

**未实测清单**：①点麦克斯·卢纳→"我想加入战场。"→假菜单 1628（练习赛 3 项/有队槽位出现评级赛）→练习赛单排→ArenaQueue 计时→确认进场→PvpArena 自关 ②阿拉希军官→跳 PvpArena 战场页 ③回归：工会注册员、竞技场组织者（Bip Nigstrom 买表/交表）、AB 战场页排入。

## 被邀请弹窗（2026-08-02 第四轮，未实测）

工会邀请（WoW 栈）没有独立面板，就是 `GuildMgr.ShowGuildInviteConfirm` 的 `CommonUI.Dialog` YES_NO 全局弹窗（RO 栈的 `GuildInviteOffer` 面板是 protobuf 体系，勿复用）。竞技场同款实现：

- `ModuleMgr/ArenaMgr.lua` `OnInit`：`EventDispatcher:Add(ON_ARENA_INVITE, fn, nil)`（**API 是 `Add(eventKey, fn, self)`，不是 AddListener**）→ 新增 `ShowArenaInviteConfirm(invite)`：`CommonUI.Dialog` YES_NO，文案"X 邀请你加入竞技场战队 Y"，【接受】`WArenaMgr:AcceptInvite()` /【拒绝】`WArenaMgr:DeclineInvite()`，两分支均清 `g_pendingInvite`，带 `SetOverrideSortLayer(Top+1)`（与工会一致）。

**未实测**：A 队队长详情窗添加成员输入 B → B 弹确认 → 接受后进队（双方 roster 刷新）/拒绝后 A 无反馈（COMMAND_RESULT 提示未做，属 P1）。

## 错误提示统一 C# 化（2026-08-02 第五轮，未实测）

**约定**：所有错误提示在 C# 里用 `WNetClient.singleton.ShowTipsAndSystem(msg)`（= 弹 Tips + 系统聊天栏黄字），不走 Lua 事件。

- `WArenaMgr.OnArenaCommandResult`：新增 `ShowCommandResultTips`——按 `ArenaTeam.h ArenaTeamCommandErrors` 全量映射 19 个错误码中文提示（找不到玩家/已在队/已邀请/满员/无权限/非队长离队/等级/拒绝等；0x08 同值两义按 command==QUIT 区分）；邀请成功（command==1, err==0）正向提示"已邀请 X 加入战队"。Lua 事件推送保留（数据用）。
- `WArenaMgr.OnArenaError`：SMSG_ARENA_ERROR → "你不在该赛制的竞技场战队中"（本服务端仅此一种用法，`ArenaTeamHandler.cpp:418`）。
- `WNPCMgr.JoinArenaQueue` 4 处前置校验提示、`WBattleMgr.OnJoinResponse` 成功/失败提示：全部从各自私有 ShowTips 换成 `ShowTipsAndSystem`（其余工会/决斗等既有流程的私有 ShowTips 不动）。

**未实测**：详情窗添加成员：输错名字→"找不到玩家"；重复邀请→"已被邀请"；非队长离队→权限提示；拒绝邀请→邀请方收到"拒绝了你的邀请"。

## 结算/对阵数据链路（2026-08-02 第六轮，未实测）

**协议修正（炸弹拆除）**：`WPVPLogDataResponse` 竞技场分支原解析与服务端布局完全不符，已按服务端源码重写（`Battleground.cpp:1394` + `Arena.cpp:32/49/60`）：

```
u8 type(=1)
×2 队(i=0 部落/绿队, i=1 联盟/金队): u32 ratingLost, u32 ratingWon, u32 matchmakerRating
×2 队: cstr 队名
u8 ended; if ended: u8 winner(0=部落/绿队 1=联盟/金队)
u32 playerCount
×N 玩家: u64 guid, u32 killingBlows, u8 pvpTeamId, u32 damage, u32 healing, u32 objCount(=0)
```

`ArenaTeamScore` 类字段改为 RatingLost/RatingWon/MMR/TeamName + 派生 `RatingChange`；`PlayerScore` 加 `PvPTeamId`。战场(Type==0)分支原样保留。**服务端比赛结束自动给每人推该包（`Battleground.cpp:971`），客户端无需请求**；战斗中客户端可 `AskForLogData()` 主动拉（Ended=0）。

**C# `WBattleMgr`**：

- `OnBattleLogData` 开头按 Type 分岔，Type==1 → `OnArenaLogData`（AB 原逻辑逐行不动）。
- 新增嵌套 payload `WArenaLogData{ ended, winner(-1=未结束), arenaType, isRated, teams=[{index,name,ratingChange,mmr,isWinner}], players=[{guid,name,classId,teamIndex,kb,damage,healing,isSelf}] }` → `CallFunc("ModuleMgr.ArenaMgr.OnArenaLogData", json)`。
- 玩家名未缓存：**不跳过该玩家**（name 置空），触发 `QueryPlayerName` + 1.5s 后 `AskForLogData()` 补拉（最多 2 次，`MTimerMgr`）。
- `STATUS_IN_PROGRESS` 分支：竞技场且**非周期重发**（新增 `prevStatusId` 守卫）→ `AskForLogData()` + `CallFunc("ModuleMgr.ArenaMgr.OnEnterArena", ArenaType, IsRated)`。

**Lua `ArenaMgr.lua`**：新事件 `ON_ARENA_LOG_DATA`；`g_arenaMatch`（OnEnterArena 写入，旧日志清空）/ `g_arenaLog` 缓存；`OnArenaLogData(json)` 解码→缓存→派发；访问器 `GetArenaMatch()/GetArenaLog()`；`OnReconnected` 一并清空。

**结算面板（2026-08-02 第七轮，未实测）**：

- `PvpArenaResult.prefab`（用户搭建，结构勿动）：Title/BtnClose/ResultPanel(LabResult/LabMatchType)/TeamA·TeamB（各 Head[Tag/Name/Rating/MMR]+Columns 静态表头[名字/职业/击杀/伤害/治疗]+Scroll）/BtnLeave；成员行模板 `MemberItemPrefab`（挂 TeamA Scroll 下，TeamB 池共用此模板）。~~MemberHeal 绑定名含零宽字符~~ 已修正（2026-08-02，模板 lua 同步去 ZWNJ）。⚠️ 根 ComRefs 重建后仍缺 `MemberItemPrefab` 一项（成员模板根节点上的组件没收进去），需补进根 MLuaUIPanel 的 ComRefs，否则两个成员池拿不到模板。
- `UI/Panel/PvpArenaResultPanel.lua` 绑定清单已手补（21 个）。
- `UI/Template/PvpArenaResultMemberItemTemplate.lua` `OnSetData`：name/className/kb/damage/healing/isSelf，自己整行金色。
- `UI/Ctrl/PvpArenaResultCtrl.lua`（用户骨架上补全）：两个成员模板池（TeamA/B 各一，共用模板）；`BtnClose`=仅关窗（留场内等自动离场）；`BtnLeave`=关窗+`WBattleMgr:BattleConfirm(false)` 立即出场；`Refresh()` 从 `ArenaMgr.GetArenaLog()` 渲染（仅 ended==true）：标题赛制、胜/负/平（金/红/灰）、积分赛/练习赛、TeamA 恒为我方（按 isSelf 的 teamIndex）、队名空回退"绿队/金队"、练习赛隐藏 Rating/MMR、Rating ±N 着色；绑 `ON_ARENA_LOG_DATA` 重刷（缺名补拉会二次到达）、绑 `BattleMgr.ON_BATTLE_PANEL` 离场自关。
- `ArenaMgr.lua`：`OnArenaLogData` 在 `ended==true` 时自动 `ShowArenaResult()`；新增 `ShowArenaResult/CloseArenaResult`。

**未实测清单**：①练习赛打完自动收到 ended=1 数据（看日志确认 OnArenaLogData 到达且字段正确，重点验证 ratingChange/队名/teamIndex）②准备阶段进场收到 ended=0 对阵名单（机器人陪练时名字可能为空→1.5s 补拉）③回归：AB 记分板（BattleCtrl ShowMember）。

## 比赛内 HUD + 记分板（2026-08-02 第八轮，未实测）

**设计**：进场后自动弹出小条 HUD `PvpArenaMatch`（非独占，`ActiveType.Normal`，仿 BattleCtrl）；记分板复用 `PvpArenaResult`（改双模式）。

**改动**：

- `UIConst.lua` 加 `PvpArenaMatch`。
- `PvpArenaResultCtrl` **双模式**：`Refresh()` 不再只渲染 ended==true——未结束时为对阵/记分板模式（LabResult="对阵信息"、Tag 隐藏、Rating/MMR 隐藏、成员 KDA 实时刷新）；`_fillTeam` 的 tagText 传 nil 即隐藏 Tag。
- 新面板 `PvpArenaMatchPanel.lua`（绑定 LabInfo/LabTeams/BtnBoard/BtnLeave）+ `PvpArenaMatchCtrl.lua`：
  - OnActive 立刻 `AskForLogData()`；`Update()` 每 3s 拉一次（`Time.realtimeSinceStartup` 节流；已出结算则停拉）
  - `BtnBoard` → `ArenaMgr.ShowArenaResult()`（对阵模式打开记分板）；`BtnLeave` → YES_NO 确认（提示判负）→ `WBattleMgr:BattleConfirm(false)` 离场
  - `ON_ARENA_LOG_DATA` ended==true → 自关（结算面板接力弹出）；`ON_BATTLE_PANEL` 离场自关
- `ArenaMgr.lua`：`OnEnterArena` 自动 `ShowArenaMatch()`；新增 `ShowArenaMatch/CloseArenaMatch`。

**PvpArenaMatch.prefab（用户已建）**：根 ComRefs 7 项 = ResultPanel/LabTeams/LabInfo/BtnsView/BtnLeave/BtnClose/BtnBoard，Panel.lua 已由生成脚本同步；`BtnClose`=仅隐藏 HUD（结算仍会弹）、`BtnBoard`=开记分板、`BtnLeave`=确认后离场。

**离场事件修正（整体核查发现）**：`ON_BATTLE_PANEL` 在 STATUS_NONE **和** STATUS_IN_PROGRESS 时都会派发，HUD/结算面板绑它会在进场瞬间被误关。已新增专用事件：C# STATUS_NONE 分支加 `CallFunc("ModuleMgr.BattleMgr.OnLeaveBattle")`（服务端 `RemovePlayerAtLeave` 会给离场者发 STATUS_NONE，`Battleground.cpp:1095`）；`BattleMgr.lua` 新增 `ON_BATTLE_LEAVE` 常量+`OnLeaveBattle()` 派发；HUD 与结算面板的离场自关都改绑 `ON_BATTLE_LEAVE`。`ON_BATTLE_PANEL` 仅保留给排队状态类 UI（PvpArena 战场页/ArenaQueue）。

**全链路时序**（已复查）：排队(WAIT_QUEUE→ArenaQueue)→WAIT_JOIN 全局确认→STATUS_IN_PROGRESS 首次到达（prevStatusId 守卫防周期重发）→关 PvpArena + 开 HUD + 拉对阵数据→比赛中 HUD 每 3s 拉取→结束服务端自动推 ended=1→HUD 关、结算面板弹→点离开/超时自动移除→STATUS_NONE→两面板自关。中途离场：HUD BtnLeave→确认→`BattleConfirm(false)`(StatusID==3→WBattleLeaveRequest)→STATUS_NONE→自关。

**未实测清单**：①排进竞技场→HUD 自动弹出、赛制/队名正确 ②点"对阵"出记分板、3s 刷新的 KDA 变化 ③结束→HUD 关、结算面板弹、积分赛 rating ±N ④点"离开"确认后传送出图 ⑤中途登出重连→HUD 自动恢复（PlayerAddedToBGCheckIfBGIsRunning 路径）⑥回归：结算面板结算模式。⑦进场瞬间 HUD 不被误关（ON_BATTLE_LEAVE 修正后）。

## 观察他人竞技场（查看PVP，2026-08-02 第九轮，未实测）

**入口**：`PlayerMenuLCtrl` "查看PVP"按钮 → `UIMgr:ActiveUI(CtrlNames.PvpArenaInspect, { guid, name })`。**服务端限制**（`ArenaTeamHandler.cpp:29-51`）：目标须在线、且不可攻击（敌对目标无法观察）；~~距离 ≤ INSPECT_DISTANCE~~ **已按需求注释放开（[CUSTOM] 标记，`ArenaTeamHandler.cpp:43`，2026-08-02，需重编译 worldserver 生效）**。不满足则服务端静默不回包，面板 4s 无数据显示"需目标在线且非敌对目标"。

**链路**：C# 新增 `WInspectArenaTeamsRequest`（MSG_INSPECT_ARENA_TEAMS，u64 原始 guid）+ `WArenaMgr.InspectArenaTeams(guid)`（wrap/桩已同步）→ 服务端按有队槽位各回一包（最多 3 个）→ `OnInspectArenaTeams` 缓存 `g_inspectTeams[guid][slot]` + 队名未缓存自动 `QueryArenaTeam`（顺带带回 stats）→ 事件 `ON_INSPECT_ARENA_TEAMS`；访问器 `GetInspectTeams(guid)`。

**新面板 `PvpArenaInspect`**（Panel/Ctrl 已写，prefab 用户搭建）：Title（"X 的竞技场"）+ BtnClose + 三行（Row1=2v2/Row2=3v3/Row3=5v5），行内 `RowNBracket`（赛制）/`RowNTeam`（队名）/`RowNRating`（战队等级）/`RowNSeason`（赛季 胜-负）/`RowNPersonal`（个人等级）；无队槽位"无战队"+其余"-"；未到数据"查询中…"。绑 `ON_INSPECT_ARENA_TEAMS`（按 guid 过滤）+`ON_ARENA_TEAM_QUERY`（队名回来刷新）。

**prefab 规格**：窗口约 420×300；绑定清单 = `Title, BtnClose, Row1~Row3（容器）, Row1Bracket/Row1Team/Row1Rating/Row1Season/Row1Personal, Row2…, Row3…`（17 个 MLuaUICom + 根面板 ComRefs 全收）。

**荣誉区补充（同日，对照官方观察 PvP 页）**：官方观察页上半部分还有目标的荣誉统计，走 **`MSG_INSPECT_HONOR_STATS`（0x2D6 双向包，`MiscHandler.cpp:1019`）**：请求 u64 guid；回包 u64 guid + u8 荣誉点数（**协议截断为 1 字节**，显示值可能不准）+ u32 kills（低 16 位本日/高 16 位昨天击杀）+ u32 本日荣誉 + u32 昨天荣誉 + u32 生涯击杀。**协议没有目标的竞技场点数字段**（显示"-"）。

- C# `PVPQueryResponse` 解析修正（kills 拆两个 u16，去掉 65537 硬编码）；`WBattleMgr.QueryInspectHonorStats(guid)` + wrap/桩；`OnPvpInfoBack` 推 `ModuleMgr.ArenaMgr.OnInspectHonorStats`(json)。
- 服务端 `MiscHandler.cpp:1031` 观察距离同步放开（[CUSTOM]，需重编译）。
- Lua：`ArenaMgr` 新增 `ON_INSPECT_HONOR_STATS` 事件 + `g_inspectHonor` 缓存 + `GetInspectHonor(guid)`；观察面板荣誉区（HonorPoint/HonorKillToday·Yesterday·Sum/HonorValueToday·Yesterday/ArenaPoint="-"），OnActive 同时发两队观察请求。
- **prefab 需补荣誉区绑定**（仿 PvpArena 主面板荣誉区）：`HonorPoint, HonorKillToday, HonorKillYesterday, HonorKillSum, HonorValueToday, HonorValueYesterday, ArenaPoint`。
- **prefab 最终形态（用户已定稿，41 绑定）**：直接复用 PvpArena 主面板命名体系——荣誉区 + 三槽位 `Arena{2V2,3V3,5V5}{Team/Empty/Name/Level/BsValue/SfValue/DjValue/FlagIcon/FlagBox/FlagBg}` + `Title/PvpView/BtnClose`。Panel/Ctrl 已按实际命名重写：槽位 Team/Empty 显隐；`Name`=队名、`Level`=TeamRating、`Bs`=赛季场次、`Sf`=胜-负、`Dj`=个人等级。⚠️ 槽位行内静态文本"本周"是 prefab 烤死的，观察页显示的是**本赛季**数据，需要在 Unity 里把该文本改成"本赛季"。

**未实测**：点 nearby 玩家头像 → 查看PVP → 三行数据显示/无队显示无战队/超距离提示。

## 竞技场/战场装备购买（2026-08-04，未实测）

**方案**：改 `SMSG_LIST_INVENTORY` 包布局——扩展消耗明细由服务端随列表直接下发（**放弃官方客户端兼容**，官方客户端是拿 ExtendedCost ID 查本地 ItemExtendedCost.dbc）。服务端 DB 数据（4 个 custom SQL：TBC S1~S4 补货 + 荣誉/竞技场点数价格）已确认导入，服务端购买校验（`Player.cpp:10759` 荣誉/点数/牌子/个人等级）原本就完整，**服务端 C++ 仅发包一处改动**。

**包布局变更**（每件物品在原 8 字段后追加 14 个 uint32，恒定追加不省零）：

```
...原字段..., u32 ExtendedCost,
u32 reqHonor, u32 reqArena, u32 reqArenaSlot, u32 reqPersonalRating,
u32 reqItemId[5], u32 reqItemCount[5]
```

**服务端改动**：

- `Handlers/ItemHandler.cpp` `SendListInventory`（:1652，[CUSTOM] 标记）：`sItemExtendedCostStore.LookupEntry(ExtendedCost)` 后逐字段写入；ExtendedCost=0 全写 0。新增 `#include "DBCStores.h"`。**需重编译 worldserver 生效**。
- 附注：ExtendedCost>0 时 `Price` 恒为 0（`IsGoldRequired` =false），客户端可按 `ExtendedCost>0` 切显示模式。

**客户端 C# 改动**：

- `NpcHandlerResponse.cs`：`VendorItem` 加 `ReqHonor/ReqArena/ReqArenaSlot/ReqPersonalRating/ReqItemIds[5]/ReqItemCounts[5]` + `GetReqItemId/GetReqItemCount(int)` + 常量 `MAX_EXT_COST_ITEMS=5`；`VendorInventoryListResponse.LoadData` 同步解析。新增 `BuyItemSuccess` 类（SMSG_BUY_ITEM：u64 guid + u32 slot + i32 newCount + u32 count，服务端 `Player.cpp:10642` 购买成功时发送）。
- `WNetClient.cs`：注册 `SMSG_BUY_ITEM` → `OnBuyItemSuccess` → `WNPCMgr.OnBuyItemSuccess`（既有 `SMSG_BUY_FAILED` 失败提示不动）。
- `WNPCMgr.cs`：`OnBuyItemSuccess`（刷新缓存库存 + `CallFunc("ModuleMgr.ShopMgr.OnBuyItemSuccess", slot, newCount, count)`）；新增 **`public string GetVendorCostInfo()`**——当前商人列表中 ExtendedCost>0 的消耗明细 JSON：`[{slot,extCost,honor,arena,arenaSlot,rating,marks=[{id,count}]}]`。
- `MoonClient_WNPCMgrWrap.cs`：手写注册 `GetVendorCostInfo`（仿 `GetLfgInfo` 的 string 返回模式）；`UnityLuaAPI/MoonClient_WNPCMgr.lua` 补桩。⚠️ **`MoonClient_VendorItemWrap.cs`（实体 wrap）未动**——新字段不经 wrap 暴露，Lua 一律走 `GetVendorCostInfo()` JSON（用户定：取多个数据 = Wrap 接口传字符串 + Lua 解析，不用 CallFunc 推、不序列化包对象）。

**Lua 改动**：

- `ShopCtrl:GetBuyItemData()`：每行商品合并 `ExtendedCost` + 按 slot 从 `ShopMgr.GetVendorCostInfoMap()` 并入 `ReqHonor/ReqArena/ReqArenaSlot/ReqPersonalRating/Marks`。
- `ShopMgr.lua` 新 region「WoW 商人扩展消耗」：`GetVendorCostInfoMap()`（拉取+decode+按 slot 建 map）、`GetMaxArenaRatingForPurchase(minSlot)`（镜像服务端：跨槽位 min(个人,战队) 取最大，战队未缓存退化为个人等级）、`CanAffordVendorCost(d)`（前端预检：金币 `WContainerMgr.CoinCount`/荣誉/点数/牌子 `WContainerMgr:GetItemCountByItemId`/个人等级，返回 bool+失败文本）、`OnBuyItemSuccess` + 事件 `BuyVendorItemSuccess`。
- 失败提示链路早已存在：服务端校验失败发 `SMSG_INVENTORY_CHANGE_FAILURE`（EQUIP_ERR 69 荣誉不足/70 点数不足/68 缺牌子/63 等级不足），`WErrorCode.cs` 有中文文案。

**待做（UI 阶段，用户接手）**：`BuyItemTemplate`/`CommonItemTipsCtrl` 的扩展消耗渲染（现价区仍按金币显示 0）、不足标红、购买按钮禁用接 `CanAffordVendorCost`、货币区显示与 `BuyVendorItemSuccess` 刷新。物品 tooltip 的"需要个人竞技场等级"仍走 `ItemBakeQuery.GetArenaRatingReq`（WBake 表，aowow 源，不含自定义 10000+ ID），与本包数据无关联，如需准确显示另议。

**未实测清单**：①点竞技场商人（加基森 Vixton/52区 Kezzik 等）→ 列表打开、`GetVendorCostInfo()` JSON 字段正确（重点：牌子 marks、S4 的 rating）②荣誉/点数不足时购买被服务端拦截且提示正确 ③等级不足（如 S4 武器 2050）拦截提示 ④购买成功扣点、得物品、库存/货币刷新 ⑤回归：普通金币商人（Price 正常、JSON 为空表）、出售、修理。

## 注意事项 / 坑

- 客户端有 RO 原栈（`M` 前缀 + protobuf）和 WoW 栈（`W` 前缀）两套，已有 "Arena/Pvp" 命名模块全是 RO 玩法，**不要复用也不要改 RO 栈**。
- C#→Lua 走 `LuaEngine.CallFunc("ModuleMgr.ArenaMgr.OnXxx", json)`；Lua Mgr 须在 `MgrMgr.lua` 注册否则 CallFunc 找不到模块。
- 未做任何构建/编译（按约定不主动编译）。改动未实测，首次跑起来后优先验证：进游戏有战队 → 看日志确认 ROSTER/STATS/QUERY 三个包到达 `WArenaMgr`。
- 邀请流程：收到 `SMSG_ARENA_TEAM_INVITE` → 弹确认 → Accept/Decline 发空包（0x351/0x352）。
- 离队限制：队长有队员时不能 leave（服务端报错），队长独自 leave=解散。
- **UIToggleEx 坑**：给处于 inactive 节点下的 toggle 设 `isOn` 会报 "Toggle is not part of ToggleGroup"（OnDisable 时已从组注销，OnEnable 才注册）。必须等其父节点激活后再设值（如 PvpArenaCtrl 的 TogAB 改在切到战场页签后设置）。

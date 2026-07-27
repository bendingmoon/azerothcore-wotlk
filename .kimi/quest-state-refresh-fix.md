# 任务状态刷新修复（接任务后对话选项不刷新 / NPC 头顶标识不对）（2026-07-26）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。

## 症状

1. 接受任务后 TalkDlg2 对话界面的任务选项不刷新（已接任务仍显示可接）。
2. NPC 头顶感叹号/问号有时候不对（接了任务还在、或该出现时不出现）。

## 根因

- **C# 不重拉列表**：`WNPCMgr.OnAcceptQuest` 发完接受包只发头顶状态查询，没像 `OnTrainerBuySucceeded` 那样重拉 gossip/任务列表，`_currentGossip.QuestItems` 永远是旧快照。
- **Lua 刷新通道被挡**：gossip 回包 → `OnSetGossipMenuResponse` → `ENUM_UI_ON_TASK_CLOSE_TARGET` → `NpcMgr.GotoNpc`，但 `IsTalking()` 时直接 return，更新被丢弃。
- **选项数据层不清理**：`NpcTalkDlgMgr.AddSelectInfo` 替换同名旧选项时只删 UI 层，`SelectInfos` 数据层残留累积。
- **头顶标识缓存不同步**：`WNpcFxComponent.refreshNpcStatus` 的 `questGiverStatus` 只在成功设置特效路径的分支赋值，清空特效的两个早退分支不更新 → 状态 8→0→8 来回切换时 `LateUpdate` 变化检测失效，感叹号不再显示。

## 改动文件

| 文件 | 改动 |
|------|------|
| `WInfo/WNPCMgr.cs` | 新增 `RefreshGossip()`（重发 hello+questgiver-hello 拉新列表，同连接按序处理必得新状态）；`OnAcceptQuest` 接受成功后调用 |
| `WNetwork/ApplicationLayer/WNetClient.cs` | `OnQuestCompleteResponse` 交任务后也调 `RefreshGossip()` |
| `Lua/ModuleMgr/NpcMgr.lua` | 抽出 `BuildWowSelectInfos(talkDlgMgr)`；新增 `RefreshTalkSelectInfos()`；`GotoNpc` 在 `IsTalking()` 时改为原地清空并重建选项 |
| `Lua/ModuleMgr/NpcTalkDlgMgr.lua` | `AddSelectInfo` 替换旧选项时从 `l_dataMgr.SelectInfos` 真正 `table.remove` |
| `WComponents/Staffs/WNpcFxComponent.cs` | `refreshNpcStatus` 两个清空分支同步 `questGiverStatus` 缓存 |

## 链路要点（以后改任务刷新用）

- TalkDlg2 选项数据流：服务端 SMSG_GOSSIP_MESSAGE / SMSG_QUESTGIVER_QUEST_LIST → `WNPCMgr._currentGossip` 快照 → Lua `WNPCMgr:GetMenuItems()` 一次性拉取 → `NpcTalkDlgMgr.AddSelectInfo` → TalkDlg2Ctrl 事件渲染。**没有主动推送，必须重发 hello 才有新快照**。
- `QuestGiverQuestListResponse`（WNetClient.cs:1240）会把 `_currentGossip` 整体替换为"只有任务、GossipItems 为空"的结构，重拉时必须像 `OnSelectNPC` 一样两个 hello 都发。
- 接受成功无专用回包，任务日志变化靠 SMSG_UPDATE_OBJECT 的 `PLAYER_QUEST_LOG_*` 字段 → `WEntity.UpdateAttrQuest` → `WQuestMgr.AddOrUpdate`，末尾群发 `WQuestgiverStatusMultipleRequest` 批量刷头顶。
- 头顶标识状态写入点三处：`QuestGiverStautsResponse`（单查）、`QuestGiverStatusMultiple`（批量，未加载实体直接丢弃）、NPC 进视野 `WEntity.Initialize` 单查兜底。

## 遗留疑点（未动）

- `WNetClient.QuestGiverStautsResponse` status==8 且 NPC 无 QUESTGIVER 旗标时整个状态更新被丢弃，旧状态残留（原意不明，怕误清图标，未改）。
- `QuestGiverStatusMultiple` 对未加载 NPC 的状态不做落地缓存，靠 spawn 单查兜底，极限时序下可能丢状态。
- `TaskInfoCtrl.UpdateTaskButton` 每次刷新都 `AddClick` 新回调，若 MLuaUICom 不去重会叠加触发 `OnAcceptQuest`（未验证）。

# NPC 头顶感叹号缺失修复（该有「！」的 NPC 看不到）（2026-08-14）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。

## 症状

某些 NPC 明明有可接任务，头顶却不显示感叹号（不是刷新慢，是根本不出现；问号不受影响）。

## 根因（服务端+客户端联合排查结论）

1. **QUESTGIVER 旗标三重门**：NPC 模板缺 `UNIT_NPC_FLAG_QUESTGIVER`（SmartAI/脚本/gossip 发任务、或模板漏配）时三连死——
   - 服务端批量包跳过无旗标 NPC（`src/server/game/Entities/Player/Player.cpp:7756`）；
   - 客户端 spawn 兜底单查只对 create 包带旗标的 NPC 发（`WEntity.cs:486-490`），旗标为 0 的单位建成 `WCreature`，不挂 `WNpcFxComponent`（`WEntityMgr.cs:853-868`）；
   - 客户端单查回包 status==8 且无旗标 → 整个更新静默丢弃（`WNetClient.QuestGiverStautsResponse`，上次修复遗留疑点，本次已修）。
2. **状态值没映射特效**：`refreshNpcStatus` 原来只认 2/5/8/10；服务端还会发 6（可重复可接）、7（日常可接）、9（REWARD2 可交），这些头顶什么都不显示（本次已修）。
3. **切图/登录不发批量刷新**：`SetMapInfo` 在 `switchingMap` 期间 early return 跳过批量请求（`WMapMgr.Core.cs:428`），`LoadingEnd`/`OnArriveNewWorld` 也都不发；服务端进图推送（`Player.cpp:11682`）到达时实体未 spawn 被丢弃 → 只剩 spawn 单查兜底（本次已修）。

## 改动文件

| 文件 | 改动 |
|------|------|
| `WNetwork/ApplicationLayer/WNetClient.cs` | `QuestGiverStautsResponse` 删掉 status==8 的 QUESTGIVER 旗标检查，状态直接落地，与批量回包 `QuestGiverStatusMultiple` 对齐 |
| `WComponents/Staffs/WNpcFxComponent.cs` | `refreshNpcStatus` 补映射：6(REWARD_REP)/7(AVAILABLE_REP) → 感叹号，9(REWARD2) → 问号 |
| `WMaps/WMapMgr.Core.cs` | `LoadingEnd` 的 DelayCall 里补发 `WQuestgiverStatusMultipleRequest`（登录/切图加载完成后批量刷一次） |

## 服务端枚举对照（QuestDef.h:110-126，线上值）

0 NONE / 1 UNAVAILABLE(灰!) / 2 LOW_LEVEL_AVAILABLE / 3 LOW_LEVEL_REWARD_REP / 4 LOW_LEVEL_AVAILABLE_REP / 5 INCOMPLETE(灰?) / 6 REWARD_REP / 7 AVAILABLE_REP / 8 AVAILABLE(黄!) / 9 REWARD2(?) / 10 REWARD(?)。客户端枚举在 `GameObjects/Player/Enums/QuestGiverStatus.cs`。

## 链路要点

- 服务端主动推 SMSG_QUESTGIVER_STATUS_MULTIPLE 只有 5 处：进图 AddToMap、升级 GiveLevel、交任务 RewardQuest、摧毁物品、响应批量查询。**接任务/放弃任务/完成目标都不推**，靠客户端在任务日志变化时自发批量查询。
- 服务端单查不校验旗标（只查敌对）；批查强制要求旗标——批量过滤是官方行为，不要动服务端，修客户端。
- 6 在 qr 循环里也用于「可重复非日常任务可接」（NPC 侧主要语义），所以映射感叹号；若个别场景显示成「！」但实际是可交，再评估。

## 遗留（未动）

- 批量回包对未加载实体仍零缓存（`WNetClient.cs:2027-2028`），LoadingEnd 补发后窗口已大幅缩小，极限时序仍可能丢，兜底靠 spawn 单查。
- 无 QUESTGIVER 旗标的 NPC 仍不会发 spawn 单查，只能靠交互（gossip）或下次批量刷新带出状态。
- 3(LOW_LEVEL_REWARD_REP)/4(LOW_LEVEL_AVAILABLE_REP) 低级可重复变体仍未映射特效（罕见，未加）。
- 数据层治本：可 SQL 查「有 creature_questrelation 但无旗标」的 NPC 补旗标。

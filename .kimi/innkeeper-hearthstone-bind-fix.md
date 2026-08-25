# 旅店绑定炉石修复（沙塔斯占星者/奥尔多旅店无法绑定）（2026-08-22）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。

## 症状

沙塔斯城占星者旅店（Minalei，entry 19046）和奥尔多旅店（Innkeeper Haelthol，entry 19232）无法绑定炉石；奥尔多店点"绑定"还会错误打开商店。其他普通旅店正常。

## 根因（三层叠加）

1. **服务端脚本化菜单**：这两个 NPC 的 `creature_template.ScriptName='npc_innkeeper'`（全服共 58 个），hello 被 `npc_innkeeper.cpp:36-61` 接管，用硬编码菜单 9733 组装选项，**选项 Id 是包内顺序号（0 起）而非 DB OptionID**，SMSG_GOSSIP_MESSAGE 的 **MenuId=0**（脚本路径从不 `SetMenuId`）。普通旅店走 DB 路径，选项 Id=DB OptionID、MenuId=真实菜单。
2. **客户端类型识别错位**：`WNPCMgr.OnSelectGossip` 靠本地表 `GossipMenuOption` 按 `(MenuId, OptionID)` 查类型；脚本菜单的顺序号查不到行，旧代码**回退菜单 0 猜类型**——菜单 0 通用行 `(0,1)=VENDOR`、`(0,0)=QUESTGIVER`，导致 Haelthol 绑定选项误判成商人（开商店），Minalei 误判成其他类型转发服务端。
3. **确认流程断裂**：转发服务端后脚本走 `GOSSIP_ACTION_INN → SetBindPoint` → 只发 **SMSG_BINDER_CONFIRM**(747) 等客户端回 `CMSG_BINDER_ACTIVATE` 才真正绑定（官方流程）。客户端此前**没有注册 SMSG_BINDER_CONFIRM 的 handler**，流程断死。

"其他旅店正常"是巧合：无脚本 + DB OptionID 恰好与本地表对齐（INN 恰好在 OptionID 0/1 的菜单才能识别）。同机制下 INN 在 OptionID≥2 或菜单无配行的旅店（萨尔玛 16602、埃索达 16739 等）都会失败；万圣节脚本在索引 0 插入 trick-or-treat 项时索引整体偏移也会错位。

## 改动文件

| 文件 | 改动 |
|------|------|
| `GameObjects/Player/Models/Response/BinderConfirm.cs`（新增） | SMSG_BINDER_CONFIRM 响应包类，读 8 字节 guid |
| `WNetwork/ApplicationLayer/WNetClient.cs` | 注册 `SMSG_BINDER_CONFIRM → OnBinderConfirm`（收到直接回 `CMSG_BINDER_ACTIVATE`，与本地表 INNKEEPER 捷径一致跳过确认框）；`OnTrainerBuySucceeded` 识别 spell 3286（绑定成功回执）弹"旅店绑定成功"并不再走训练师逻辑；`BinderActive` 删除无条件假成功提示 |
| `WInfo/WNPCMgr.cs` | `OnSelectGossip` 删除"查不到回退菜单 0 猜类型"的 fallback，查不到按 `GOSSIP_OPTION_NONE` 转发服务端分发 |

## 链路要点（以后改旅店/gossip 用）

- 官方绑定流程两段式：gossip 选项 → `SetBindPoint` 只发 SMSG_BINDER_CONFIRM → 客户端回 CMSG_BINDER_ACTIVATE → `HandleBinderActivateOpcode`（校验 UNIT_NPC_FLAG_INNKEEPER=0x10000、距离、声望）→ `SendBindPoint`：NPC 对玩家施法 3286（EffectBind 里 `SetHomebind` + 发 SMSG_BINDPOINTUPDATE）+ 复用 **SMSG_TRAINER_BUY_SUCCEEDED(guid, 3286)** 当成功回执 + SMSG_GOSSIP_COMPLETE。
- SMSG_BINDPOINTUPDATE 登录时也会发（`Player::SendInitialPacketsBeforeAddToMap`），**不能**用它当"绑定成功"提示信号；3286 的 SMSG_TRAINER_BUY_SUCCEEDED 才是绑定专属回执。
- 服务端 `HandleGossipSelectOptionOpcode`（MiscHandler.cpp:89）只校验 gossipListId（包内索引）和 sender guid，**不校验 menuId**——脚本菜单转发时 menuId 填客户端猜的值也能到脚本。
- 本地表 `GossipMenuOption` 是 flatbuffer 静态配表（`Assets/artres/Resources/Wow/TableData/GossipMenuOption.bytes`，int 字段 XOR -482275667），与服务端 `gossip_menu_option` 对应。

## 遗留疑点（未动）

- `OnBinderConfirm` 直接自动确认，没做官方"确定要把这里设为家吗？"确认框（与本地表捷径行为一致；要加的话在此弹框，确认后再发 WBinderActive）。
- `BindPointUpdate` handler 仍是空函数（绑定坐标没落地缓存）。
- 万圣节期间 trick-or-treat 选项（脚本菜单索引 0）转发服务端可正常处理，但未实测。

# 副本CD显示问题调查（团队信息/副本信息面板）

> 2026-08-18。前端"副本信息"面板（RaidCtrl.lua）两个问题：重复行（已修复）+ 面板无CD但进本没怪（已定位根因，修复方案待定）。

## 数据链路

- 打开面板 → `RaidCtrl:OnActive`（`Assets/Scripts/Lua/UI/Ctrl/RaidCtrl.lua:211`）→ `WNetClient:RaidInfoRequest` 发 CMSG_REQUEST_RAID_INFO(717)
- 服务端 `HandleRequestRaidInfoOpcode`（`src/server/game/Handlers/GroupHandler.cpp:1081`）→ `Player::SendRaidInfo`（`PlayerStorage.cpp:6617`）→ SMSG_RAID_INSTANCE_INFO(716)
- 客户端 `WRaidInstanceInfoResponse.LoadData`（`WPartyCommandResultResponse.cs:39`）→ `WTeamMgr.SetRaidInfo`（`WTeamMgr.cs:886`）→ Lua `TeamMgr.UpdateRaidInfo` → `RaidCtrl:SetRaidInfo` 模板池重建列表
- **服务端只发 perm 绑定**（`PlayerStorage.cpp:6633`）；登录不推送，绑定变化不推送（仅响应请求 + 全局重置后群发 `InstanceSaveMgr.cpp:494`）

## 问题1：每开一次面板多一条重复行 —— 已修复

- 根因：`WPacketsHandler.cs:74` 响应包按 opcode 单例复用，`Load()` 不复位；`WRaidInstanceInfoResponse.LoadData` 里 `RaidInstances.Add` 前不清空 → 每次收包叠加
- 修复：`WPartyCommandResultResponse.cs` 的 `LoadData()` 循环前加 `RaidInstances.Clear()`（已改，2026-08-18）
- 同类隐患：`WLfgPlayerInfoPacket.RandomDungeons`/`LockedDungeons`（同文件 143-181 行）同样"字段初始化 List + Add 不清"，待修

## 问题2：面板无CD但进本没怪 —— 根因与场景

核心矛盾：**面板只反映 perm 绑定，但 temp（非perm）绑定也持久化、也参与进本路由**。

- temp 绑定进门即创建（`Map.cpp:2028`），写入 `character_instance.permanent=0`（`InstanceSaveMgr.cpp:676`），启动全表加载（`:405`），跨重登/重启存活
- 路由 `PlayerGetDestinationInstanceId`（`InstanceSaveMgr.cpp:802`）：①自己perm → ②**队长绑定（不看perm！）全队跟随** → ③有队无队长绑定=新副本 → ④无队时自己temp也路由
- perm 只发给 boss 死时**在图内**的人（`PermBindAllPlayers`，`Map.cpp:2194`）

场景（按可能性）：
1. **队长持 temp 绑定**：队长早先进过该英雄本（temp），boss死时他不在场（早退/掉线）→无perm→面板空；temp 持久留存，且自建队队长的 temp 无任何清除时机（GROUP_JOIN 清理只对非队长 `Group.cpp:472`，英雄本不能手动重置）→ 全队按规则②进旧清空本。60秒后 save `CanReset=false`（杀手perm还在）→ 全员被自动perm绑到空本（`Map.cpp:2044` → `PlayerUpdates.cpp:345`），隐形CD变真CD
2. **CopyBinds 传播**（fork特有）：换队长时旧队长绑定以 temp 复制给新队长（`Group.cpp:726` → `InstanceSaveMgr.cpp:819`）
3. **停机跨每日重置点**：英雄 save DB 中 resettime=0（`GetResetTimeForDB`），启动清理删不到（`InstanceSaveMgr.cpp:268`），过期 save+绑定活到下次调度重置
4. **bot 参与的清空**：在场 bot 被 perm 后随机化清绑定，但任一 temp 绑定存活则 save 不删；`CanReset=false` 一旦设置永不复位
5. **重登路由**：下线路径在副本内/门口，重登按 temp 绑定落回清空本

客户端加剧因素：未实现 `SMSG_INSTANCE_LOCK_WARNING_QUERY`(327) 确认框（玩家不知情即被绑）、未注册 `SMSG_INSTANCE_SAVE_CREATED`(715) 刷新、"延长副本锁定"协议（CMSG_SET_SAVED_INSTANCE_EXTEND，服务端已实现 `CalendarHandler.cpp:786`）客户端未接线。

## 候选修复方向（待用户选择）

- A. 路由收紧：规则②/④ 对英雄/团队本只认 perm 绑定（注意别破坏"队友还在本里时重进"——在场者绑定仍路由）
- B. 清理时机：boss 死 PermBindAllPlayers 时清掉不在图内玩家的 temp 绑定（最贴近官方语义）；和/或登录时清 temp
- C. 启动补清：LoadResetTimes 清理停机期间过期的英雄 save
- D. 客户端补协议：327 确认框 + 715 后刷新面板

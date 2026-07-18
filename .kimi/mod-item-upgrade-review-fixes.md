# mod-item-upgrade 全链路审查与修复（2026-07-18）

> 关联：详细历史记忆在 Claude memory 目录 `mod-item-upgrade-*.md`（分析、Tier 方案、Phase 1-3、概率、突破修复、绑定、session 修复）。本文记录 2026-07-18 的四路审查（服务端模块 / 服务端协议 / C# 客户端 / Lua UI）及修复结果。

## 修复状态总览

| 层 | 状态 | 改动文件 |
|----|------|---------|
| 服务端主仓库 | ✅ 已修，未编译 | `src/server/game/Handlers/ItemHandler.cpp`（+26/-18） |
| 模块仓库（独立 git） | ✅ 已修，未编译 | `src/item_upgrade.cpp`（+281/-168）、`item_upgrade.h`、`item_upgrade_worldscript.cpp` |
| C# 客户端（非 git） | ✅ 已修 | `WContainerMgr.Equip.cs`、`WContainerMgr.Item.cs`、`MoonClient_WContainerMgrWrap.cs`、Lua 桩 `MoonClient_WContainerMgr.lua` |
| Lua UI | ⏸ 用户自行处理 | 未动 |

## 服务端高危 7 项（全部已修）

1. **移动端购买不重算属性** — 3 个 `Purchase*Upgrade` 成功路径补 `_ApplyItemMods(false/true)`（仅 `IsEquipped` 时）+ `RefreshWeaponSpeed` + `VisualFeedback`。
2. **可买装备没有的属性/绕过黑白名单** — `PurchaseStatUpgrade`（`item_upgrade.cpp:4496+`）新增 `GetStatByType(LoadItemStatInfo(item), statType)` 和 `CanApplyUpgradeForItem` 校验，先于扣钱，失败返回 `UPGRADE_ERR_VALIDATION`。原因：移动端的 `statType` 来自客户端字节流，不可信。
3. **摧毁背包物品泄漏装备/附魔光环** — `HandleItemRemove`（`:2163-2187`）的 unapply/reapply 仅 `IsEquipped` 时执行。
4. **物品模板查询错误路径发错 opcode** — `ItemHandler.cpp:711` 改 `SMSG_MOBILE_ITEM_QUERY_SINGLE_RESPONSE`。
5. **`IsValidWeaponForSpeedUpgrade` 越界读**（`:998-1028`）— 最终方案：**不要**加 `IsEquipped` 限制（背包武器可查看/购买是设计意图，与 `IsValidWeaponForUpgrade` 一致）；改为已装备用 `GetWeaponDamageRange`，未装备回退 `GetItemProtoDamage(proto).second > 0`。
6. **消耗块协议错位** — 查询响应三处（`ItemHandler.cpp:807-829/855-870/896-911`）改为 `!isMaxed` 时**始终**写消耗块，无 rank/无消耗写 `(0,0,0)+0.0f`。见下方"关键设计结论"。
7. **突破竞态 + 每 opcode 5~10 次同步 SQL** — 新增 Tier 内存缓存 `_characterItemTiers`（`playerGuidCounter → itemGuidCounter → tier`）+ `LoadCharacterItemTierData()`；`GetCurrentTierNum` 纯内存读（默认 1）；`PerformBreakthrough` 同步更新缓存再异步写库；`HandleItemRemove`/`HandleCharacterRemove`/完全 purge 三处清理。

## 服务端中危及清理（已修）

- 武器 purge 退款：从 `_weaponDmgRanks`/`_weaponSpdRanks` 按 rank 重建（原来读死的 `weaponUpgradeReqs` 空容器，退 0）；gossip 消耗页同理改显示真实消耗。
- 完全 purge 后 `ResetItemTierIfFullyPurged` 重置 tier（修 gossip 软锁/移动端跳级）；purge token 改为**先验证退款后发放**（原来可被刷）。
- `GetMaxTierNum` 改为按 `GetNextTier` 同款 per-tier 规则逐级走，两者一致；`GetCurrentTier` 删除跨物品兜底。
- 加载校验：`LoadTiers` 畸形 `breakthrough_costs` token 跳过+报错不再崩服；`LoadWeaponDmgRanks/SpdRanks` 校验 req_type 1-5，攻速 pct 钳到 [0,95]（防攻速 0）。
- 概率 Roll：`urand(0,9999)/100.0f` + `roll < successChance`（精确百分比）；`<=0` 短路失败、`>=100` 短路成功。
- `FindUpgradeForItem` 改无分配单遍扫描（热路径：每次属性重算都经过）。
- `SendItemPacket` 尾部补 `canUpgrade` 字节，与核心移动端 handler 对齐。
- 死代码删除：`weaponUpgradeReqs`/`weaponSpeedUpgradeReqs` 成员、`BuildWeaponUpgradeReqs/BuildWeaponSpeedUpgradeReqs`、`LoadWeaponUpgradePercents`、`FindNearestWeaponUpgradeStat`；`HandleCharacterRemove` 的 `operator[]` 误插入改为 erase。

## ItemHandler.cpp 其余修复

- `buildNextInfo` 校验下一 rank 真实存在（`FindUpgradeStat/FindWeaponDmgRank/FindWeaponSpdRank`），不存在则 `nextRank=0, isMaxed=1`，消除幽灵 rank。
- 突破 handler 错误码魔法数字 1/2/4/5 → `ItemUpgrade::UpgradeResult` 枚举（协议值不变）。

## C# 客户端修复

- 购买/突破响应 guid 不匹配时：不再丢弃，改为更新 `WItemUpgradeMgr` 缓存中对应条目（`TryGetCachedUpgrade` 已存在）；错误提示无条件显示。
- 查询失败（null 回调）与突破后重查失败：各加中文错误提示。
- 删除死代码 `_pendingUpgradeSummaries`/`ConsumeUpgradeSummary`（C# + tolua wrap + Lua EmmyLua 桩三处）。
- **证实**：`_itemUpgrade` 与 `WItemUpgradeMgr` 缓存是**同一对象引用**，购买后就地更新即刷新缓存，tips 摘要的 `RequestUpgradeSummary` 立刻拿到新数据。

## 关键设计结论（后续开发别再踩）

1. **0x530 消耗块读写条件 = `!isMaxed`**：服务端始终写（无则补零），C# 按 `!IsMaxed` 读。改协议时两边必须保持这个唯一判别条件。
2. **背包武器攻速升级合法**：查看和购买都允许；`RefreshWeaponSpeed` 只遍历装备槽，背包武器的攻速 rank 休眠，装备上时自动生效。
3. **Tier 缓存维护点**共 4 处：登录加载、突破更新、删装备/删角色清理、完全 purge 重置。新增写 tier 的代码必须同步维护 `_characterItemTiers`。
4. **校验先于扣钱**：所有 Purchase 路径先完全校验，再 `TakeRequirements`，再 Roll 概率。
5. 客户端 `ItemGuid` 全程 u64/string，Lua 侧需 `ToInt64`，不要 `tonumber`。

## 有意不改（遗留已知项）

- `reqVal1/reqVal2` 为 float 的精度问题（铜币 >2^24 不精确，改动面大）。
- 移动端 opcode 均为 `PROCESS_INPLACE`（fork 全局模式，未单独改）。
- Gossip NPC 路径不 Roll `success_chance`（概率仅移动端，设计如此）。
- PagedData 手动 new/delete、gossip sender 硬编码偏移、`item_upgrade.cpp` 文件过大（老分析中的已知遗留）。
- 属性/武器行消耗只发第一个 requirement（协议限制；多消耗仅突破支持）。
- Lua UI 待用户处理：消耗按 CostType 格式化、突破消耗货币行、成功率显示、`OnActive` 取消注释、tips 摘要 `◆升级` 块替换逻辑。

## 验证状态

所有改动**未经编译**（按约定未跑构建）。上线前需：C++ 重编译（主仓库 + 模块）+ C# DLL 重生成 + Lua 更新，三件套缺一不可。

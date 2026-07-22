# 突破奖励词条（Breakthrough Enchant）功能记录

> 2026-07-21 实现。装备升级（mod-item-upgrade）突破到下一 Tier 时，奖励一条词条附魔
> （SpellItemEnchantment，与洗练词条同一体系）。同槽覆盖，只保留最新；突破 100% 成功不考虑失败回滚。

## 配置

- 表 `mod_item_upgrade_tiers` 加列 `breakthrough_enchant_id`（int unsigned, 默认 0=无），
  按 `(item_entry, tier)` 配置到具体装备的具体突破等级；`item_entry=0` 全局默认。
- SQL：`modules/mod-item-upgrade/data/sql/db-characters/updates/u_MIU_2026_07_21_00.sql`（ALTER），
  base 文件 `b_mod_item_upgrade_tiers.sql` 已同步加列。

## 服务端改动

| 文件 | 改动 |
|------|------|
| `item_upgrade.h` | `ItemTier` 加 `breakthroughEnchantId` 字段 |
| `item_upgrade.cpp` `LoadTiers()` | SELECT 加列读取；加载时用 `sSpellItemEnchantmentStore` 校验，非法 ID 置 0 并 LOG_ERROR |
| `item_upgrade.cpp` `PerformBreakthrough()` | 写 tier 后若配置了词条：装备中先 unapply → `SetEnchantment(PROP_ENCHANTMENT_SLOT_1, id, 0, 0)` → reapply → `SetState(ITEM_CHANGED)` |
| `ItemHandler.cpp` 查询响应 | 突破消耗段后追加 u32 `breakthroughEnchantId`（下一 Tier 预告）+ u32 `CurrentEnchantId`（当前已拥有, 读 `GetEnchantmentId(PROP_ENCHANTMENT_SLOT_1)`） |
| `ItemHandler.cpp` 突破响应 | 所有分支末尾追加 u32 `grantedEnchantId`（成功时=新 tier 配置，否则 0） |

**槽位：`PROP_ENCHANTMENT_SLOT_1`（=8）**。占用排查：PERM(0)=附魔专业、TEMP(1)=洗练非武器、
SOCK(2-4)=宝石/洗练武器、BONUS(5)=插槽奖励、PRISMATIC(6)=腰带扣/洗练 dummy、PROP_0(7)=随机属性。
词条是永久附魔，随 `item_instance.enchantments` 持久化，重登自动恢复；再次突破同槽覆盖。

## 客户端改动（C#）

| 文件 | 改动 |
|------|------|
| `ItemHandlerResponse.cs` | `MobileItemUpgradeQueryResponse` 加 `BreakthroughEnchantId` + `CurrentEnchantId`；`MobileItemBreakthroughResponse` 加 `GrantedEnchantId`；缓存类 `ItemUpgrade` 加 4 个字段：`BreakthroughEnchantId`/`CurrentEnchantId` + 已解析名称 `BreakthroughEnchantName`/`CurrentEnchantName`（构造时经 `WAttrItem.GetEnchantNameById` 解析，无词条为 "无"，Lua 直接用） |
| `WAttrItem.cs` | 新增 `GetBreakthroughEnchant()`（读 `ITEM_FIELD_ENCHANTMENT_9_1` = slot 8）和静态 `GetEnchantNameById(int)`（查 `Aowow_ItemEnchantment.name_loc4`） |
| `WContainerMgr.Item.cs` `BuildUpgradeSummaryText` | tips 升级摘要块（"◆升级[等级 x/y]"）末尾追加绿色"词条：xxx"行（`CurrentEnchantId>0` 时）——词条跟在升级块后面，不在物品等级行 |
| `WContainerMgr.Equip.cs` | 突破成功且 `GrantedEnchantId>0` 时 CallFunc `ModuleMgr.EquipMgr.OnBreakthroughEnchantGranted(itemGuid, enchantId)` |
| `MoonClient_WAttrItemWrap.cs` + Lua 桩 `MoonClient_WAttrItem.lua` | 注册/声明两个新方法（`GetEnchantNameById` 是静态，Lua 用点号调用） |

## Lua 侧待办（用户自行处理）

- 实现 `ModuleMgr.EquipMgr.OnBreakthroughEnchantGranted(itemGuid, enchantId)`（获得词条提示）。
- 升级面板可用缓存的 `ItemUpgrade.BreakthroughEnchantId` + `WAttrItem.GetEnchantNameById` 做"突破后可获得 XXX"预告。
- 物品 tips 可用 `attrItem:GetBreakthroughEnchant()` 展示当前词条。

## 待验证

- 服务端/客户端编译（用户自行生成验证）。
- 未跑过实际突破流程；注意确认 slot 8 与随机附魔多属性物品无冲突（3.3.5 实际只用 PROP_0）。

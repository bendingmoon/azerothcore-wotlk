# 装备洗练（随机属性 / Item Boost）全链路记录

> 记录日期：2026-07-21。注意与「装备升级」（mod-item-upgrade）区分——洗练是另一个功能，
> 服务端在 `modules/StatBooster/`，客户端主文件是 `WContainerMgr.Enchant.cs`。

## 功能概述

玩家对装备进行「洗练」，随机（或指定）获得一条额外附魔属性。支持金币洗 / 道具洗、
随机 / 锁定预览 / 手动选择四种玩法，洗练次数持久化在物品附魔字段里。

## 文件位置

| 端 | 文件 | 作用 |
|----|------|------|
| 服务端 | `src/server/game/Handlers/ItemHandler.cpp:1245` `HandleBoostItem` | opcode 入口 |
| 服务端 | `modules/StatBooster/src/StatBoostMgr.cpp:553` `BoostItemByMoney` | 核心逻辑 |
| 服务端 | `modules/StatBooster/src/StatBoostCfgMgr.cpp` + `data/sql/db-world/` | `statbooster_enchant_template` 配置加载 |
| 客户端 | `Assets/HotUpdate/MoonClient/WInfo/WContainerMgr.Enchant.cs` | 主逻辑（发包 + 给 Lua 的数据接口） |
| 客户端 | `WNetwork/Models/Request/WItemHandlerRequest.cs:49` `WItemBoostRequest` | 请求包 |
| 客户端 | `GameObjects/Player/Models/Response/ItemHandlerResponse.cs:139` | 回包 |
| 客户端 | `WComponents/Attribute/WAttrItem.cs:747-812` | 附魔字段读取（描述/预览/次数/可选标志） |
| 客户端 | `WNetwork/ApplicationLayer/WNetClient.cs:168,1025` | 回包注册与分发 |

## 协议

- `CMSG_BOOST_ITEM`：bag(u8), slot(u8), fromPre(u8), isLock(u8), selectedID(u32), useItem(u8)
- `SMSG_BOOST_ITEM = 1312`：result(u8) + itemGuid
- 客户端回包处理：`OnItemBoostResponse` → `WContainerMgr.EnchantResponse()` → 延迟 60ms 调 Lua
  `ModuleMgr.SelectEquipMgr.OnItemBoostResponse` 刷新 UI

### 服务端返回码

| 码 | 含义 |
|----|------|
| 0 | 道具不存在 |
| 1 | 不是装备 |
| 2 | 品质不符 / 该附魔不适用于此装备 |
| 3 | 几率不足（随机模式 roll 失败，当前 chance 写死 100） |
| 4 | 金币不足 |
| 5 | 成功 |
| 6 | 预览失败：未找到存储的预览属性 |
| 7 | 选择失败：洗练次数未达要求 / 已选择过 |
| 8 | 道具不足 |

## 四种模式（BoostItemByMoney）

1. **预览应用 `fromPre==1`**：读 `ITEM_FIELD_ENCHANTMENT_12_1+1` 存的预览附魔 ID，
   `ApplyBoostEnchant` 直接应用，**不扣钱、不计数**。无预览返回 6。
2. **随机模式 `selectedID==0`**：`AnalyzeItem` 判属性类型（失败则按子类兜底）→
   `EnchantPool.GetByQa(statType, classMask, subClassMask, typeMask, itemLevel, quality)` 抽附魔。
3. **选择模式 `selectedID!=0`**：`GetById` 校验附魔存在；要求洗练次数 ≥ `MinChooseCount`；
   `ENCHANTMENT_11_1+1 == 1` 时拒绝（返回 7，表示已选过）。
4. **锁定模式 `isLock==1`**：只把抽到的附魔 ID 写入 `ENCHANTMENT_12_1+1` 作预览，
   不真正附魔；计数 +1；费用双倍。之后用 `fromPre==1` 确认应用。

## 消耗

- 道具洗 `useItem==true`：扣道具 **57005**，锁定模式 ×2；不足返回 8。
- 金币洗：`GetBoostMoney(item)`，锁定 ×2；客户端 `GetItemBoostCost` 写死显示 **500000**（50 金）。
- 附魔写入：非武器 → `TEMP_ENCHANTMENT_SLOT`；武器 → 第一个空闲宝石槽
  （`SOCK_ENCHANTMENT_SLOT/2/3`）+ `PRISMATIC_ENCHANTMENT_SLOT` 写 `ENCHANT_DUMMY` 占位。
- 成功后 `MakeSoulbound` 强制绑定。

## 物品字段布局（关键！）

| 字段 | 含义 |
|------|------|
| `ITEM_FIELD_ENCHANTMENT_3_1` | 武器当前洗练附魔 ID（客户端读描述用） |
| `ITEM_FIELD_ENCHANTMENT_12_1+1` | 预览（锁定）附魔 ID；选择模式（金币洗）后置 1 表示「已选择过」 |
| `ITEM_FIELD_ENCHANTMENT_12_1+2` | 金币洗洗练次数 |
| `ITEM_FIELD_ENCHANTMENT_11_1+1` | 「已选择过」标志（道具洗）；达到里程碑时重置 0 |
| `ITEM_FIELD_ENCHANTMENT_11_1+2` | 道具洗洗练次数 |

洗练计数里程碑：10 / 50 / 100 / 200 / 300 / 500 / 1000 —— 到达时把 `ENCHANTMENT_11_1+1`
重置为 0，即解锁一次手动选择。

## 装等修正（两端一致）

- 橙装（Quality == 5）：itemLevel + 20
- 双手武器（INVTYPE_2HWEAPON）：+ 20
- 道具洗（useItem）：+ 20（仅客户端 `GetItemEchantCan` 过滤列表时）

## 配置表 `statbooster_enchant_template`

服务端 EnchantPool 与客户端 ProtoTable 同名表。字段：`EnchantID`、`iLvlMin/iLvlMax`、
`ClassMask`、`SubClassMask`、`ItemTypeMask`（掩码 0 = 不限）、`MinChooseCount`（>0 时需次数
达到才可选）、`Description`。客户端 `GetItemEchantCan` 按这些条件过滤后 JSON 给 Lua，
同一 EnchantID 去重。

## 客户端给 Lua 的接口（WContainerMgr.Enchant.cs）

- `ItemBoost(guid, fromPre, isLock, selectedID, useItem)` — 发起洗练
- `GetItemEnchantDesc(guid)` / `GetItemEnchantPreDesc(guid)` — 当前 / 预览属性名
- `GetItemEchantCount(guid, useItem)` — 洗练次数
- `IsItemEnchantCanSelect(guid, useItem)` — 次数 ≥10 且可选标志为真
- `GetItemEchantCan(guid, isChooze, useItem)` — 可选附魔列表 JSON
- 另有分解（`EquipdSplit`，附魔技能 13262）、`GetDisenchant` 等附魔专业相关

## 已知疑点（2026-07-21 分析时发现，未修）

1. **「已选过」标志读写字段不一致**：服务端 `BoostItemByMoney` 选择模式校验时永远读
   `ENCHANTMENT_11_1+1`（道具洗标志），但金币洗成功后写的是 `ENCHANTMENT_12_1+1`
   （StatBoostMgr.cpp:668-678）；客户端 `GetEchantCanChooze()` 也只读 `11_1+1`。
2. **预览应用路径不重置已选标志**，与正常附魔路径不对称。
3. **客户端写死次数 ≥10 可选**，服务端按配置 `MinChooseCount` 判断，配置 ≠10 时两端不一致。
4. 随机模式 roll 几率写死 100（`chance = 100`，配置 `ReforgeChance` 被注释），返回码 3 实际不会触发。

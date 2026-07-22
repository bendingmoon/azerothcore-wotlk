# 装备升级（mod-item-upgrade）配置文档

> 2026-07-21 整理。配置表都在 `acore_characters` 库，模块目录 `modules/mod-item-upgrade/`。
> 可直接作为配置人员的使用文档。

## 一、整体机制

装备升级分 **3 类升级线**：普通属性（力量/敏捷等，按 stat_type 分）、武器伤害、武器攻速。
每条线按 **rank（等级）** 逐级购买，每级提升一个百分比 `stat_mod_pct`（按装备基础值加成）。
rank 被 **Tier（品阶）** 分段，当前 Tier 升满后必须 **突破（breakthrough）** 才能继续。
突破 100% 成功，消耗材料，突破后可奖励一条词条附魔。

消耗规则：**先扣钱/材料，再 roll 成功率**，失败不退。

## 二、配置表一览

### 1. `mod_item_upgrade_stats` — 属性升级阶梯（核心表）

每行定义"某属性升到某 rank 后的加成"。**每个 (stat_type, stat_rank) 组合一行**，
`id` 就是其它表引用的 `stat_id`。

| 列 | 说明 |
|---|---|
| `id` | 阶梯项 ID（stat_id），被 req/override/黑白名单表引用 |
| `stat_type` | 属性类型，见第三节 ItemModType 完整枚举 |
| `stat_mod_pct` | 该 rank 的总加成百分比（如 5 = +5%） |
| `stat_rank` | 等级，从 1 递增；与 tiers 表的 begin/end_rank 对应 |
| `success_chance` | 成功率 0-100，100=必成；失败已扣消耗不退 |

### 2. `mod_item_upgrade_stats_req` — 属性升级消耗

每行是某 `stat_id` 的一项消耗；**同一 stat_id 可有多行 = 多项消耗同时收取**。

| 列 | 说明 |
|---|---|
| `stat_id` | 关联 `mod_item_upgrade_stats.id` |
| `req_type` | 消耗类型，见第三节枚举 |
| `req_val1` | type=1/2/3 时为数量；type=4 时为道具 entry |
| `req_val2` | 仅 type=4 用：道具数量 |

### 3. `mod_item_upgrade_stats_req_override` — 按装备覆盖消耗

按 `(stat_id, item_entry)` 覆盖基础消耗。**命中后整组替换**（不是叠加），该装备该档只用
override 里的消耗行（也可多行）。查不到 override 才回退到 `stats_req`。

### 4. `mod_item_upgrade_weapon_dmg` / `mod_item_upgrade_weapon_spd` — 武器伤害/攻速阶梯

和 stats 表同类，但**消耗列直接内嵌**（req_type/req_val1/req_val2 每行一组，不支持多消耗）。
`stat_mod_pct` 伤害是放大百分比，攻速是缩短百分比。两表 rank 序列都从 1 连续递增，
加载时会校验连续性。

### 5. `mod_item_upgrade_tiers` — 品阶与突破

| 列 | 说明 |
|---|---|
| `item_entry` | 0=全局默认；非 0=特定装备专属 Tier 配置（优先于全局） |
| `tier` | 品阶号 1,2,3... |
| `name` | 品阶名（精良/史诗/传说…），突破后下发客户端 |
| `begin_rank` / `end_rank` | 该 Tier 可升的 rank 区间（三条升级线共用） |
| `breakthrough_costs` | 突破消耗字符串：`type:val1:val2\|type:val1:val2`，例 `1:50000000:0\|4:12345:3` = 5000金 + 道具12345×3。type 含义同 req_type |
| `breakthrough_enchant_id` | 突破到该 Tier 奖励的词条附魔 ID（SpellItemEnchantment），0=无；同槽覆盖只留最新 |

突破条件：当前 Tier 内**所有可升级项都达到 end_rank**，且存在下一 Tier。

### 6. 黑白名单（4 张）

| 表 | 粒度 | 行为 |
|---|---|---|
| `mod_item_upgrade_allowed_items` | 装备 entry | **空表=全部允许**；非空=只有表内装备可升级 |
| `mod_item_upgrade_blacklisted_items` | 装备 entry | 禁止升级，优先级高于白名单 |
| `mod_item_upgrade_allowed_stats_items` | (stat_id, entry) | 某 stat_id 无记录=不限；有记录=仅列出的装备可买该档 |
| `mod_item_upgrade_blacklisted_stats_items` | (stat_id, entry) | 禁止指定装备买指定档 |

## 三、重要枚举

### req_type（UpgradeStatReqType）

| 值 | 含义 | req_val1 | req_val2 |
|---|---|---|---|
| 1 | 金币（铜币单位，10000000 = 1000 金） | 铜币数 | - |
| 2 | 荣誉点 | 点数 | - |
| 3 | 竞技场点 | 点数 | - |
| 4 | 道具 | 道具 entry | 数量 |
| 5 | 无消耗 | - | - |

### stat_type 完整枚举（ItemModType / 客户端 GetStatName）

| 值 | 属性 | 值 | 属性 |
|---|---|---|---|
| 0 | 法力值 | 25 | 近战被暴击 |
| 1 | 生命值 | 26 | 远程被暴击 |
| ~~2~~ | （未使用，勿配） | 27 | 法术被暴击 |
| 3 | 敏捷 | 28 | 近战急速 |
| 4 | 力量 | 29 | 远程急速 |
| 5 | 智力 | 30 | 法术急速 |
| 6 | 精神 | 31 | 命中（通用） |
| 7 | 耐力 | 32 | 暴击（通用） |
| 12 | 防御等级 | 33 | 被命中（通用） |
| 13 | 躲闪等级 | 34 | 被暴击（通用） |
| 14 | 招架等级 | 35 | 韧性 |
| 15 | 格挡等级 | 36 | 急速（通用） |
| 16 | 近战命中 | 37 | 精准 |
| 17 | 远程命中 | 38 | 攻击强度 |
| 18 | 法术命中 | 39 | 远程攻击强度 |
| 19 | 近战暴击 | ~~40~~ | （野性攻强，3.3.5 未使用，勿配） |
| 20 | 远程暴击 | 41 | 法术治疗效果（已废弃，勿配） |
| 21 | 法术暴击 | 42 | 法术伤害效果（已废弃，勿配） |
| 22 | 近战被命中 | 43 | 法力回复 |
| 23 | 远程被命中 | 44 | 护甲穿透 |
| 24 | 法术被命中 | 45 | 法术强度 |
| 46 | 生命回复 | 47 | 法术穿透 |
| 48 | 格挡值 | | |

说明：

- **2 和 40 不要配**：服务端枚举里 2 是空位、40 标注 `not in 3.3`，客户端显示"未知"。
- **16-30 的分系等级（近战/远程/法术）和 31-37 的通用等级是不同字段**；升级时 `stat_type`
  必须和装备上实际存在的 `ItemStatType` 完全一致才会生效（服务端会校验装备必须有该属性）。
- **41/42 是旧版字段**（deprecated），3.3.5 装备基本用 45 法强，别配 41/42。
- 默认配置实际只用 8 种：3/4/5/6/7、32、36、45。
- 客户端 1000/1001（武器伤害/攻速）只是协议层伪 statType，不进数据库。

### 客户端伪 statType（仅协议/Lua，不进库）

- `1000` = 武器伤害升级线
- `1001` = 武器攻速升级线

## 四、配档注意事项

- stats 表每个 stat_type 的 rank 序列要连续，加载有校验，断档/重复会报错。
- 新增 Tier 时，三条线的阶梯表都要补到对应的 `end_rank`，否则该 Tier 内无法升满、永远突破不了。
- 某装备想单独定价：tier 用 `item_entry` 行，单档消耗用 `stats_req_override`。
- `breakthrough_enchant_id` 必须是 `SpellItemEnchantment.dbc` 里存在的 ID（洗练词条同一张表），
  配错会在启动日志报 ERROR 并按 0 处理。
- 改表后无需重启：模块有 reload 流程（`.reload` 或重载配置），但 `character_item_tier` 等
  玩家数据表不要手动改。

## 五、参考代码位置

| 内容 | 位置 |
|---|---|
| ItemModType 枚举 | `src/server/game/Entities/Item/ItemTemplate.h` |
| 客户端 stat_type 中文名 | `MoonClient/GameObjects/Player/Models/Response/ItemHandlerResponse.cs` `GetStatName()` |
| 表结构 SQL | `modules/mod-item-upgrade/data/sql/db-characters/base/` |
| 加载/校验逻辑 | `modules/mod-item-upgrade/src/item_upgrade.cpp`（`LoadTiers`/`CheckDataValidity` 等） |

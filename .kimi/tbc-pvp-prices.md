# TBC (S4 赛季) PvP 价格还原 — 工作记录

> 记录日期: 2026-07-28。本文档配合 `tbc_pvp_prices.sql` 使用，供后续修改时参考。

## 背景与目标

服务器封顶 70 级（TBC），但 WLK 3.3.5a 框架下旧 PvP 装备被改成"纯荣誉、无竞技场等级要求"。
目标：还原 TBC S4 赛季期间（2.4.3）的原版规则：

- S4 野蛮角斗士：竞技场点数 + 个人/战队等级 1550~2200
- S3 复仇角斗士：S4 开启后折扣价 + 等级 1800/1950
- 荣誉散件（守备官/护卫者/老兵/元帅督军套/坐骑/消耗品）：荣誉 + 战场牌子双材料

## 涉及文件

| 文件 | 说明 |
|---|---|
| `data/sql/custom/db_world/tbc_pvp_prices.sql` | **价格还原 SQL**（幂等，可重复导入） |
| `data/sql/custom/db_world/tbc_remove_wotlk_pvp.sql` | **移除 WLK 80 级 PvP 装备售卖**：1062 件 S5~S8 角斗士全系 + Titan-Forged（2026-07-28 追加；用户反馈加基森商人仍卖 80 级装备。只删 npc_vendor 记录，item_template 保留；game_event_npc_vendor 已确认无相关） |
| `data/sql/custom/db_world/tbc_gadgetzan_arena_vendors.sql` | **加基森 S1~S3 商人补货**（2026-07-28 追加）：Ecton(见习)→S1、Argex(老兵)→S2 荣誉+牌子（S1/S2 荣誉价格体系相同：头胸腿14500+30牌、肩11250+20AB、手10500+20AV、盾15000+20EotS、双手27000+40AV、法系主手25200/物理18000+20EotS、副手9000、投掷/魔杖/圣物8000+10EotS，cost ID 10040-10050）；Evee→S3 竞技场点数（沿用 10020-10028 桶）。每个 NPC 3 个同名人（33915-33941）全部补齐，共 1113 行。S4 由 Vixton 20278 原样售卖 |
| `data/sql/custom/db_world/tbc_other_arena_vendors.sql` | **其他竞技场商人补货**（2026-07-28 追加，生成脚本 `var/gen_area52.py`，分析 `var/arena_vendor_analysis.txt`）：52区 Leeni(见习)→S1、Kezzik(老兵)→S2、Big Zokk→S3、Grex(武器)→S4 武器，加基森 Blazzek(武器)→S4 武器（49 件，桶 10005-10010）。共 1407 行。已确认无需改动：鲜血之环 Meminnie(S3)/Frixee(S4)、试炼之环 Grikkin(S4)、沙塔斯 Drelik/Drolig(S4)。达拉然商人全部跳过（70 级不可达） |
| `data/sql/custom/db_world/tbc_close_p6_badge_vendors.sql` | **关闭 P6（2.4 太阳井阶段）公正徽章兑换**（2026-08-19）：删铁匠霍尔萨（25046，奎岛阳湾军械库 guid 93964，货挂 game_event 109）+ 安维赫（27667，沙塔斯）各 57 件 ilvl 141-146 牌子装；只删售货记录，NPC 保留。P6 开放时导入同目录 `tbc_restore_p6_badge_vendors.sql` 恢复（幂等） |
| `data/sql/custom/db_world/tbc_close_outland_honor_qm.sql` | **清空荣誉大厅"外域护甲军需官"**（2026-08-19）：拉切尔·瓦卡中尉（12778，暴风城）+ 军团士兵蒂娜（12788，奥格瑞玛），全部 67×2 件皆为后期货（守备官 141 / 护卫者 154-159 / 战斗大师 156），无一件 P1，整店清空，NPC 保留。后期开放导 `tbc_restore_outland_honor_qm.sql`。同大厅的配件军需官（12781/12793，ilvl ≤128 旧世+老兵散件）保留；WLK PvP 商（朵莉丝·维兰提斯 34060/32385、雷角中士 34038/32383、血卫士札尔希 32832 等，S5~S8 ilvl 200-264，两阵营大厅都有）由 `tbc_remove_wotlk_pvp.sql` 覆盖，**当前库未导入该文件故仍在卖** |
| `data/sql/custom/db_world/tbc_close_badge_vendors.sql` | **关闭全部剩余公正徽章兑换**（2026-08-19，同日修订：吉尔拉斯 P1 段保留）：吉尔拉斯 G'eras（18525，沙塔斯）删 P4 段 82 件（2.3 牌子装 ilvl 128-136）+ 源生虚空(23572)/虚空漩涡(30183)（2.4 才追加的徽章材料），**保留 P1 段装备 53 件**（2.0 原版 ilvl ≤115，用户要求）；凯里 Kayri（26089，奎岛）删 45 件徽章换老兵（cost 1015/2347），她另 45 件守备官是太阳井 T6 代币兑换（2320-2328，与瑟雷敏斯 25976 的 T6 同号）故保留；昂图沃（27666）+ 夏尼（25950，奎岛 guid 94386，event 110）各删 6 颗徽章史诗宝石（cost 1642），金币图鉴保留。恢复导 `tbc_restore_badge_vendors.sql`（全量恢复，再按需重跑关闭文件的部分语句）。牌子装阶段对照：P1=ilvl ≤115（2.0）、P4=ilvl 128-136（2.3）、P6=ilvl 141-146（2.4）。团本代币兑换（T4 20613/20616、T5 21905/21906、T6 23381/25976、太阳之尘 25977、代币换 PvP 26090/26091/26092）属团本奖励循环，**有意保留** |
| `data/sql/custom/db_world/tbc_remove_wotlk_emblem_vendors.sql` | **移除 WLK 纹章/代币兑换装备商**（2026-08-19）：达拉然各级纹章军需官与护甲/珠宝纹章商、T9/T10 职业护甲商、银色锦标赛军需官（含传家宝）、冬拥湖、风险硬币、传家宝商、北风苔原蚌壳商（25206，ilvl 138-145 可被 70 级获取），共 82 个 NPC 删 ExtendedCost>0 行（4959 行），金币货保留。恢复导 `tbc_restore_wotlk_emblem_vendors.sql`。注意：WLK **竞技场**商人（31863/32356/33915-33941/34036-34095 等）不在此文件，由 `tbc_remove_wotlk_pvp.sql` 覆盖 |
| `var/gen_tbc_pvp2.py` | 生成脚本（Python 2.7，调价格/等级后重跑即可） |
| `var/tmp_ipp.sql` | mod-individual-progression 的荣誉价格原始数据（生成脚本输入） |
| `var/tmp_rows.txt` | 从基础库 `item_template.sql` 提取的 S2/S3/S4 装备行（生成脚本输入） |
| `var/check_wlk_pvp.py` / `var/tmp_wlk_pvp_items.txt` | WLK PvP 物品扫描脚本与结果（1062 件清单） |

## 核心机制（为什么这样改有效）

- 购买校验在 `src/server/game/Entities/Player/Player.cpp` `BuyItemFromVendorSlot`（约 10759-10798 行）：
  检查荣誉点、竞技场点数、`RequiredArenaRating`（取 `min(个人等级, 战队等级)` 在各分组的最大值）。
- ExtendedCost 双源加载：`DBCStores.cpp:342` 先读 `ItemExtendedCost.dbc`，再用 world 库表
  `itemextendedcost_dbc` 覆盖/新增同 ID 条目（`DBCDatabaseLoader.cpp:76`）。**改表即可，无需改客户端 DBC**。
- 扣费在同文件 `_StoreOrEquipNewItem`（约 10619-10633 行）。

## ExtendedCost ID 分配（10000+ 段）

| ID 段 | 用途 |
|---|---|
| 10000-10010 | S4 野蛮角斗士（竞技场点数 + 等级） |
| 10020-10028 | S3 复仇角斗士（折扣价 + 等级） |
| 10125-12291 | 荣誉散件/坐骑/消耗品（荣誉 + 牌子，沿用 mod-individual-progression 原 ID） |

S4 分桶：头1875@1700 / 胸1875@1600 / 腿1875@1550 / 肩1500@2200 / 手1125 /
双手·远程3750@2050 / 法系主手3150@2050 / 物理单手2625@2050 / 盾1875@2050 /
副手1125 / 投掷·魔杖·圣物1000

S3 分桶：头胸腿1500 / 肩1200@1950 / 手900 / 双手·远程3000@1800 / 法系主手2739@1800 /
物理单手2283@1800 / 盾1630@1800 / 副手978 / 投掷·魔杖·圣物800

荣誉部分：775 件映射（465 件 UPDATE 改价 + 310 件补进现有军需官 NPC；
mod 的自定义 NPC 126393 不存在于基础库，其物品已改派到 23446/24672/26393 等现有 NPC）。
48 条 cost 定义与 mod 原文件逐条机器比对一致。

## 数据来源

- S3/S4 价格与等级：WoWWiki 档案 Season 3/4 Arena rewards + UltimoWoW 原始物品库逐件核实
  （例：35015 双手剑 3750@2050、35093 匕首 2625@2050、35068 头 1875@1700、35070 肩 1500@2200、
  33688 S3 双手剑折扣后 3000@1800）。
- 荣誉散件：mod-individual-progression 的 `optional/sql/world/zz_optional_tbc_pvp_prices.sql`
  （数据源自 cMangos TBC）。
- 用户提供的参考表有一处误差已修正：S4 双手武器是 3750 点（不是 3150），3150 是法系主手价。

## 应用步骤

```bash
mysql acore_world < data/sql/custom/db_world/tbc_pvp_prices.sql
mysql acore_world < data/sql/custom/db_world/tbc_remove_wotlk_pvp.sql
# 重启 worldserver（itemextendedcost_dbc / npc_vendor 均在启动时加载）
```

**注意**：改价和"卖什么"是两回事。`tbc_pvp_prices.sql` 只改 TBC 装备的价格，不会把 WLK 装备从商人
列表里拿掉——80 级竞技场装备要靠 `tbc_remove_wotlk_pvp.sql` 删除。删空后只卖 WLK 装备的 NPC
（如 Big Zokk Torquewrench）商店会变空，属预期；如需隐藏可再清其 npcflag 的 vendor 位（128）。
达拉然牌子 PvE 商人（31579/31580 等，卖 80 级英雄纹章装备）不在本次范围内，需要的话另行处理。

## worldserver.conf 配套

当前角色库 `active_arena_season = (8, 1)`，即 WLK S8。**赛季 ≥6 时 TBC legacy 行为不生效**，两个方案：

方案 A（推荐）：游戏内 GM 执行 `.arena season start 4` 切到 S4 赛季，然后 conf 只改：
- `Arena.AutoDistributePoints = 1`（每周自动结算发点，**默认关，必须开**）
- `Arena.LegacyArenaPoints = 1`（≤1500 分固定 344 点的 TBC 公式）
- 初始分不用配：赛季 <6 时自动 LegacyArenaStartRating=1500、个人 1500（默认值即 TBC 行为）

方案 B（留在 S8）：
- `Arena.AutoDistributePoints = 1`
- `Arena.ArenaStartRating = 1500`、`Arena.ArenaStartPersonalRating = 1500`
- `Arena.LegacyArenaPoints` 不用开（赛季 ≥6 时 ≤1500 分本来就固定 344）

相关代码位置：
- 赛季分流初始分：`src/server/game/Battlegrounds/ArenaTeam.cpp:41`（队）、`:133`（个人）
- 点数公式：`ArenaTeam.cpp:668 GetPoints`（赛季<6 且未开 Legacy 用线性公式，否则 344）
- 每周结算：`src/server/game/Battlegrounds/ArenaTeamMgr.cpp:216 DistributeArenaPoints`
- 配置加载：`src/server/game/World/WorldConfig.cpp:449-452`
- 赛季切换 GM 命令：`src/server/scripts/Commands/cs_arena.cpp`（`.arena season start/set/reward/deleteteams`）

## 已知事项 / 待办

- **客户端显示**：商人窗口价格由客户端本地 `ItemExtendedCost.dbc` 渲染。不打客户端补丁时，
  界面仍显示旧的荣誉价格，但购买会被服务端正确拦截。荣誉部分可直接用 mod-individual-progression
  仓库的 `optional/patch-V.7z` / `patch-S.7z`；S3/S4 竞技场部分需自制 DBC 补丁（待办）。
- ~~S2 残酷角斗士未处理~~ / ~~S1 未处理~~：已由 `tbc_gadgetzan_arena_vendors.sql` 解决（2026-07-28），
  S1/S2 均按原版荣誉价格体系（14500+30牌 等）在加基森售卖。
- **42xxx 同名复制品**：WotLK 追加的同名复制物品（如 42625 Merciless 胸、42632 Gladiator 头）已排除，
  不上架（生成脚本按 entry >= 40000 过滤）。
- **已修正的分类错误**：33743 Vengeful Gladiator's Salvation、35082 Brutal Gladiator's Salvation（治疗锤）
  分别从 10025(2283)/10007(2625) 改到 10024(2739)/10006(3150)，治疗锤属法系主手
  （生成脚本已加 Salvation 关键词，2026-07-28）。
- **竞技场商人全图**（`var/arena_vendor_analysis.txt`，36 个有刷新的商人）：
  - 加基森：Vixton(S4) + Ecton/Argex/Evee/Blazzek（已补） ✓ 完整
  - 52区：Leeni/Kezzik/Big Zokk/Grex（已补） ✓ 完整
  - 鲜血之环：Meminnie(S3)、Frixee(S4)；试炼之环：Grikkin(S4)；沙塔斯：Drelik/Drolig(S4) —— 基础库数据正确，未动
  - 达拉然：9+4 个商人全是 WLK 货（已随 1062 件删除而清空），70 级不可达，**故意留空**
- 40440-40444 等几件无人售卖的 S4 物品，UPDATE 命中 0 行，无害。
- T6/T5/T4 代币换 S3/S2/S1 的兑换商（奎岛 Karynna 等）未动，属 2.4 原版行为。
- 加基森 Ecton/Evee 身上残留的 1 件 "Commendation of Bravery"（WotLK 物品）未删，无害；如需纯净可删。
- 只卖 WLK 装备的 NPC（如 Big Zokk Torquewrench）清货后商店为空，属预期；如需隐藏清 npcflag vendor 位(128)。
- ~~达拉然牌子 PvE 商人（31579/31580 等，80 级英雄纹章装备）不在范围内，需要时另行处理。~~ 已由 `tbc_remove_wotlk_emblem_vendors.sql` 解决（2026-08-19），含 T9/T10/锦标赛/冬拥湖/传家宝/蚌壳商，共 4959 行。
- ⚠️ **2026-08-19 发现**：本机 `acore_world` 中没有 2026-07-28 那 4 个 SQL 的任何痕迹（自定义 ExtendedCost 10000+ 段 0 条、ilvl≥200 在售 8471 行），该库疑似重建过。用户称线上库已导入过——**若线上就是本机这个库，需重导 4 个 PvP SQL**。本次两个新 SQL（徽章关闭 + WLK 纹章商移除）已导入本机 `acore_world`，线上库若非本机需同步导入。

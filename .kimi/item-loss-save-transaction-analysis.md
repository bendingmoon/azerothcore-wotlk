# 挂机装备消失（野数据）+ 宠物保存 62 万重复键：死锁丢事务全案（2026-08-21）

> 状态：代码修复已完成，**观察期 2 天**（用户决定），观察通过后再评估是否上"保存失败回调加固"。

## 问题现象

真人玩家点挂机（AFK grind / auto-pilot）后，身上或背包里的装备"消失"。DB 里 `item_instance` 记录还在，但与角色的关联（`character_inventory` 行）没了，成野数据。不是被销毁（DestroyItem 会删两个表）。

## 实锤根因链（装备野数据）

1. 挂机 AI 高频操作背包 → 玩家保存事务又大又频繁；全服机器人也在高频写同样的表 → InnoDB 死锁（1213）。
2. 死锁重试上限 1 分钟（`Transaction.cpp:96-129` `DEADLOCK_MAX_RETRY_TIME_MS`），仍失败则打 `Fatal deadlocked SQL Transaction` 并**丢弃整个事务**；非死锁错误（如 1062 重复键）在 `ExecuteTransaction` 直接回滚丢弃（`MySQLConnection.cpp:410-415`）。
3. **组包即标已保存**：`_SaveInventory`（`PlayerStorage.cpp:7471-7473`）把语句塞进事务后立刻清状态、清 `m_itemUpdateQueue`——丢掉的改动永不补写，DB 位置信息停在旧状态。
4. **REPLACE 唯一键静默删行**：`character_inventory` 主键 `(item)` + 唯一键 `(guid,bag,slot)`（`data/sql/base/db_characters/character_inventory.sql:28-29`）。旧行占着旧格子，内存已把该格子分给别的物品；下次保存 `REPLACE INTO character_inventory` 撞唯一键 → **MySQL 静默删除旧物品关联行**，`item_instance` 保留 → 野数据。此过程零日志（日志里 `doesnt have a valid bag` = 0 条，证明不是 `_LoadInventory` 6015 登录清理产生的）。

## 实锤根因链（pet_spell 62 万重复键）

- 日志特征：62 万条 1062 全是 `INSERT INTO pet_spell`，只涉及 **5 只宠物**（72785/72941/72989/73293/73566），每只恰好卡 1 个基础技能（3009×2/35346/27049/27050，active=129=自动施法开）。5 次 `Fatal deadlocked` 也全是这 5 只的事务。
- 机制：一次保存失败（死锁/重复键回滚）丢掉 REMOVED 技能的 DELETE → 内存已删、DB 残留 → 重新学习时 `addSpell` 标 `PETSPELL_NEW`（`Pet.cpp:1760`）→ `_SaveSpells` 裸 INSERT（`CharacterDatabase.cpp:571`）撞残留行 → 1062 → 整事务回滚。**NEW 状态的技能永远没人发 DELETE** → 残留行永不被清 → 永久自锁。`_SaveSpells` 同样在组包时标 UNCHANGED（`Pet.cpp:1594`），是同一结构缺陷。

## Errors.log 证据分布（91MB / 124 万行）

| 证据 | 数量 | 说明 |
|------|------|------|
| `[ERROR]: [1062]` | 621,066 | 几乎全部 `pet_spell.PRIMARY`（+5 次 `character_spell`） |
| `[ERROR]: [1213]` | 412 | 死锁；按语句分：`INSERT character_achievement_progress` 214、`REPLACE character_inventory` 96、`DELETE character_pet` 42、`INSERT realmcharacters` 36、`DELETE pet_spell` 8、`INSERT pet_spell`/`character_queststatus_daily` 各 5、`DELETE character_inventory` 4 |
| `Fatal deadlocked` | 5 | 全是宠物事务（pet_spell INSERT） |
| `doesnt have a valid bag` / `Possible cheat` / `_SaveInventory` 位置错误 / 崩溃 | 0 | 野数据非登录清理产生，无崩溃 |

注：Errors.log 只收 ERROR 级，`retrying. Loop timer`（WARN）看不到，无法从该文件判断死锁重试成功率。

## 已完成的代码修复（2026-08-21）

**A. 挂机真人玩家未门控动作 ×4**（`botAI->IsRealPlayer()` 早退，与 2026-07-26 批次同款）：

- `CleanQuestLogAction::Execute`（`DropQuestAction.cpp:62`）—— nc 策略 random 触发 + `DropObsoleteQuests` 默认 true，会自动放弃真人全部灰色任务并销毁任务物品（含已装备的任务装备，`TakeQuestSourceItem`→`DestroyItemCount`）
- `DestroySoulShardAction::Execute`（`WarlockActions.cpp:192`）—— 销毁真人灵魂碎片
- `OpenItemAction::Execute` / `UnlockItemAction::Execute`（item push result 触发，自动开容器/锁箱）

**B. 裸 INSERT → REPLACE 幂等化 ×7**（`src/server/database/Database/Implementation/CharacterDatabase.cpp`）：

- 首批（已上线，新日志 1062 归零已验证）：`:530` `character_spell`、`:569` `pet_spell_cooldown`、`:571` `pet_spell`、`:572` `pet_aura`
- 扩展批（2026-08-21 二次，新日志实锤 `character_action` 1062 后追加）：`:514` `character_action`、`:528` `character_skills`、`:542` `character_talent`——至此玩家保存事务内无重复键引爆点
- ~~`LoginDatabase.cpp:83` `realmcharacters` 改 INSERT IGNORE~~ **已按用户要求还原**（2026-08-21）：该语句是独立执行（`AccountMgr.cpp:73`，不在事务里），1062 纯属日志噪音无功能影响，还原零风险；保留 REPLACE 会被 IGNORE 掩盖其他错误的顾虑，尊重用户决定

- 主键齐备，残留行变覆盖，5 只卡死宠物下次保存自愈，无需手工清库
- 安全不用动的表（同事务先全删再全插）：`character_glyphs`、`character_queststatus_daily/weekly/monthly`、`character_aura`

## 待办（运维侧）

- [ ] my.cnf `[mysqld]` 加 `transaction_isolation = READ-COMMITTED` → `sudo systemctl restart mysql` → 重启 worldserver（连接池是老连接）。风险=零（全库无 FOR UPDATE/显式隔离依赖；仅 STATEMENT binlog 复制需确认，MySQL8 默认 ROW 无碍）
- [ ] 确认线上 `CharacterDatabase.WorkerThreads = 1`（`worldserver.conf.dist:138` 默认 1；调大会让同一玩家保存并发互锁）
- [ ] 重新编译部署 worldserver

## 观察期验证方法

- `Errors.log` 里 `1062` 应归零；`1213` 应大幅下降。若 `1213` 仍高 → 死锁另有热点，拿新日志复查。
- `Fatal deadlocked` 每出现一次 = 丢了一个事务，需重点看。
- 野数据盘点 SQL：
  ```sql
  SELECT ii.guid, ii.itemEntry, ii.owner_guid
  FROM item_instance ii LEFT JOIN character_inventory ci ON ci.item = ii.guid
  WHERE ci.item IS NULL AND ii.owner_guid != 0 ORDER BY ii.owner_guid;
  ```
- 旁证：玩家若说"某装备变成了另一件"，正是 REPLACE 顶行机制（旧格子被新物品占用）。

## 暂缓项：保存失败回调加固（设计已定，未实施，用户 2026-08-21 决定先观察）

基础设施已具备：`DatabaseWorkerPool::AsyncCommitTransaction` 返回 `TransactionCallback`（future<bool>），`WorldSession::AddTransactionCallback`（`WorldSession.h:1196`）在世界线程触发回调；范例 `CharacterHandler.cpp:629`。

方案（约 120 行 / 6 文件，风险低、可逆）：

1. `Player::SaveToDB(create,logout)`（`PlayerStorage.cpp:7147`）提交改 `AsyncCommitTransaction` + session 回调；无 session 回落旧路径。只影响这个重载；角色创建/邮件/拍卖走自己的事务不受影响。
2. 回调失败分支：LOG_ERROR 带 GUID；`FindConnectedPlayer` 在线则调 `MarkSaveDataDirtyForRetry()`——物品仅翻 `UNCHANGED→CHANGED`（跳过 ITEM_NEW/REMOVED，m_items[PLAYER_SLOTS_COUNT] 全槽+包内物品）+ `AchievementMgr::SetAllChanged()`；**不立即重存**（靠下次正常保存，无重试风暴）。
3. `Pet::SavePetToDB` 两个提交点同样处理，失败重标 `MarkSpellsDirtyForSaveRetry()`（UNCHANGED→CHANGED，跳过 FAMILY）。
4. 残留盲区（接受）：销毁物品 DELETE 丢失→复活（复制风险）、下线瞬间回调可能不触发、邮件/拍卖事务不在覆盖内。
5. **覆盖范围补充（2026-08-21 拍卖行问题）**：`MailHandler.cpp:616`（取件）、`AuctionHouseMgr.cpp:558`（过期返还批处理）、`AuctionHouseHandler.cpp:329/403/585`（上架/竞拍）都是各自独立的 fire-and-forget 提交点，**回调加固若实施必须一并改造**，否则邮件/拍卖链仍然裸奔。

影响评估结论：回调与物品包处理同线程上下文（世界线程），无新并发面；重写全部幂等；可逆性 100%（3 个调用点换回 `CommitTransaction` 即还原）。

## 附：拍卖行"上架 20 下架变 11"排查（2026-08-21）

主流程逐环核实**均无计数漏洞**：上架单事务（`AuctionHouseHandler.cpp:324-329`）；过期/取消返还批量事务（`AuctionHouseMgr.cpp:516-558`，注意 555-556 行在提交结果已知前就清了内存 map，事务丢则拍卖物品成孤儿，重启后从 auctionhouse 表恢复重发）；竞拍/一口价只能整组买（协议无数量字段，`:426-586`）；邮件取件总数全取或取不出、单事务（`MailHandler.cpp:510-622`）；本 fork 无 AhBot 购买逻辑（LootAction.cpp 里是注释死代码）。

候选解释按序：① 取回/入包时保存事务被丢 → 内存/DB 发散 → 后续操作在中间态上落库（间歇性吻合，与本案同一根因）；② 感知误差（包里已有部分堆叠，取回合并显示 +11 增量；或 11+9 分组上架回来一组）；③ 客户端数量缓存。

取证：`entities.player.auctionhouse` INFO 日志逐笔带数量（`created auction #N ... x20` / `Auction #N expired: ... x20`），对照实际投诉——日志 x20 但到手 11 → 邮件/取件环节（保存丢失）；日志已是 x11 → 上架环节。

## 第二阶段：修复后仍有新增（2026-08-24/25，进行中）

**已确认事实**：

- 修复后新日志（F:\wow\Errors.log）：`1213=0`、`Fatal=0`、`1062` 仅剩 35 条 realmcharacters（已还原的 INSERT 噪音，无害）——**事务通道已闭合**
- 但昨天仍有 5 件道具变野数据（74480639 封印命运腰带、75553605 氪金宝箱、75656769 源生暗影、75857749 破损黑曜石棒、74131518 暗影微粒），玩家反馈与**切换双天赋**相关
- "准野数据"查询（bag 指向无效背包的行）= **0** → 排除"死锁旧伤在登录时结账"
- 移动端 3 个装备升级/突破/洗练 handler（`ItemHandler.cpp:949/1035/1226`）审计：薄封装不动物品位置，排除；客户端不主动换装备（无 CMSG_USE_EQUIPMENT_SET 发送方）；切天赋服务端只碰副手（AutoUnequipOffhandIfNeed，包满转邮件有兜底）

**当前主嫌疑**：`RemoveItem` **只摘不标**（`PlayerStorage.cpp:2996-3067`，注释自证 "does not actually change the item"）——若某路径摘了物品既不存回也不销毁，DB 旧行成无主行，之后任意 REPLACE 撞到该 `(guid,bag,slot)` 唯一键即静默顶行 → 野数据。纯内存分歧，不需要死锁。

**已部署诊断探针**（全部在 `src/server/game/Entities/Player/PlayerStorage.cpp`，确诊后搜 `[OrphanProbe]` 移除）：

| 野数据产生路径 | 覆盖 | 位置 |
|---|---|---|
| REPLACE 唯一键顶行（内存/DB 位置分歧） | ✅ Probe A | `_SaveInventory` NEW/CHANGED 分支（含同事务正常换装误报抑制 queueItemGuids） |
| 保存时位置校验分支按 (bag,slot) 删行 | ✅ Probe B | `_SaveInventory` 7446 分支（打出被删行上的物品 GUID） |
| 登录时无效 bag 清理（6015/6028/6169/6184） | ✅ 核心原有 LOG_ERROR | 需确认 `entities.player` 路由进 Errors.log |
| buyback 清理 | 不产野数据（item_instance 同删） | — |
| AH/邮件/公会银行转移 | 事务化已验证 | — |
| 商城 PHP 直写 | ⚠️ C++ 探不到 | 需运营侧自查 |

探针开销：每次有变更的保存多一次主键 SELECT（~0.05ms）+ 异常时才打日志。

**待办**：

- [ ] 编译部署探针；出 `[OrphanProbe]` 日志后拿被驱逐物品 GUID 反推"是谁摘了它没存"
- [ ] 检查线上日志配置：`entities.player` 是否路由进 Errors.log（`_LoadInventory` 的 6015/6028 清理日志目前不可见）
- [ ] 排除商城 PHP 直写 `character_inventory`/`item_instance` 的可能（若有）

## 相关文件

- `src/server/game/Entities/Player/PlayerStorage.cpp`（`_SaveInventory` 7338、`_LoadInventory` 5925、`SaveToDB` 7147）
- `src/server/game/Entities/Pet/Pet.cpp`（`SavePetToDB` 502、`_SaveSpells` 1549、`addSpell` 1760、`LoadPetFromDB` 216）
- `src/server/database/Database/MySQLConnection.cpp`（`ExecuteTransaction` 382）
- `src/server/database/Database/Transaction.cpp`（死锁重试 96）
- `modules/mod-playerbots/src/Ai/Base/Actions/{DropQuestAction,OpenItemAction,UnlockItemAction}.cpp`、`Ai/Class/Warlock/Action/WarlockActions.cpp`

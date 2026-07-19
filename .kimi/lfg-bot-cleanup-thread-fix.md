# LFG 机器人清理线程竞态修复（2026-07-18）

> 起因：线上 worldserver SIGSEGV，崩溃于 `BIH::subdivide`（BoundingIntervalHierarchy.cpp:85）。用户触发场景：随机本传出 → 退队 → 片刻后崩。本文记录根因分析与补丁。

## 崩溃签名与判据

- 栈：`Creature::UpdateMovementFlags` → `GetFloorZ` → `DynamicMapTree::getHeight` → `RegularGrid2D::intersectZAllignedRay` → `BIHWrap::intersectRay` → 懒触发 `balance()` → `BIH::build` → `subdivide` 崩溃。
- 关键证据：`dat.indices` 是同一线程几微秒前刚 `new` 并填好 `0..104` 的，根节点第一次分割却读出野值 `0x44A07201` → **新鲜堆块被外线程踩坏**；`dat` 本体在活跃栈上完好（numPrims=105）。
- 结论：BIH 是**被害现场**不是凶手；`src/common/Collision/` 全体系无锁，依赖"同图同线程"约定。dump 里的 inf/NaN 均为 G3D::Ray 轴对齐正常值和 gdb 优化伪值。

## 根因链（已修复）

1. 玩家排随机本 → `LfgGroupBotMgr` 生 4 个陪打 bot（`OnPlayerQueueForLfg`，世界线程）。
2. 玩家传出 + 退队 → bot 主人远离 → bot AI（**副本地图 worker 线程**）触发 `LeaveFarAwayAction`/`LeaveGroupAction` → `OnBotLeftGroup` → `CleanupBot`。
3. 旧 `CleanupBot` 在地图线程**同步**执行 `Group::RemoveMember` → 核心钩子 `LFGGroupScript::OnRemoveMember` → `LFGMgr::LeaveLfg`，改写 LFGMgr 全局容器；而 `LFGUpdateRequest` worker 被刻意安排**与地图更新并行**（MapMgr.cpp:260），正在遍历同一批容器 → 堆损坏。
4. 崩溃地图（571 北岛）某格 BIH 懒重建分配数组时踩到坏堆 → SIGSEGV。

## 补丁内容（modules/mod-playerbots，独立 git 仓库）

| 文件 | 改动 |
|------|------|
| `src/Script/WorldThr/PlayerbotOperations.h` | 新增 `BotLfgCleanupOperation`（含 `#include "LFGMgr.h"`）：世界线程执行退 LFG 队列、`RemoveMember`（在线+离线两分支）、清 "add" 事件，末尾复用 `BotLogoutOperation` 登出。优先级 90，`IsValid` 恒 true。 |
| `src/Bot/LfgGroupBotMgr.cpp/.h` | `CleanupBot`/`OnBotLeftGroup`/`CleanupBotsForPlayer` 只做"打标记 + 投递操作"；新增 `MarkBotForCleanup`（幂等，首个调用者胜出）；`m_spawnedBots`/`m_pendingLogins` 全部 8 处访问点加 `m_mutex`（含易漏的 `push_back`）；`CheckAndCleanup` 的 erase 阶段加锁。 |

设计要点：

- 地图线程入口只剩 `OnBotLeftGroup`，仅做标记（`state = TO_LOGOUT`）+ `QueueOperation`（本身线程安全）。
- 世界线程入口（`Update`→`CheckAndCleanup`、`OnPlayerQueueForLfg` 等，经 `RandomPlayerbotMgr::UpdateAIInternal` ← `Playerbots.cpp:382 OnUpdate`）结构不变。
- 锁只用短作用域，不跨耗时调用（`CheckAndCleanup` 主循环不持锁，靠 `MarkBotForCleanup` 内重校验兜底）。
- `activateCheckLfgQueueThread` 等 boost::thread 辅助函数已确认是无调用者的死代码，未动。

## 排查过程中排除的嫌疑（备查）

- 世界线程与地图 worker 有相位分离（`MapMgr.cpp:279` 的 `m_updater.wait()`），bot 会话泵、传送 ack、`PlayerbotWorldThreadProcessor` 均在世界线程，安全。
- `MustDelayTeleport` 只在玩家自身 `Update` 内为 true，但 LFG 传送实际走世界线程 packet handler，未构成实锤。
- 用户自定义提交（装备升级、移动端挂机 opcode）体量小且不碰线程，排除。
- mod-playerbots 官方 issue 区无 `BIH::subdivide` 同签名报告——非已知已修 bug。

## 遗留同类隐患（未修，后续跟进）

- `BattleGroundJoinAction.cpp:112`：地图线程直调 `member->GetGroup()->RemoveMember(...)`（BG 场景，影响面小）。
- LFG worker 在地图更新期间发包（`SendLfgUpdate*`）与 Asio IO 线程的 socket 竞态（AC 原版问题）。
- 堆损坏无法穷举来源：若补丁后仍崩，上 ASan 构建或采完整 core dump（`si_addr` + `thread apply all bt`）。

## 部署 / 验证 / 回滚

- modules 为 static 编译，需重新链接 worldserver；本地编译验证未跑（用户取消），上线前必须重编译。
- 验证：排随机本→传出→退队，日志应成对出现 `LFG: Bot xxx left group, scheduling cleanup`（地图线程）→ `LFG: Cleaning up bot xxx`（世界线程）。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.* src/Script/WorldThr/PlayerbotOperations.h`。
- 短期缓解（未打补丁前）：`MapUpdate.Threads = 1`、关闭 playerbots `CommandServerPort`。

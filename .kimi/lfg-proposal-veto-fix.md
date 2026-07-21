# LFG 匹配被 proposal 否决钩子卡死（2026-07-20）

> 症状：真人排随机本，4 个陪打 bot 全部进队（日志 `lfgState=2` 齐整），但永远不发 proposal、组不上；第 4 个 bot 反复"spawn → no matching dungeons → 清理"死循环。

## 根因一（主因）：真人被误判成 bot，proposal 被否决

- 核心撮合在 `src/server/game/DungeonFinding/LFGQueue.cpp:415`：建 proposal 前调
  `sScriptMgr->OnPlayerbotCheckLFGQueue(queues)`，返回 false 则放弃撮合（`LFG_INCOMPATIBLES_HAS_IGNORES`）。
- 上游实现 `modules/mod-playerbots/src/Script/Playerbots.cpp` `PlayerbotsScript::OnPlayerbotCheckLFGQueue`
  用"身上没有 PlayerbotAI"判定真人。
- 本 fork 7 月中的 auto-pilot / AFK 挂机功能会给真人常驻创建 `PlayerbotAI`
  （`PlayerbotMgr.cpp` `StartAutoPilot` → `AddPlayerbotData(master, true)`），
  于是真人玩家被钩子判为 bot → 每次撮合都被否决。6 月做 LFG 功能时真人还没挂 botAI，所以"以前是对的"。

**修复**：钩子改用 `IsRealPlayer()` 口径（与 `CheckLfgQueue` 一致）：
`!botAI || botAI->IsRealPlayer()` 即算真人。

## 根因二（次因）：替补 bot 等级随出副本区间外

- `LfgGroupBotMgr.cpp` 等级同步用 `玩家等级 ± lfgSpawnBotMaxLevelDiff(默认5)`，
  可能超出所排副本的 MaxLevel / `MinLevel+10`（LFG_TYPE_DUNGEON）限制 →
  join 过滤后列表为空 → `0366ba33` 引入的"立即清理"→ 腾名额 → 再 spawn，死循环。

**修复**：随机等级前，把 `[minLvl, maxLvl]` 按 `LfgDungeons[team]` 各副本的
MinLevel/MaxLevel（MaxLevel=0 视为无上限，DUNGEON 类型再叠 `MinLevel+10` 上限）夹取；
窗口与副本区间不相交时回退到副本区间本身（下限不低于 15）。

## 部署 / 验证

- modules 为 static 编译，需重新编译 worldserver 生效。
- 验证：真人（用过挂机、身上带 botAI）排随机本 → 应出现 proposal 并组队进本；
  日志不再出现 "no matching dungeons, cleaning up immediately" 的反复循环。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Script/Playerbots.cpp src/Bot/LfgGroupBotMgr.cpp`

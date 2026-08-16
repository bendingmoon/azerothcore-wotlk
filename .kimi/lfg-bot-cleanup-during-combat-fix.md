# LFG 副本完成判定早于战斗结束导致 bot 战斗中消失修复（2026-08-16）

## 症状

随机本地狱火城墙老三（瓦兹德+纳杉双 boss）：打死瓦兹德后 4 个陪打 bot 立刻全部消失，
纳杉还活着却没人打，队里只剩真人单挑。

## 根因链路（逐环核实）

1. 战斗脚本（boss_vazruden_the_herald.cpp）：杀两个 Hellfire Sentry → Herald 落地召唤
   地面瓦兹德（NPC_VAZRUDEN=17537）+ 天上纳杉（NPC_NAZAN=17536）；瓦兹德 35% 血喊纳杉
   下来；**设计上两个都死完** Herald 才 KillSelf 结束战斗（SummonedCreatureDies 等
   summons 清空，boss_vazruden_the_herald.cpp:108-111）。
2. 但 LFG 完成判定只绑瓦兹德：`instance_encounters.sql:214-215`
   `(396,0,17537,136,'Vazruden the Herald')` / `(397,0,17537,188,...)`——击杀 credit 绑
   17537 地面瓦兹德，lastEncounterDungeon=136/188（随机本城墙 普通/英雄）。
3. 瓦兹德一死 → `Map::UpdateEncounterState`（Map.cpp:2887-2909）→ `LFGMgr::FinishDungeon`
   → 队伍状态 `LFG_STATE_FINISHED_DUNGEON`（此刻纳杉仍在战斗）。
4. `LfgGroupBotMgr::CheckAndCleanup` 旧逻辑见 FINISHED_DUNGEON 立即 `CleanupBot` 全部
   bot → 战斗中集体消失。

对真人该判定时机无害（奖励照发，自己接着杀纳杉摸箱子——瓦兹德死即发奖励大概率就是
官方口径，未改核心数据）；对"FINISHED 即清 bot"的逻辑则是事故。**任何完成判定先于
战斗结束的副本都会踩到**，城墙老三只是典型。

## 修复内容

`modules/mod-playerbots/src/Bot/LfgGroupBotMgr.cpp` `CheckAndCleanup` 的
FINISHED_DUNGEON 分支：清理前遍历队伍成员（`group->GetFirstMember()`），**任一成员
战斗中则本轮跳过**（state 保持 IN_DUNGEON，下秒重查）：

- 瓦兹德死、纳杉还在打 → bot 全部留下继续打；
- 纳杉死/脱战/灭团 → 战斗结束 → 1 秒内按原逻辑清场，其他副本收尾行为不变。

头文件：`GroupReference` 完整定义经 `Player.h:29`（`#include "GroupReference.h"`）
传递可用；Group.h 里只有前向声明。

## 验证 / 回滚

- modules static 编译，需重编 worldserver（本地未编译验证）。
- 验证：排城墙打到老三，杀瓦兹德后 bot 应继续攻击纳杉；纳杉死后 bot 才消失。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/LfgGroupBotMgr.cpp`

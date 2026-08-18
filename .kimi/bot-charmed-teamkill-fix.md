# 猎人 bot 进队杀队友：H1 魅惑停摆修复（2026-08-17）

> 现象：随机地下城匹配到的猎人 bot 进队后立刻攻击队友（真人+bot）。
> 排查结论：阵营正常、同队的 bot 不存在任何主动攻击队友的代码路径（每层目标选择都过
> `IsFriendlyTo`/`IsValidAttackTarget`）。事发瞬间必是 reaction 异常——最可能是副本 boss
> 心控（暗影迷宫 Blackheart 全体 MC、STSM 女妖占据等）叠加本 fork 自改放大。

## 根因链（H1）

1. bot 被心控 → `Unit::SetCharmedBy` 把 faction 翻成 charmer 阵营（Unit.cpp:14698-14699），
   只 `CastStop`/`AttackStop` 当前动作，不管 AI；
2. `PlayerbotAI::DoNextAction` 原本**没有 charmed 检查**；`CanCastSpell` 只挡
   `UNIT_STATE_LOST_CONTROL`，而该状态不含 CHARMED/POSSESSED（UnitDefines.h:219）
   → 被 MC 期间 AI 照常跑，队友在其眼里是敌对 → 远程爆发+宠物打全团；其他 bot 也会
   "合法"集火这个变敌对的猎人；
3. 放大器：本 fork 自改 `Unit::IsValidAttackTarget`（Unit.cpp:10871-10874，
   commit f1da4ef01"阵营对战"）reaction 敌对即放行、不要求 PvP 标记。

## 修复内容（仅 PlayerbotAI.cpp，三处）

- `DoNextAction`（死亡/复活引擎切换之后）：`isBotAlive && bot->IsCharmed()` 时
  `SetNextCheckDelay(reactDelay)` 直接 return——魅惑期间 AI 完全停摆（不攻击、不奶、
  不下移动指令），与真人被 MC 表现一致；魅惑解除后自动恢复。
  - `IsCharmed()` = `GetCharmerGUID()` 非空（Unit.h:1310），charm 和 possess 两条路径
    都会经 `Unit::SetCharm` 写 `UNIT_FIELD_CHARMEDBY`（Unit.cpp:8004），两者都覆盖。
  - 守卫放在死亡引擎切换**之后**：被 MC 期间死亡仍能正常进 DEAD 引擎（死亡会清魅惑光环）。
- `CanCastSpell(uint32, Unit*, ...)` 与 `CanCastSpell(uint32, GameObject*, ...)` 两个重载：
  原 `UNIT_STATE_LOST_CONTROL` 检查补上 `|| bot->IsCharmed()`，堵住非引擎路径
  （聊天指令等）的施法。位置重载（x,y,z）原本连 LOST_CONTROL 都没查，未动。
- 未加日志/配置开关；行为即"真人在 MC 下不能操作"。

## 已知残余（未修，按优先级后续处理）

- H2 副本内决斗：`AcceptDuelAction`（AcceptDuelAction.cpp:11-32）无区域校验，副本里
  被丢决斗照样接，决斗中 reaction 敌对 → 互砍。只解释单目标。
- H3 宠物反击链无阵营检查：`PetAI::CanAttack`（PetAI.cpp:689-768）全程不查敌友，
  `OwnerAttackedBy`/`AttackedBy` 触发后 defensive 宠物会反杀任何伤害来源（含队友）。
- H4 LFG 登录路径漏 `SetPvP` 复位：`RandomPlayerbotMgr.cpp:2742` 的登录 SetPvP 外包着
  `IsRandomBot` 判断，LFG bot 走 `AddPlayerBot` 不在 `currentBots` 里不执行；PvP realm
  下放大 H1（`AttackersValue.cpp:195` 的"未标记玩家豁免"失效）。

## 部署 / 验证 / 回滚

- modules static 编译，需重编 worldserver；本地未编译验证（遵循 AGENTS.md 约定）。
  `python3 apps/codestyle/codestyle-cpp.py` 通过。
- 验证：
  1. 带 bot 进暗影迷宫（Blackheart the Inciter）或 STSM 女妖，等 bot 被心控
     → 被 MC 期间 bot 原地停手、不打队友、不奶怪；MC 结束后恢复正常战斗；
  2. 观察宠物：主人在 MC 期间宠物不应主动打队友（若仍打→是 H3 链，另行修）；
  3. 非 MC 场景回归：bot 正常打本输出/治疗不受影响。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/PlayerbotAI.cpp`

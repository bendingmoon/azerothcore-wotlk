# LFG 陪打 bot 按分配职责生成天赋（2026-08-11）

> 解决 `lfg-requeue-rolecheck-fix.md` 的遗留隐患：陪打 bot 的天赋按全随机专精随，
> 没按 spawn 时的 `assignedRole` 配——"奶" bot 可能随成暗影牧/元素萨，"坦" bot 可能
> 随成武器战，职责检查应答（天赋推导口径）与 assignedRole 也可能不一致。

## 机制背景（排查结论）

- `PlayerbotFactory::Randomize` → `InitTalentsTree`（非 increment 分支）按
  `AiPlayerbot.RandomClassSpecProb.<cls>.<specno>` 权重**随机** roll specno，再
  `InitTalentsByTemplate(specno)` 套 `PremadeSpecLink` 模板（模板只有 60/80 两级，
  低级 bot 按模板顺序学到点数用完为止，主系在前 → 低级也能保证主系正确）。
- 下游全部从**实际天赋**派生，强制专精后自动传导，无需额外改：
  装备权重 `StatsWeightCalculator`（AiFactory::GetPlayerSpecTab）、铭文（specNo
  缓存值）、战斗/非战斗策略（ResetStrategies 按 tab 给 tank assist/heal 等）、
  职责检查应答 `LfgJoinAction::GetRoles`（同口径反推）。
- 各职业 specno 布局（conf 默认，identity 映射）：战 0武器/1狂暴/2防护，
  骑 0神圣/1防护/2惩戒，猎 0-2 全DPS，贼 0-2 全DPS，牧 0戒律/1神圣/2暗影，
  DK 0鲜血/1冰霜/2邪恶，萨 0元素/1增强/2恢复，法/术 0-2 全DPS，
  德 0平衡/1熊/2恢复/3猫。

## 改动内容（modules/mod-playerbots）

- `src/Bot/Factory/PlayerbotFactory.h/.cpp`：
  `Randomize(bool incremental, int forcedSpecTab = -1)`、
  `InitTalentsTree(..., int forcedSpecTab = -1)` 新增可选参数；>=0 且 < MAX_SPECNO
  时跳过权重随机直接采用该 specno（-1 = 旧行为，既有调用方零改动）。
  `Randomize` 内 `InitTalentsTree()` 调用改为显式传参。
- `src/Bot/LfgGroupBotMgr.cpp`：
  - 新增文件内静态函数 `GetRandomSpecNoForRole(cls, role)`——职责→specno 映射
    （坦：战2/骑1/DK0/德1；奶：牧0|1/骑0/萨2/德2；DPS：战0|1/骑2/牧2/DK1|2/萨0|1/
    德0|3/猎贼法术0|1|2），多选随机；映射与 `LfgJoinAction::GetRoles` 反推口径一致。
  - spawn 时 `factory.Randomize(false, GetRandomSpecNoForRole(bot->getClass(), info.assignedRole))`。
- 映射找不到（理论上不会）回退 -1 → 旧的随机 roll，安全兜底。

## 部署 / 验证 / 回滚

- modules 为 static 编译，需重新编译 worldserver 生效（本次本地未编译验证）。
- 验证：
  1. 玩家以 DPS 排随机本 → 补位的坦 bot 应为防护战/防骑/血DK/熊（德看有厚皮光环 16931），
     奶 bot 应为戒律/神圣牧、奶骑、恢复萨/德；
  2. 本内踢人补位重排队 → 职责检查应答与实际天赋一致（日志无 WRONG_ROLES）；
  3. 观察坦 bot 有 "tank assist"/"pull" 策略、奶 bot 会治疗、装备属性匹配职责。
- 回滚：`cd modules/mod-playerbots && git checkout -- src/Bot/Factory/PlayerbotFactory.h src/Bot/Factory/PlayerbotFactory.cpp src/Bot/LfgGroupBotMgr.cpp`

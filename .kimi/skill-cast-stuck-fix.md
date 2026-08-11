# 卡技能全链修复：施法链路 6 处 + 双端 CD 对齐 6 项（2026-08-11）

> 纯前端修复（Unity 客户端 `D:\Unity\clientproj`），服务端未动。
> 触发问题：玩家反馈"英勇打击有时候按一次不生效，要连续按几次才生效"（纯手动操作）。
> 分析结论：主因全在客户端——伪 GCD 一刀切 + 等待队列吞键 + 下次普攻技能零反馈；
> 服务端"被打断/法术进行中/未准备好"回包大多是客户端在错误时机发包所致，客户端管住发送关口后噪音自然消失。

## 服务端技能/CD 模型（分析所得，后续改服务端时参考）

- 施法链：`HandleCastSpellOpcode`（`SpellHandler.cpp:376`）→ `Spell::prepare`（`Spell.cpp:3404`）→ CheckCast（`:3482`）→ SendSpellStart（`:3618`）→ TriggerGlobalCooldown（`:3646`）→ `_cast`（GO，`:3893` 施加 CD）→ SendSpellGo。
- **CD 在 GO 时刻施加**（`AddSpellAndCategoryCooldowns`，`Player.cpp:10869`）：自身 CD `rec>0?rec:catrec`（**rec 优先**，`:10953`）；**同 category + 同 spellfamily 的技能一起上类别 CD**（`:10957-10998`）；SPELLMOD_COOLDOWN 两边都应用，SPELL_AURA_MOD_COOLDOWN 只作用于 rec。
- **正常施法服务端不发 SMSG_SPELL_COOLDOWN**（仅 SPELL_AURA_MOD_COOLDOWN 改动或 CU_FORCE_SEND_CATEGORY_COOLDOWNS 时发）——客户端 CD 全靠自预测，必须与 DBC 语义严格对齐。
- GCD：`TriggerGlobalCooldown`（`Spell.cpp:8878`）= StartRecoveryTime，1.0~1.5s 区间内的法系技能（category 133 / 1500ms / 非近战非远程 / 非 ability）按 `UNIT_MOD_CAST_SPEED` 急速缩减；读条打断 `CancelGlobalCooldown` 会撤销 GCD（仅 CURRENT_GENERIC_SPELL）。
- `SMSG_COOLDOWN_EVENT`（`SendCooldownEvent`，`Player.cpp:11067`）语义 = **启动**该技能 DBC 冷却（药水等 IsCooldownStartedOnEvent 技能）；`SMSG_CLEAR_COOLDOWN` = 清除。
- CheckCast 冷却/GCD 拒绝均回 `SPELL_FAILED_NOT_READY`（`Spell.cpp:5637/5660`）；施法中再施法回 `SPELL_FAILED_SPELL_IN_PROGRESS`（`:3465`，要求 castCount≠0）。

## 客户端状态机要点（改前坑位）

- 客户端发 `CMSG_MOBILE_CAST_SPELL`(0x525) 后**不置任何状态**，全靠服务端包驱动：SPELL_START(Timer>0)→kStateSinging 读条态；SPELL_GO→kStateSkill 释放态；CAST_FAILED/SPELL_FAILURE→CastSkillFailed。
- kStateSinging 退出只有 5 条路（GO/FAILED/死亡/离场/自动寻路），**无超时**；`InCasting` = kStateSkill || `_inChanneled`（**不含 singing**）。
- 引导技能住 kStateSkill（非 singing）：GO 创建 `_skill`，MSG_CHANNEL_START 置 `_inChanneled` + TimeDuration。
- 群 CD（group 0）= 客户端 GCD 容器：`SkillCDGroupDic[0]`，`CooledDown` 检查它，`SetGroupCd` 写它（硬编码 type 0）。

## 改动清单一：排队高亮（MSkillDoubleDiskHandler.cs）

> 原有机制：`refreshCDs()` 里 `_waitSkillId`/`_autoSkillId` 匹配槽位时闪烁 `SkillMasks[i]`，但首亮延迟 1 秒 + 1Hz 慢闪，实战不可见。经典布局（Classic）无此逻辑（其 refreshCDs 走旧 M 体系），默认布局 DoubleDisk（`MPlayerSetting.cs:1017`）故未补。

- 新增字段 `_waitSkillBtnSlot`（当前闪烁槽位，-1 无）。
- 排队成功**当帧立即亮** + 0.5s 周期闪烁；`_waitSkillId` 清除即灭并复位记录；切页/刷新槽位 0.5s 内自恢复；双槽位同时排队（猛禽+自动射击）退化为双常亮。

## 改动清单二：施法链路 6 处（WSkillCore / WSkillComponent / WWaitingDataMgr）

1. **群 CD 豁免无 GCD 技能**：`WSkillCore.HasGlobalCooldown`（`IsNextAttack()` false 或 `StartRecoveryTime==0` → 不受 GCD）；`CooledDown` 对无 GCD 技能跳过群 CD 检查；`OnSkillStart` 的 `SetTimer(cd, 1.5f)` → `SetTimer(cd, core.GlobalCooldown)`。**英勇打击等不再被伪 GCD 封锁 1.3 秒**。
2. **等待队列**（WWaitingDataMgr）：`Update()` 移动时不再 `ClearAll()`（只取消自动普攻意图）；新增 `_waitSetTime` + 2.5s 过期（`WAIT_SKILL_EXPIRE_SECONDS`），`SetData` 埋点。**移动中/GCD 内按的技能不再被吃，也不会永久悬置**。
3. **读条看门狗**（WSkillComponent.Update，仅玩家）：新字段 `_singingSpellId`/`_singingDeadline`（OnSkillStart Timer>0 时设置 = 条时长+3s 兜底）；超时未收 GO/FAILED → 停 Lua 施法条 + DestroyFxBySpellId + ForceToDefault + `[SkillDiag] singing watchdog reset` 日志。**全技能永久灰死根除**。
4. **`_autoSkillId` 吞 GO 修复**：OnSkillGo 门从 `_autoSkillId<=0` 改为只吞 `data.SpellId == _autoSkillId` 的自动攻击 GO（特效分支保留）；其他技能 GO 正常切状态。**自动射击挂着读瞄准不再卡死**。
5. **`_waitSkillId` 自愈**：OnSkillStart 设置时记录 `_waitSkillSetTime`；Update 里 >5s 未解析（无 GO/FAILED）自动清零 + `[SkillDiag] waitSkill self-heal` 日志。**按钮不再永久失效**。
6. **CastSkillFailed 关联**：`ForceToDefault` 仅在 `spellId == _singingSpellId`（失败包属于当前读条技能）时执行。**读条中误按他键的拒绝包不再误杀读条显示**。

另：`CanSkill` 新增 kStateSinging 拦截——读条中按键返回 false 进等待队列、读条结束自动补发（替代服务端 SPELL_IN_PROGRESS 拒绝，零售式 spell queue 体验）。

## 改动清单三：双端 CD 对齐 6 项

| # | 问题 | 修复 |
|---|------|------|
| M1 | `CooldownEventResponse` 把"启动 CD"误实现为"清除 CD"（药水永不显 CD） | 改 `SetTimer(core.CoolDown/1000f, 0)` 启动冷却（WAttackRegister.cs） |
| M2 | `CoolDown` 取值 catrec 优先，与服务端 rec 优先相反 | 改 rec 优先，rec=0 才用 catrec（WSkillCore.cs） |
| M3 | 类别冷却客户端无扇出（震击系/审判系共享 CD 不显） | `WSkillCore.CategoryCoolDown` 新属性；`SetTimer` 设置 CD 时扇出（清除不扇出）；`WSkillComponent.SetCategoryCooldown` 遍历 `_skillMgr.AllSkillCores` 同 category 技能一起上 CD；`WSkillMgr.AllSkillCores` 访问器 |
| M4 | CD 起算 START（客户端）vs GO（服务端） | `OnSkillGo` 重同步去掉 `IsMainUISkillSlot` 限制，所有技能 GO 时刻重设 CD；`SetTimer` 的 2×延迟减法保留（正好抵消收发各一程延迟） |
| M5 | 打断后服务端撤 GCD、客户端多锁 ~1.3s | CastSkillFailed 的 singing 关联分支里 `SetGroupCdByType(0, 0)` 同步撤销 |
| M6 | 法系 GCD 急速缩减客户端不知道 | `GlobalCooldown`：1.5s GCD + 魔法系（SchoolMask≠1）+ 法术职业（骑/牧/萨/法/术/德，即 2/5/7/8/9/11）时按 `UNIT_MOD_CAST_SPEED` 缩减并夹 [1.0,1.5]；读不到按 1.0 兜底 |

## 已知取舍 / 待办

- 类别扇出只按 category 匹配，无服务端 SpellFamilyName 二次过滤（客户端表无此数据）；同 category 跨 family 在玩家技能书实际不存在，无影响。
- DK（6）未列入 GCD 急速职业（避免冰触等魔法系武器打击被误缩；DK 主要受符文约束），死亡缠绕 GCD 不缩属保守方向。
- `SetTimer` 扇出在 `SetFixedCD`/登录同步（SMSG_INITIAL_SPELLS）路径不经过——登录包服务端已按技能逐个下发，无需扇出。
- **服务端遗留（未动，后续按需要做）**：SpellQueue 无时间戳不过期 + 队头阻塞（`PlayerUpdates.cpp:2369-2397`， queued 零回包 `SpellHandler.cpp:424-437`）；not-in-spellbook 等静默 return（`:471-472`）；托管 AI castCount=0 硬断玩家施法 + 噪音包（仅托管场景，`PlayerbotAI::CastSpell`）。
- 经典布局（Classic）技能栏无排队高亮（refreshCDs 走旧 M 体系），需要时另补。

## 验证路径

- 战士双手武器：GCD 内按英勇打击 → 按钮立即亮闪、挥砍正常打出；移动中按不再被吃；猛击读条中按英勇 → 读条完自动补上，无"法术进行中"提示。
- 猎人挂自动射击读瞄准 → 读条正常结束；萨满连按不同震击/骑士审判 → 同系一起转 CD；喝药 → 按钮转 CD。
- 日志 `[SkillDiag] singing watchdog reset` / `waitSkill self-heal` 正常游玩应极少出现；出现即说明服务端有丢包路径，再修服务端（见上"服务端遗留"）。

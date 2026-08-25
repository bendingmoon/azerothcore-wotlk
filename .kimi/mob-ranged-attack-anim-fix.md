# 怪物远程射击动作缺失修复（2026-08-24）

> 纯客户端修复（`D:\Unity\clientproj`），服务端零改动。
> 症状：禁魔监狱萨格隆弓箭手（Sargeron Archer, entry 20901）等弓箭怪全程站立射击，无拉弓动作、无箭矢弹道；官方客户端同场景有完整弓箭射击表现。

## 根因（四层叠加，逐层验证）

1. **主攻击不是普攻是法术**：20901 SmartAI（smart_scripts）战斗中每 2.4-4.8s 施放 Shoot（普通 22907 / 英雄 38940，targettype=当前目标）——服务端发 SMSG_SPELL_GO，**没有 SMSG_ATTACKERSTATEUPDATE**，`WEvent_Attack → OnAttackEvent → PlayAttack` 近战普攻路径不触发。
2. **官方 DBC 此类法术本来就没有 SpellVisual**：经 Kaev/AzerothcoreDBCToSQL（3.3.5 官方 DBC SQL 镜像）核实 22907 `SpellVisualID=0`、瞬发（CastingTimeIndex=1）、Speed=40（有弹道）、要求装备弓/枪/弩（EquippedItemClass=2, Subclass=0x4000C）。客户端表 `SpellxSpellVisualWoW.bytes` 无 22907 映射与官方一致（**不是转换丢数据**）。官方客户端对这种"要求远程武器+弹道"的法术不走 SpellVisual，直接按武器类型播 AttackBow/AttackRifle + 弹药弹道。
3. **本端施法动画完全依赖视觉 kit**：`WSkill.TriggerAnim`/`HandleSkillEffect` 只吃 SpellVisualKit 的 type-6 效果；状态机层兜底动画名 `Skill`/`Singing` 在 `AnimationDataWoW` 表里**不存在**（`GetAnimationIdByName` 返回 -1 静默跳过）——所以无视觉的法术连通用施法动作都没有。
4. **唯一的弓箭分支限定错了属性**：`OnSkillGo` else 分支的 `STATE_ATTACK_BOW/RIFLE` 要求 `WeaponAttackType == RANGED_ATTACK`（即 SPELL_ATTR2_AUTO_REPEAT，猎人自动射击语义），怪物 Shoot 法术无此属性；且 `_autoSkillId` 只对本地玩家设置。

## 修复内容（客户端）

`WSkill/WSkillComponent.cs` `OnSkillGo` 第一分支（`!InCasting && !isAutoRepeatGo`）`OnActionEvent(args)` 之后新增：

- `!Entity.IsPlayer && IsRangedWeaponSpell(spellId)` → `Model.Ator.PlayAttack(STATE_ATTACK_BOW, STATE_SHEATH_BOW, 1)` + 触发箭矢弹道（`WEvent_Bullet`，复用猎人分支的硬编码箭矢 prefab，速度取 `core.Speed` 回退 28）。
- 新增 `IsRangedWeaponSpell(uint)`：`SpellEquippedItems` 表 `EquippedItemClass==2`（武器）且子类掩码含弓(1<<2)/枪(1<<3)/弩(1<<18)。
- 顺带覆盖：bot 猎人自动射击（75，同样无视觉）、Scatter Shot 23601（有视觉但只有音效/FX 无动作）、以及一切"要求远程武器"的怪物射击法术。玩家路径完全不动。

## 关键认知

- 怪物模型动画三来源：普攻包（ATTACKERSTATEUPDATE→WEvent_Attack）、法术视觉 kit（type-6）、OnSkillGo 猎人分支；都不覆盖"远程武器法术"，这是机制性缺口而非单个技能数据问题。
- `SpellEquippedItems.bytes`（2214 行，crypt -1712573623，字段 ID@4/Class@6/InvTypes@8/Subclass@10）按法术 ID 直查，22907/38940/23601/75 均在表且值与官方 DBC 一致。
- 验证工具：`var/trace_shoot_spell_22907.py`（施法时间/属性/视觉映射/kit 效果/动画一条龙）。
- 官方 3.3.5a Spell.dbc 逐字段校验法：Kaev/AzerothcoreDBCToSQL 的 Spell.sql，`CastingTimeIndex`、`EquippedItemClass/Subclass`、`Effect` 多锚点对齐后读 `SpellVisualID`。
- 读条型射击法术（Timer>0）也覆盖：START 进读条态，GO 时仍走第一分支补播。

## 验证清单

1. 编辑器编译 HotUpdate 程序集通过（本次改动未经编译）。
2. 禁魔监狱（map 552）萨格隆弓箭手：每次射击有拉弓动作+箭矢飞行；普通/英雄模式（22907/38940）都验。
3. 散射（23601）放箭动作+原视觉 FX 不冲突；钩网（36827，无武器要求）不播弓动作。
4. 猎人（含 bot）自动射击、玩家施法无回归；近战怪普攻动作无回归。

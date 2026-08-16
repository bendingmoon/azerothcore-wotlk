# Kimi Memory — AzerothCore WotLK Project

This file is the curated index for Kimi Code CLI. Detailed feature memories live in sibling files under `.kimi/`.

## Project Pairing

- **Server repo**: `D:\UnityWow\azerothcore\azerothcore-wotlk` (this repo)
- **Client repo**: `D:\Unity\clientproj` (Unity + tolua/Lua hybrid)
- **Current branch**: `Playerbot`
- **Key module**: `modules/mod-playerbots/`

## Memory Files

| File | Topic |
|------|-------|
| [auto-pilot.md](auto-pilot.md) | Real-player quest automation (auto-pilot) |
| [afk-grind.md](afk-grind.md) | In-place mob farming (AFK grind) |
| [mod-item-upgrade-review-fixes.md](mod-item-upgrade-review-fixes.md) | Item upgrade 全链路审查与修复（2026-07-18，高危7项+中危+C#，Lua 留用户） |
| [equip-reforge.md](equip-reforge.md) | 装备洗练（StatBooster / CMSG_BOOST_ITEM）全链路：四种模式、物品字段布局、配置表、已知疑点（2026-07-21） |
| [breakthrough-enchant.md](breakthrough-enchant.md) | 突破奖励词条：tiers 表 breakthrough_enchant_id，PROP_ENCHANTMENT_SLOT_1 写入，协议+客户端已实现，Lua 留用户（2026-07-21） |
| [item-upgrade-config.md](item-upgrade-config.md) | 装备升级配置文档（可直接给配置人员）：6 类表说明、req_type/stat_type 完整枚举、配档注意事项（2026-07-21） |
| [lfg-proposal-veto-fix.md](lfg-proposal-veto-fix.md) | LFG 匹配被 OnPlayerbotCheckLFGQueue 否决（真人带 botAI 误判）+ 替补 bot 等级死循环修复（2026-07-20） |
| [lfg-bot-whisper-teleport-fix.md](lfg-bot-whisper-teleport-fix.md) | 禁止 bot 向真人发悄悄话 + LFG 等待匹配期间禁止随机传送（2026-07-21） |
| [lfg-requeue-rolecheck-fix.md](lfg-requeue-rolecheck-fix.md) | LFG 踢人补位重排队 WRONG_ROLES：bot 职责检查改用 LFG 已存职责应答 + 前端抑制重复职责框（2026-07-24） |
| [lfg-bot-follow-stuck-fix.md](lfg-bot-follow-stuck-fix.md) | LFG bot 站着不动不跟随：等待期禁 grind/rpg 防传送瞬间进战斗 + 落单 bot 补发传送兜底 + master 失联 60s 清理 + 进队问候 + 禁止陪打 bot 自退队列（2026-08-11） |
| [lfg-bot-dungeon-teleport-fallback-fix.md](lfg-bot-dungeon-teleport-fallback-fix.md) | LFG bot 在队满血但不进本：兜底补发仍被 TeleportPlayer 检查卡死（战斗/坠落/疲劳不自愈）→ 两段式（补发→CombatStop+直接 TeleportTo 强拉）+ 传送途中防误清理（2026-08-12） |
| [lfg-votekick-small-group-fix.md](lfg-votekick-small-group-fix.md) | LFG 真人队长踢 bot 免投票直接出队：新钩子 OnPlayerbotLfgKickBypassVote + Group.cpp 直踢分支（计票逻辑未动；≤3 人队投票踢不出真人是原版特性）（2026-08-12） |
| [lfg-bot-force-assembly.md](lfg-bot-force-assembly.md) | LFG 强制组队装配：废弃撮合池，2 分钟后服务端直接组队整排必出 proposal；补位 bot 只排当前本；proposal 接受加固（2026-08-12） |
| [lfg-solo-group-disband.md](lfg-solo-group-disband.md) | 随机本掉线重登后不显示进入副本+退不出队列：单人 LFG 组存活 → 0 成员 GROUP_LIST 被前端误判解散 → 状态死锁；修复=RemoveMember 剩 1 人时按"最后成员"条件解散（在线在副本里则保留）+ 客户端 OnGroupList 用 LeaderGuid 区分单人 LFG 队伍与真解散（2026-08-13） |
| [lfg-bot-groupless-cleanup.md](lfg-bot-groupless-cleanup.md) | LFG bot 掉出队伍立即清理：wasGrouped 标记区分"进过组又掉出来"与"停驻/池排队本来无组"，前者轮询即清不再等超时（2026-08-13） |
| [lfg-assembly-combat-gate.md](lfg-assembly-combat-gate.md) | LFG 装配战斗门控：战斗中不 spawn/不装配（弹窗不撞战斗），300s 一轮×2 轮仍不脱战则 LeaveLfg 移出队列（无冷却光环）+系统消息；含官方口径对照（2026-08-15） |
| [lfg-bot-role-spec.md](lfg-bot-role-spec.md) | LFG 陪打 bot 按分配职责生成天赋：Randomize/InitTalentsTree 支持指定专精 + 职责→specno 映射（坦/奶/DPS 各归其位，装备/铭文/策略/职责应答自动传导）（2026-08-11） |
| [lfg-leader-transfer-rules.md](lfg-leader-transfer-rules.md) | LFG 队伍队长规则：新钩子 OnPlayerbotCanChangeGroupLeader 禁止传队长给 bot（只拦手动）；OnChangeLeader 里 LFG 组传给真人时所有 bot 的 master 切新队长（2026-08-11） |
| [death-state-sync-fix.md](death-state-sync-fix.md) | 死亡卡死/无灵魂状态/血条残留：前端死亡判断改 GHOST 标志驱动（活人 HP=1 不误判）+ Attr.IsDead 直接置位 + 事件兜底重发；playerbots 死后自动行为跳过真人（2026-07-25） |
| [death-release-stuck-fix.md](death-release-stuck-fix.md) | 死亡卡死防御性修复：IsDead setter 标志/清血条提前+全防护、UpdateAttrValue/EquipVisible 补 try-catch、GetUpdateValues 空 catch 加日志、Update() 每秒补弹释放框；Repop/SelfResurrect 加真人守卫（2026-07-29） |
| [quest-state-refresh-fix.md](quest-state-refresh-fix.md) | 接任务后 TalkDlg2 选项不刷新/NPC 头顶标识不对：C# 接交任务后重拉 gossip + Lua GotoNpc 改为原地重建选项 + WNpcFxComponent 状态缓存同步（2026-07-26） |
| [quest-giver-icon-missing-fix.md](quest-giver-icon-missing-fix.md) | NPC 该有感叹号却不显示：删掉单查回包 status==8 的 QUESTGIVER 旗标丢弃 + refreshNpcStatus 补 6/7/9 状态映射 + LoadingEnd 补发切图批量刷新（2026-08-14） |
| [remote-movement-sync.md](remote-movement-sync.md) | 远程玩家移动同步全套修复：航位推测+追赶加速+防倒退+追停朝向；官方端游发送端协议差异（STOP 残留标志/STOP_STRAFE 真停/SET_FACING 无标志/心跳稀疏）；待优化项（2026-07-27） |
| [spell-fx-lifetime-fix.md](spell-fx-lifetime-fix.md) | 技能/buff/引导特效生命周期整体修复：自毁链收口 OwnerFxId、消退窗口防硬切、IsBullet 去重、死亡清理、引导特效绑定时长、WBuff 用错管理器泄漏（2026-07-27） |
| [skill-cast-stuck-fix.md](skill-cast-stuck-fix.md) | 卡技能全链修复（纯前端）：群CD豁免无GCD技能、等待队列移动不清+过期、读条看门狗、_autoSkillId 精确吞包、_waitSkillId 自愈、排队高亮即时化 + 双端 CD 对齐 6 项（COOLDOWN_EVENT 反向/rec优先/类别扇出/GO起算/打断撤GCD/急速GCD）；含服务端技能与 CD 模型参考（2026-08-11） |
| [range-skill-lock-target-revert.md](range-skill-lock-target-revert.md) | 暴风雪等范围技能点按不锁最近敌方、落自己脚下：客户端 SVN r963 把 OnDown 的 EffectOffset/CastingOffset 清零，已还原；同版本其余 3 文件评估无影响；附无 svn CLI 时用 wc.db+pristine 取历史版本的方法（2026-08-12） |
| [stance-buff-icon-fix.md](stance-buff-icon-fix.md) | 战士姿态 buff 图标不显示：IsAuraShow 误用 retail 语义 NO_AURA_ICON(0x10000000) 于 WotLK 数据，变形/姿态光环豁免该检查（2026-08-10） |
| [buff-ghost-icon-fix.md](buff-ghost-icon-fix.md) | buff 图标串单位/复活/重复：根源是网络层响应对象单例复用 + WAuraUpdateAllResponse.Auras 跨包累积（Auras.Clear）；配套快照差集/注册即回放/单条排队回放/时长刷新/白名单（2026-08-10） |
| [relogin-no-ui-fix.md](relogin-no-ui-fix.md) | 重登/杀进程重登后主 UI 全不显示：直接根因 SceneEnterMgr ExtraUi 静态表原地改写致二次进场景 gmatch 崩溃、面板列表为空；服务端接管路径改"先踢后登"统一完整登录序列；客户端登出流程补全（76 处理器/正式清理/关双连接/免弹窗）+ 场景兜底（firstInScene 15s 超时/SwitchMap 判空重试）（2026-08-11） |
| [tbc-pvp-prices.md](tbc-pvp-prices.md) | TBC S4 赛季 PvP 价格还原：S3/S4 竞技场点数+等级门槛、荣誉散件荣誉+牌子（itemextendedcost_dbc 10000+ 段，SQL+生成脚本+conf/赛季配套），待办客户端 DBC 补丁与 S2（2026-07-28） |
| [other-memories.md](other-memories.md) | Index of original Claude Code memories |
| [worldserver-lag-spike-analysis.md](worldserver-lag-spike-analysis.md) | 世界服 Update time diff 尖峰分析：主因 LFG 按需 bot 成批登录+Randomize（重试放大）；方案=先错峰退避后池化复用；含 PerfMon/gdb 验证法、配置调整、杂项清理、代码索引（2026-08-12） |
| [gameobject-door-system-analysis.md](gameobject-door-system-analysis.md) | 门系统全链路：破碎大厅小怪免疫事件链（AT 4347/开门触发）、GO 字段布局（BYTES_1/PARENTROTATION/FLAGS）、CMSG_GAMEOBJ_USE(177) 无锁校验、四元数转换公式 q=(-y,-z,x,w)、前端门渲染+交互待办清单（2026-08-14） |
| [lfg-bot-corpse-pile-fix.md](lfg-bot-corpse-pile-fix.md) | 奥格尸体堆：副本战死 bot 被 OnRemoveMember 的 TeleportToEntryPoint（无死亡检查）拉回奥格、登出落库留 3 天；修复=清理登出前复活+清尸，尸体过期 3 天→2 小时（2026-08-15） |
| [lfg-bot-refollow-after-master-death.md](lfg-bot-refollow-after-master-death.md) | 队长死一次后 bot 永不跟随：释放灵魂转发包给活 bot 上 -follow,+stay，恢复仅认 20 码内 CMSG_RECLAIM_CORPSE（被奶活/离远即永久卡死）；修复=CheckAndCleanup 1s 轮询主人复活即 +follow,-stay（2026-08-16） |
| [lfg-bot-cleanup-during-combat-fix.md](lfg-bot-cleanup-during-combat-fix.md) | 城墙老三杀瓦兹德后 bot 战斗中消失：LFG 完成判定绑 17537 瓦兹德之死（纳杉还活着）→ FINISHED 即清 bot；修复=清理前检查队伍任一成员战斗中则跳过下秒重查（2026-08-16） |

## Memory Management Rule

When a new feature or large topic needs to be remembered:

1. Create a new sibling file: `.kimi/<feature>.md`.
2. Add one row to the **Memory Files** table above.
3. Do **not** dump detailed feature content directly into `memory.md`.

This keeps `memory.md` bounded and readable while allowing feature notes to grow independently.

## Remember When Answering

- Always verify paths against the current tree before acting.
- This is a Playerbot fork; upstream `master` conventions may differ.
- Prefer minimal changes and follow existing code style.
- Server-side changes usually need corresponding Unity C# + Lua changes for mobile features.
- Do not run full builds unless explicitly requested.

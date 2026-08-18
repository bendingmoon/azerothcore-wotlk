# GM 命令：.character setreputation（自定义）

设置指定角色（**无论在线或离线**）的指定阵营声望为给定数值。

- 实现：[src/server/scripts/Commands/cs_character.cpp](../src/server/scripts/Commands/cs_character.cpp) `HandleCharacterSetReputationCommand`（第 728 行起）
- 注册：`character` 命令表，`Console::Yes`（游戏内与控制台均可用）
- 权限：RBAC **1005** `Command: character setreputation`（[RBAC.h](../src/server/game/Accounts/RBAC.h) 第 690 行，custom permissions 1000+ 区段）

## 语法

```
.character setreputation $playername #factionId #standing
.character setreputation $playername #factionId +delta
.character setreputation $playername #factionId 等级名
```

| 参数 | 说明 |
|---|---|
| `$playername` | 角色名（在线或离线均可）；在游戏内省略时取当前目标/自己 |
| `#factionId` | 阵营 ID（Faction.dbc），必须 `reputationListID >= 0`，否则报 unknown faction |
| `#standing` | 绝对声望值 **-42000 ~ 42999**（超出自动 clamp）；`+数值`（如 `+1000`、`+-500`）= 在**当前显示值**基础上加减（不乘声望倍率，精确生效）；或等级名 |

等级名支持前缀匹配与本地化名称：

| 等级 | 起始值 | 区间 |
|---|---|---|
| hated 仇恨 | -42000 | -42000 ~ -6001 |
| hostile 敌对 | -6000 | -6000 ~ -3001 |
| unfriendly 冷淡 | -3000 | -3000 ~ -1 |
| neutral 中立 | 0 | 0 ~ 2999 |
| friendly 友善 | 3000 | 3000 ~ 8999 |
| honored 尊敬 | 9000 | 9000 ~ 20999 |
| revered 崇敬 | 21000 | 21000 ~ 41999 |
| exalted 崇拜 | 42000 | 42000 ~ 42999 |

> 用等级名设置时，实际设为该等级的**起始值**（如 `exalted` = 42000，而非满值 42999）。

## 示例

```
.character setreputation Bob 932 42999     # 灰舌死誓者 设为崇拜满值
.character setreputation Bob 932 exalted   # 同上，但设为崇拜起始值 42000
.character setreputation Bob 72 honored    # 暴风城 设为尊敬(9000)
.character setreputation Bob 942 0         # 塞纳里奥远征队 设为中立
.character setreputation Bob 942 +1000     # 塞纳里奥远征队 在当前值上 +1000（离线也可用）
```

> `+delta` 模式说明：在线取 `GetReputation()`（含基础声望的显示值）+ delta 后 clamp；
> 离线取 `baseRep + character_reputation.standing`（无记录按 0）+ delta 后 clamp，再存回差值。
> 刻意不走 `SetOneFactionReputation` 的 incremental 路径，避免乘 `RATE_REPUTATION_GAIN` 导致数值不准。

## 奥尔多/占星者联动（互斥镜像）

对 **奥尔多(932)** 或 **占星者(934)** 的修改会自动按**实际生效的变化量**镜像到对方阵营（一边加多少，另一边减多少，各自的 clamp 独立计算）：

- 绝对值、`+delta`、等级名三种写法都会触发镜像；镜像量 = `新值 − 旧值`（被 clamp 截断的部分不会镜像，保证只反映真实变化）。
- 在线/离线两条路径都实现；在线走 `SetOneFactionReputation`（对方阵营同样触发脚本钩子/成就），离线直写对方阵营的 `character_reputation` 行（保留原 flags，无行则按默认 flags 新建）。
- 镜像是对称的：把奥尔多调低，占星者会等量回升——GM 工具里刻意保持"两边之和不变"，这样命令永远搞不出双崇拜的号。
- 实际变化量为 0（设为原值）时跳过镜像。修改成功会额外输出一条对方阵营的结果消息。

## 荣耀堡/萨尔玛阵营校验（硬编码）

荣耀堡(946) 只允许联盟角色、萨尔玛(947) 只允许部落角色，跨阵营直接报错拒绝（"目标角色属于X阵营，无法设置Y的声望。"），在线/离线都校验：

- 在线取 `Player::GetTeamId()`，离线取 `Player::TeamIdForRace(race)`。
- 为什么是硬编码而非通用 DBC 校验：荣耀堡/萨尔玛对**对立阵营也有基础声望槽位**（base = -42000 仇恨，实测客户端 Faction 表确认），所以"无匹配槽位即拒绝"的通用规则对这两个阵营不生效；而主城阵营（暴风城 72 等）联盟/部落槽位又互相交叉，通用规则误判面大，故只硬编码这一对。
- 其他对立阵营声望（主城等）不做校验，GM 自便。

## TBC 阵营 ID 速查（商城"声望"商品配置用）

从客户端 Faction 表实抠（`var/dump_factions.py`，全部 repIdx ≥ 0 可用于本命令）：

| 阵营 | ID | 阵营 | ID | 阵营 | ID |
|---|---|---|---|---|---|
| 荣耀堡(联盟) | 946 | 萨尔玛(部落) | 947 | 塞纳里奥远征队 | 942 |
| 贫民窟 | 1011 | 沙塔尔 | 935 | 时光守护者 | 989 |
| 奥尔多 | 932 | 占星者 | 934 | 星界财团 | 933 |
| 孢子村 | 970 | 沙塔尔天空卫队 | 1031 | 灵翼之龙 | 1015 |
| 奥格瑞拉 | 1038 | 紫罗兰之眼 | 967 | 流沙之鳞 | 990 |
| 灰舌死誓者 | 1012 | 破碎残阳 | 1077 | | |

商城 PHP 对接（`shopconfig.type='声望'`，`item=[{"id":"946","num":"1000","type":"8"}]`）：付款前校验段按 race 拦错阵营（联盟 race ∈ 1,3,4,7,11）；执行段 foreach 发 `.character setreputation $名字 $id +$num`（SOAP，在线离线均生效）。注意奥尔多/占星者两个商品互斥镜像，买一边会扣另一边。

## 客户端声望刷新链路（配套修复，2026-08-18）

修复前：**客户端没注册 SMSG_SET_FACTION_STANDING(292) 的处理器**——`InitialFactions` 只在登录时全量下发一次，之后刷怪/交任务/GM 命令的声望变化（服务端 `ReputationMgr::SendState` → 292）客户端全丢，声望面板重登前一直是旧数据；且 `ReputationCtrl` 只在 OnActive 时读一次。

修复后链路（在线购买即时可见）：

1. C# 新包类 `SetFactionStandingInfo.cs`（`GameObjects/Player/Models/Response/`）：解析 292 —— float(0) + uint8 + uint32(count) + count×{reputationListID, standing}。
2. `WNetClient.OnSetFactionStanding`（注册于 147-149 行段）：按 reputationListID 更新 `InitialFactions.Factions` 并重算展示字段（`UpdateFactionDisplay`，从 InitialFactions 处理器抽出的共用方法），然后 `CallFunc("ModuleMgr.ReputationMgr.OnReputationChangedNtf")`。
3. Lua `ReputationMgr.lua`：新增 `EventDispatcher` + `ReputationChangedEvent`，Ntf 里 `Dispatch`。
4. Lua `ReputationCtrl:BindEvents`：订阅该事件 → `ShowItems()` 重读 `WPlayerInfo:GetReputation()`。

已知小瑕疵（未修，跟随现状）：`FactionEntry.GetFactionShowStanding` 对负声望（仇恨段）有 off-by-one（-42000 显示 -41999）；面板 Lua 只显示 `CurrentValue>0` 的阵营。

## 工作原理

### 玩家在线

`SetOneFactionReputation(factionEntry, amount, false)` + `SendState`：

- 立即生效并推送客户端（`needSend`/`needSave` 置位）
- 自动触发连锁逻辑（ReputationMgr.cpp:424-454）：
  - 脚本钩子 `OnPlayerReputationChange`
  - 降到 Hostile 以下自动 `SetAtWar(true)`；升出 Hostile 自动解除
  - 声望等级计数器与 4 类声望成就 criteria 更新
- 有 `HasLowerSecurity` 越权检查（不能改权限高于自己的 GM）

### 玩家离线

直写 `character_reputation` 表：

1. 查 `characters` 表取 race/class
2. 按 DBC `BaseRepRaceMask/BaseRepClassMask` 计算基础声望 baseRep（镜像 `ReputationMgr::GetBaseReputation`）
3. **存 `standing = 目标值 − baseRep`**（DB 存的是不含种族/职业基础声望的差值，与在线路径语义一致）
4. 已有记录保留原 flags；新记录用 DBC 默认 flags 并强制加 `FACTION_FLAG_VISIBLE`
5. `CHAR_DEL_CHAR_REPUTATION_BY_FACTION` + `CHAR_INS_CHAR_REPUTATION_BY_FACTION` 先删后插

玩家下次上线由 `ReputationMgr::LoadFromDB` 重建状态并即时重算等级计数，最终状态与在线设置一致（离线时不触发即时脚本钩子）。

## 数值体系（ReputationMgr）

```
PointsInRank = {36000, 3000, 3000, 3000, 6000, 12000, 21000, 1000}
Reputation_Bottom = -42000    Reputation_Cap = 42999
显示值(GetReputation) = DB存储值(Standing) + 种族/职业基础声望(BaseRep)
```

## 与官方 `.modify reputation` 的对比

| | `.character setreputation`（本命令） | `.modify reputation`（官方） |
|---|---|---|
| 目标 | 按角色名，**在线/离线均可** | 仅选中的在线玩家 |
| 控制台 | 可用 | 不可用 |
| 设为 0 / neutral | 正常 | **静默失败**（`if (!amount) return false;` 的坑） |
| 越界值 | 命令内 clamp | 靠 ReputationMgr 内部 clamp |
| 增量偏移 | 支持 `+1000` / `+-500` 前缀（按当前值精确加减，不乘倍率） | 支持第 3 参数 `#delta`（仅等级内偏移） |

## 配套 SQL（部署时导入）

| 文件 | 作用 |
|---|---|
| `data/sql/updates/pending_db_auth/rev_1786773346000000000.sql` | 插入 rbac_permissions 1005，并链接到 GM 命令角色 197（Administrator 经继承获得） |
| `data/sql/updates/pending_db_world/rev_1786773346000000001.sql` | `command` 表帮助文本 |

## 部署状态

代码与 SQL 均已合入本地，**待重新编译 worldserver + 数据库更新器导入 pending SQL 后线上可用**。

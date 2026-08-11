# 重新登录/断线重登后主 UI 全部不显示修复（2026-08-11）

> 症状：首次登录一切正常；游戏内点"重新登录"再走一遍登录后，3D 世界、怪物血条、伤害数字都正常，但动作条/聊天框/背包/头像/菜单等主 UI 按钮一个都不显示。主动重登必现；杀进程立刻重登偶尔出现。

## 根因（代码逐行核实）

1. **直接根因（实测报错坐实）**：`SceneEnterMgr.lua:130` 每次进场景执行
   `CurrentMainUiTableData.ExtraUi = StringToTable(...)`，把**静态配置表**
   `main_ui_config[1]` 的 `ExtraUi` 字符串原地改写成 table。同一进程第二次进
   45 场景时 `StringToTable` 收到 table → `gmatch` 崩溃（"string expected, got
   table"）。崩溃点在填充面板列表（:132）之前，而列表在 :122 刚被清空 →
   `ShowMainUI` 拿到空列表 → 主 UI 全不显示。与登录方式无关，二次进 45 场景必中。
2. **服务端路径分叉**：登出未完成（战斗被拒 `MiscHandler.cpp:416-474` / 20s 计时
   / 断线 60s 宽限 `WorldSessionMgr.cpp:153-163`）时旧角色仍在世界，重登走
   `HandlePlayerLoginToCharInWorld` 接管路径（`CharacterHandler.cpp:1175`），比完整
   FromDB 登录**少发**公会登录信息、`OnPlayerLogin` 钩子、LoadPet、鬼魂处理等——
   服务端当"断线重连刷新"，客户端却全量推倒重建，语义不匹配（杀进程立刻重登必走此路）。
3. **客户端登出流程缺陷**：`SMSG_LOGOUT_RESPONSE`(76) 无处理器（战斗中点重新登录
   毫无反应）；`OnLogoutResponse` 只关 auth 不关 world；回登录界面借道
   `WAuthConnection.OnClosed` 无条件弹出的"重连失败"对话框（`WAuthConnection.cs:207`）。
4. **兜底层隐患（已加固，非根因）**：主 UI 唯一激活点是
   `MScene.OnSceneLoaded → game.ActiveMainPanels`；场景加载器被
   `WMapMgr.firstInScene` 门闩卡住（`MSceneLoader.cs:205`），`SwitchMap` 里
   `player.VehicleOrModel` 未就绪时 NRE 会让地图状态机永久停在 Finish；世界渲染走
   独立瓦片流不受门闩控制——构成"世界正常、UI 全无"的完整机理。
   `WEntityMgr.isRelogin` 置位后全工程无人读取（重登路径没写完的佐证）。

## 改动内容

### 服务端（azerothcore-wotlk，需重编 worldserver）

- `src/server/game/Handlers/CharacterHandler.cpp` `HandlePlayerLoginOpcode`
  （:815-819）：同角色离线会话的"接管"分支改为 `sess->LogoutPlayer(true)`（正常登出
  存档、移出世界），随后落入完整 FromDB 登录。**任何方式的重登都拿到完整登录包序列**。
  语义变化：`EnableLoginAfterDC=1`（默认）从"接管旧角色"变"先踢后登"；`=0` 仍是
  DuplicateCharacter 拒绝。`HandlePlayerLoginToCharInWorld` 保留但已无调用方。

### 客户端 C#（D:\Unity\clientproj\Assets\HotUpdate\MoonClient）

- `WNetwork/Models/Response/WServerAuthChallengeRequest.cs`：新增
  `WLogoutResultResponse`（解析 SMSG_LOGOUT_RESPONSE 的 Reason(u32)+Instant(u8)）。
- `WNetwork/ApplicationLayer/WNetClient.cs`：
  - 注册 `SMSG_LOGOUT_RESPONSE`(76) → `OnLogoutResultResponse`：Reason!=0 通知 Lua
    关等待框+提示，留在游戏里；==0 继续等 COMPLETE。
  - `OnLogoutResponse`（SMSG_LOGOUT_COMPLETE）：先调 Lua 正式清理回登录界面，再依次
    关闭 world、auth 两条连接（不再借道重连失败弹窗）。
- `WNetwork/ApplicationLayer/WAuthConnection.cs`：新增一次性标记
  `SuppressReconnectFailedDialog`，主动登出的 NormalClose 不再弹"重连失败"框；
  其余关闭路径（真断线/重连失败）行为不变。
- `Scene/MSceneLoader.cs`：`firstInScene` 门闩加 15s 超时，超时打错误日志
  （"firstInScene hold timeout"）并强制完成场景加载。
- `WMaps/WMapMgr.Core.cs` `SwitchMap`：player/VehicleOrModel 未就绪判空 + 每 5 帧
  重试（上限 120 次≈10s，日志 "SwitchMap player not ready, retry"），避免状态机
  永久卡死。

### 客户端 Lua（D:\Unity\clientproj\Assets\Scripts\Lua）

- `ModuleMgr/SceneEnterMgr.lua:130`：**根因修复**，ExtraUi 仅在仍为 string 时转换
  （幂等），二次进场景直接复用 table；MainIcon 等字段只读无此隐患。
- `WNetwork/WNetwork_Init.lua`：新增 `OnLogoutCompleteHandlers`（关等待框→
  `LogoutToAccount()` 正式清理）与 `OnLogoutRefusedHandlers`（关等待框→按原因提示
  "战斗中无法退出登录"等）并注册；等待框超时 5s→25s 盖住服务端 20s 登出计时。
- `WNetwork/WNetwork_Handler.lua`：补两个 handler 槽位声明。

## 已知残余（未修）

- 移动端 FIN 丢失时服务端认为旧会话仍存活 → 重登走完整路径但同 GUID 双角色共存
  （原版 AzerothCore 对僵尸连接的固有缺陷，本次未处理）。
- 20s 登出等待期间不支持取消（客户端未实现 CMSG_LOGOUT_CANCEL）。
- `WEntityMgr.isRelogin` 仍是死标记；重登链路如有更多状态残留问题，先看客户端日志
  三行标记：`SwitchMap player not ready, retry` / `firstInScene hold timeout` /
  `Duplicate SwitchTo 45`。

## 部署 / 验证 / 回滚

- 服务端需重新编译 worldserver（本次本地未编译验证，遵循 AGENTS.md 约定）；
  **Lua 需重新导出 .bytes**（`Assets/Resources/Lua/**` 仍是旧拷贝），C# 走热更。
- 验证：
  1. 野外脱战点"重新登录"→ 20s 内自动回登录界面无弹窗 → 重登 → 主 UI 完整、无
     gmatch 报错；
  2. 战斗中点"重新登录"→ 提示"战斗中无法退出登录"，留在游戏里；
  3. 旅店（休息区）点"重新登录"→ 秒回登录界面 → 重登正常；
  4. 杀进程立刻重登 → 主 UI 完整（服务端先踢后登生效，日志伴随一次正常登出存档）；
  5. 客户端日志不再出现 `StringToTable`/`gmatch` 异常；三行兜底标记正常情况不应出现。
- 回滚（服务端）：`git checkout -- src/server/game/Handlers/CharacterHandler.cpp`
- 回滚（客户端，按文件还原）：`Scene/MSceneLoader.cs`、`WMaps/WMapMgr.Core.cs`、
  `WNetwork/ApplicationLayer/{WNetClient.cs,WAuthConnection.cs}`、
  `WNetwork/Models/Response/WServerAuthChallengeRequest.cs`、Lua 侧
  `ModuleMgr/SceneEnterMgr.lua`、`WNetwork/WNetwork_Init.lua`、`WNetwork/WNetwork_Handler.lua`
  （及对应 .bytes 重新导出）。

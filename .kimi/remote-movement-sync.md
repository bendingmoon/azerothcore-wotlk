# 远程玩家移动同步修复（队友界面移动延迟/漂移/停不下来）（2026-07-27）

> 纯客户端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动（服务端转发链路已核实干净）。
> 症状：两人组队时 B 看 A 的移动延迟 1 秒以上、A 停了 B 还在走、后退前闪、停后朝后、平移停不下来、快速变向路径对不上。

## 链路架构（排障先读）

- **发送侧**（WPlayer.cs）：起步/停下等 `IsMoving` 跳变发对应 START/STOP 包；持续移动每 0.5s 发 `MSG_MOVE_HEARTBEAT`；**移动中变向（前进/后退/左右平移切换）现在会立即补发起始包**（`_lastSentMoveDir`）。
- **服务端**（`MovementHandler.cpp:344-396`）：校验通过即 `SendMessageToSet` 原样转发，无节流/攒批/"停了不发"判断。丢包只在：位置非法、服务端 spline 未走完、root 状态矛盾、anticheat 踢人。
- **接收侧**（WRole）：`WMoveRegister` 分发 → `WMoveComponent.onMoveEvent` → `PlayerUpdate` 直线追赶。NPC/怪物走另一条 `SMSG_MONSTER_MOVE` → `LagSyncUpdate` 影子跟随路径，**本次全部改动都用 `Entity.IsRole` 门控，不影响 NPC**。
- 收包管道：网络线程入队 → `WNetClient.HandleNetEvent()` 每帧 `while` 全排空（WNetClient.cs:587-654），无每帧上限，延迟 <1 帧，不用动。

## 核心机制：航位推测（dead reckoning）

`WMoveComponent._drContinue`：仅远程玩家，包带 FORWARD/BACKWARD/STRAFE 标志且**不是 MSG_MOVE_STOP** 时为 true。

- 为 true 时追到同步点不停下，沿 `_targetDir` 外推（每次 +2m 续目标），等下个包校正；
- 起步包/稳态下目标点≈当前位置时，用 `BuildDrDirection()` 按包内朝向+方向锁合成移动方向（前进=朝向、后退=反向、平移=±90°），不再原地愣住；
- 落后 >1m 时追赶加速（`1 + clamp((dis-1)*0.15, 0, 0.35)`，最多 +35%），追近恢复原速；
- 防倒退：新目标在移动反方向且 `dis < 1m` 判定为旧位置回退，不掉头继续外推（真掉头 1 个包周期内偏差必超 1m 被接受）；
- `_stopFace`：所有判定停止的包记录真实朝向，追停到位后以此站立；停止目标在身后 <1.5m 时不回走，直接落位+朝向+停。

## 官方端游当发送端的协议差异（坑全在这）

| 现象 | 官方端行为 | 处理 |
|---|---|---|
| STOP 残留标志 | `MSG_MOVE_STOP` 可能仍带 STRAFE_RIGHT 等移动标志 | drContinue 判定排除 `MSG_MOVE_STOP`，停止以 opcode 为准 |
| 纯平移停止 | 发 `MSG_MOVE_STOP_STRAFE flags=0`，不是 MSG_MOVE_STOP | 停止族包 flags=0 即真停，**不能当空标志包忽略** |
| STOP_STRAFE 通用 handler | 会清方向锁导致停后朝向变侧向 | STOP_STRAFE 改注册到 `HandlePlayerMoveStop`（保留方向锁） |
| 转向包 | `MSG_MOVE_SET_FACING/SET_PITCH` 无移动标志、位置是当前位置 | 分流到 `HandlePlayerSetFacing` 只转面向，不碰移动状态 |
| 心跳稀疏 | 持续同向移动时秒级无包（事件驱动），与 Unity 端 0.5s 心跳不同 | 接收端外推必须容忍秒级盲期；盲期挖坑靠追赶收敛 |
| 后退心跳 | 速度/方向信息要按 WALKING/BACKWARD/STRAFE 标志还原 | `HandlePlayerMoveHeartbeat` 按标志设 isRun + 方向锁 |

## 修改文件清单

- `WEvent/EventArgs/WMoveEventArgs.cs` — 新增 `moveFlags`、`opcode` 字段（Clone/Recycle 同步）
- `WEntity/Registers/WMoveRegister.cs` — 6 个 handler 填 moveFlags/opcode；心跳按标志还原方向锁和跑走；SET_FACING/PITCH 分流 `HandlePlayerSetFacing`；STOP_STRAFE 改走 `HandlePlayerMoveStop`
- `WComponents/Common/WMoveComponent.cs` — `_drContinue`/`_lastDrDir`/`_stopFace`/`DR_MOVING_FLAGS`、`BuildDrDirection()`、外推+追赶加速+防倒退+追停落位（全部 IsRole 门控）
- `WEntity/WPlayer.cs` — 心跳带真实方向标志（原来写死 FORWARD，后退会前闪）；移动中变向立即补发包（`_lastSentMoveDir`）

## 待优化项（未做，按优先级）

1. **发送侧心跳 0.5s→0.1s**（WPlayer.cs:463 一行）：盲区缩到 0.1s，快速变向路径基本 1:1。用户暂缓，正式场景发送端是自家 Unity 端时收益最大。
2. **追赶加速 +35%→3 倍**（PlayerUpdate boost 一行）：长盲期 10m+ 大坑从 6s 收敛到 1-2s。用户暂缓。
3. **偏差 >10m 瞬移对齐**：用户已拒绝一次，观察后再定。
4. **3 秒静默悬案**：官方端 strafe 时观察到 3s 无包，与服务端注释的"0.5s 心跳"不符。未定论是官方端不发还是链路丢。定论手段：`MovementHandler.cpp` 加临时日志数 MSG_MOVE_HEARTBEAT。
5. 已知小瑕疵：心跳对 walking 玩家的动画、`Time` 字段语义误用（`moveArgs.delay = Time/1000 - RTT/2`，Time 是开机毫秒数不是时长，当前路径不用它，隐患）。
6. 审查过的边缘情况（2026-07-27 全量复查结论，均有意不改）：STOP_STRAFE flags=1（前进中停平移）方向锁残留影响 BuildDrDirection 合成方向，0.5s 内心跳自愈；MSG_MOVE_TELEPORT 不清 drContinue，下一拍自愈；防倒退(1m)/身后落位(1.5m)/_stopFace 三者职责不重叠。

## 标准测试场景

- 以**官方端游**为发送端（协议差异最大）：直线跑/急停/后退/左右平移/平移急停/快速变向/边跑边转向。
- 以 Unity 端为发送端回归。
- NPC/怪物移动不用回归（改动全部 IsRole 门控）。

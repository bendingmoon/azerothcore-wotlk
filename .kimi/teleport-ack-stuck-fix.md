# 传送 ACK 漏发导致移动同步永久冻结修复（2026-08-24）

> 纯客户端修复（Unity 客户端 `D:\Unity\clientproj`），服务端无改动。
> 症状：与端游玩家组队，本地角色已走出很远，端游那边看角色还在原位置一动不动——服务端坐标完全没更新。重登可自愈。

## 根因模型

单包丢失不会出此症状（0.5s 心跳兜底校正），必然是起步/心跳/停止包**全部**被持续掐断。
服务端 `HandleMovementOpcodes`（MovementHandler.cpp:344）对移动包的静默丢弃点：

| 丢弃点 | 条件 | 自愈手段 |
|---|---|---|
| :355 | `plrMover->IsBeingTeleported()`（传送信号量未清） | **无超时/无重发**，只有等 ACK 或重登 |
| :366 | 包内 guid != `m_mover` guid（farsight/possess 状态错配） | 状态解除 |
| :530 | `movespline` 未 Finalized（AI/击退/恐惧驱动中） | 生成器结束 |
| :537/594 | DISABLE_MOVE/root 状态矛盾 | 状态解除 |

本次修的是第一类：客户端两条 teleport ACK 路径都存在漏发分支，而服务端信号量
`IsBeingTeleportedNear/Far` 漏一次 ACK 就永久卡死。

## 客户端漏发分支（修复前）

- **N1 近传送**：`MoveTeleportResponse`/`MoveTeleportAckResponse`（WMoveRegister.cs）的 ACK 被
  `GetEntity(guid) != null && entity.IsPlayer` 门控；`GetEntity` 在实体 `Deprecated || Destroying`
  时返回 null（WEntityMgr.cs:1069）——传送包落在实体拆除/重建窗口（重登、切图清理）即静默吞掉，
  不回 ACK、无日志、无重试。
- **F1 远传送**：`SwitchMap` 玩家实体未就绪重试 120 次后 give up（WMapMgr.Core.cs），加载状态机
  未启动 → `OnArriveNewWorld` 永不调用 → 无 `WORLDPORT_ACK`。
- **F2 远传送**：120s `loadingMaxTime` 兜底只覆盖 ADT 等待；之后 `PreloadAllBuildings` 回调与
  `_gateGlobalWMOPending` 无任何超时，挂住即永久不发 ACK。

## 修复内容

- `WEntity/Registers/WMoveRegister.cs`
  - 新增 `EnsureTeleportAck(guid, flags, time)`：实体查不到时，确认是本玩家
    （`WEntityMgr.Player.UID` 或 `WPlayerInfo.ServerCharacter.GUID`）就直接按包内 guid 回
    `WMoveTeleportAckRequest`，ACK 与实体状态解耦；附黄色日志。
  - 两个传送 handler 的 `else` 分支接入。
- `WMaps/WMapMgr.Core.cs`
  - 新增 `_pendingWorldportAck` 记账 + `MarkWorldportAckPending()` / `ClearWorldportAckPending()`
    / `FlushWorldportAck(reason)`；`Update()` 在 mapHolder 检查之前做 45s
    （`WORLDPORT_ACK_MAX_WAIT`）超时强发；`SwitchMap` give-up 分支补发；`Reset()` 清账。
- `WNetwork/ApplicationLayer/WNetClient.cs`
  - `OnNewMapResponse`（SMSG_NEW_WORLD）入口 `MarkWorldportAckPending()`；
  - `OnArriveNewWorld()` 发出 `WORLDPORT_ACK` 后 `ClearWorldportAckPending()`
    （正常/兜底/Lua 所有调用路径统一清账，保证每个 NEW_WORLD 恰好一次 ACK）。

## 安全性备注

- 服务端对多余 ACK 天然免疫：`HandleMoveWorldportAck` 非 far 传送中直接 return
  （MovementHandler.cpp:53-55），`HandleMoveTeleportAck` 校验 near 信号量+guid（:286-292）。
- 45s 超时 < ADT 等待的 120s 合法上限：慢盘机器可能在加载中提前发 ACK，服务端会提前开始
  推送世界状态，客户端按常规动态 spawn 处理，可接受；两次强发都有 warning 日志可观察。
- 未覆盖的剩余嫌疑（若复发且日志未命中则查这两处）：farsight/possess 状态卡死导致
  `MoverGuid` 错配（WPlayer.cs:808）、托管状态双端不一致（auto-pilot.md 已知残留）。

## 复发时的定位

客户端日志命中 `[WMoveRegister] 传送包到达时玩家实体未就绪` 或 `[WMapMgr] 强制补发 WORLDPORT_ACK`
即坐实本路径；服务端侧可在 MovementHandler.cpp:355/366 加节流日志数包确认。

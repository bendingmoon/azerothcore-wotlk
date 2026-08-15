# GameObject 门系统：服务端机制分析 + 前端联动手册

> 起因：破碎大厅（Shattered Halls, map 540）老一 Nethekurse 门前小怪"无效的目标"。
> 结论：小怪免疫是刻意设计，解除事件靠"踩 Areatrigger 4347"或"打开 BOSS 房门"触发。
> 由此引出门系统的完整需求：前端目前没做门的开关状态渲染和交互，本文档是重做门逻辑的交接文档。

## 一、关键对象数据（破碎大厅案例）

| 项 | 值 |
|---|---|
| BOSS 房入口门 | GO entry **182539**，guid 4700，坐标 (141.516, 266.366, -11.568)，朝向四元数 DB rotation=(0,0,1,~0)（绕 z 转 180°） |
| BOSS 房出口门 | GO entry **182540**，guid 4712，坐标 (214.488, 266.167, -11.518) |
| 模板 | type=0(DOOR)，Data0(startOpen)=0，**Data1(openLockId)=1662**（锁，疑似钥匙 28395/开锁），Data2(autoCloseTime)=0 |
| DB 初始 | `gameobject.state=1`（GO_STATE_READY=关），`animprogress=255` |
| 副本注册 | `doorData` 里两门均为 `DOOR_TYPE_PASSAGE`（`instance_shattered_halls.cpp:38-39`）→ **BOSS 死亡前副本脚本强制关，死亡后强制开** |
| 事件联动 | BOSS `UpdateAI` 每 tick 轮询门 182539，`GetGoState()==GO_STATE_ACTIVE`（开）→ 启动 intro、解除小怪免疫（`boss_nethekurse.cpp:262-274`） |
| Areatrigger 4347 | map 540，球心 (181.405, 220.670, -20.122)，半径 30 码（门锁之前 16 码，正常走路必踩） |

## 二、服务端门机制（全部已读码验证）

### 状态模型

- `GOState`：`GO_STATE_ACTIVE=0`（开）、`GO_STATE_READY=1`（关）、`GO_STATE_ACTIVE_ALTERNATIVE=2`（替代/破坏）。
- 状态通过**更新字段**广播（无单独的门包），字段布局 `UpdateFields.h:395-405`（偏移相对 OBJECT_END）：

| 字段 | 偏移 | 说明 |
|---|---|---|
| `GAMEOBJECT_DISPLAYID` | +2 | 模型 |
| `GAMEOBJECT_FLAGS` | +3 | `GO_FLAG_IN_USE`(0x1)：门打开/使用中置位 |
| `GAMEOBJECT_PARENTROTATION` | +4..+7 | **电梯路径旋转**（`gameobject_addon.ParentRotation`，`GameObject.cpp:319` `SetTransportPathRotation`）；无 addon 行恒 (0,0,0,1)。**不是门朝向**（本文档旧版有误） |
| `GAMEOBJECT_DYNAMIC` | +8 | |

- **门朝向的真实来源**：DB `rotation0-3` → `GameObject::Create` → `SetWorldRotation`（`GameObject.cpp:312`）→ 打包 int64（`UpdatePackedRotation`，`GameObject.cpp:2237-2248`：z 占 bit0-20、y bit21-41、x bit42-63，有符号定点，w 折入符号）→ 创建包 movement 块 `UPDATEFLAG_ROTATION`(0x200) 下发（`Object.cpp:480-483`；GO 恒带此 flag，`GameObject.cpp:63`）。创建包 values 块只含非零字段（`Object.cpp:501`），门通常只收到 `PARENTROTATION+3=1.0f`。
- 注意 GO 的 movement 块走 `UPDATEFLAG_POSITION`(0x100) 分支（`Object.cpp:367`），位置/标量 O 在 MovementPosition 里，不在 Stationary。
| `GAMEOBJECT_FACTION` | +9 | |
| `GAMEOBJECT_LEVEL` | +10 | |
| `GAMEOBJECT_BYTES_1` | +11 | **byte0=门状态**（`SetGoState` 写入，`GameObject.cpp:2499`）；byte2=artKit；byte3=animProgress |

- 初始状态：`GameObject::Create` 用 DB 的 `state`/`animprogress` 初始化（`GameObject.cpp:352/359/397`）。
- 碰撞：关门时开碰撞（`SetGoState` → `EnableCollision(state == GO_STATE_READY)`，`GameObject.cpp:2509`）。
- 副本强制：`InstanceScript::UpdateDoorState`（`InstanceScript.cpp:258-286`），PASSAGE 门 `bossState==DONE` 才开；BOSS 进战/死亡时触发重设并照常广播。

### 开门交互协议

1. 客户端发 **`CMSG_GAMEOBJ_USE`（opcode 177 / 0x0B1）**，负载 = GO guid。
2. `HandleGameObjectUseOpcode`（`SpellHandler.cpp:327`）：校验交互距离（门 5 码，`GameObject.cpp:2917-2918`）、骑乘/遥控状态 → `GameObject::Use`。
3. `GameObject::Use` → case DOOR（`GameObject.cpp:1495`）→ `UseDoorOrButton`（:1410）→ `SwitchDoorOrButton`（:1447）：置 `GO_FLAG_IN_USE`，状态 READY→ACTIVE（再点则 ACTIVE→READY，toggle）。
4. 字段更新走标准 `SMSG_UPDATE_OBJECT` 广播给周围所有玩家。
5. **注意：`CMSG_GAMEOBJ_USE` 路径没有锁校验**。锁是两条别的路：
   - 官方客户端行为：点锁着的门时客户端自查 Lock.dbc，无钥匙显示"已锁"，有钥匙则对门施放开锁法术；
   - 法术路径 `Spell::EffectOpenLock`（`SpellEffects.cpp:2093`）只对**箱子/物品**做开锁 loot，不开门。
   - 结论：**前端直接发 177 即可开任何门（包括 182539），服务端不拦**。要还原钥匙机制只能前端自查 Lock 1662（钥匙 28395 / 开锁技能）。
6. `autoCloseTime=0` 的门开了不会自动关。

### 坐标/旋转换算（前端现有约定，已验证自洽）

- 本地→服务器：`ConvertPosition`（`WMapMgr.Utils.cs:19`）= `ToServerPosition`（`WPosition.cs:112`）：`server = (-local.z, local.x, local.y)`。
- 服务器→本地：`ServerPosToLocal`（`WMapMgr.Utils.cs:24`）= `ToClientVector`（`WPosition.cs:118`）：`local = (server.y, server.z, -server.x)`。
- 该映射是带手性翻转的反射。**四元数转换公式**：`q_unity = (-q.y, -q.z, q.x, q.w)`（虚部按轴映射后取负，w 不变）。
  - 验证：182539 的服务端四元数 (0,0,1,0)（绕服务器 z 180°）→ packed=0x100000 → 解包 (0,0,-1,0) → 转换 (0,1,0,0) = Unity 绕 y 180°，正确。
  - 客户端实现：`WMapMgr.UnpackServerRotation`（packed int64 解包）+ `WMapMgr.ServerQuatToLocal`（手性转换），见 `WMapMgr.Utils.cs`。作用于解包后的 WorldRotation，**不要**用 PARENTROTATION。

## 三、前端现状与待办（客户端工程 `D:\Unity\clientproj`）

现状问题（根因已查明）：
1. ~~门显示成默认姿态（像开着的）~~：`GAMEOBJECT_BYTES_1` byte0 其实已解析（`WGameObject.GetWGOState`），初始姿态由加载回调 `DoStateAnimation` 驱动。
2. **门旋转位置不对**：双根因——(a) `UpdateAttrGameObjectActive` 把 PARENTROTATION 当"坐标+弧度"调 `SetPosRotation`，门创建时被挪到本地原点、朝向乱值；(b) 真正朝向在 movement 块 packed int64（`WMovementRotation.Rotation`），旧代码误当弧度/被标量 O 抢先。
3. **点门没有交互发包**：`TouchObject.setPlayerTarget` 没有 type==0 分支（仅带锁门走了无效的 Lock 表路径）。

待办清单（2026-08-14 已实施，实测验证待做）：

- [x] GO 创建/更新包解析：`GAMEOBJECT_BYTES_1`/`GAMEOBJECT_FLAGS`/`GAMEOBJECT_DYNAMIC` 原有通用掩码解析已覆盖；byte3 animProgress 不做插值（用 glTF Open/Close clip + Clamp）。
- [x] 门渲染：state 0=开 / 1=关 / 2=替代态（按开处理）→ `WGameObject.DoStateAnimation`；包驱动（`UpdateAttrGameObjectActive`）+ 100 tick 轮询兜底。**视觉当前为模拟实现**（无真动画，见 §七"门模拟动画"）。
- [x] 门朝向：packed rotation 解包 + `(-q.y,-q.z,q.x,q.w)` 转换（`WMapMgr.Utils.cs`），创建时由 `WEntityMgr` GO 分支喂入（`SetDoorRotationFromPacked`）；`SetPosRotation` 误用已收窄为仅电梯（type 11/15）（`WEntity.cs:1457`）。
- [x] 本地碰撞：关门挡路 = 门子网格的 BakeMesh MeshCollider（layer 19，`SetupDoorBlocker`），`AllowPass` 按状态开关。寻路为直线 steering，无动态障碍概念。
- [x] 交互：点门 ≤5 码发 `CMSG_GAMEOBJ_USE`(177,guid)（`TouchObject.setPlayerTarget` type==0 分支，复用现成 `WNPCMgr.UseGameObject` 链路）；超距弹提示"你距离太远，走近点再开门"（`WMapMgr.ShowTips`）。
- [x] 锁门策略：**不做 Lock 检查**（产品决策：一律直接开；服务端本不校验）。`TouchObject` 的 Data1≠0 锁分支已收窄为仅 type 1。
- [ ] **实测验证**（编辑器进破碎大厅）：门朝向与门框吻合（若差恒定 90°，在 `SetDoorRotationFromPacked` 补 `* Quaternion.Euler(0,90,0)`）；初始关闭；点击开/关切换；关门挡人；BOSS 进战自动关、死亡自动开；宝箱/电梯回归。
- [x] （已完成的关联修复）`WMapMgr.AreaTrigger.cs` 的 `PositionToGridKey` 已从 `(x,z)` 改为 `(x,y)`（服务器坐标 z 是高度）。

## 四、服务端可选改动（尚未做，按需提）

1. **`CMSG_GAMEOBJ_USE` 路径补锁校验**：目前无钥匙也能开门，属于轻度漏洞。位置：`HandleGameObjectUseOpcode`（`SpellHandler.cpp:327`）或 `GameObject::Use` DOOR 分支（`GameObject.cpp:1495`），用 `GameObject::GetSpellForLock`（`GameObject.cpp:3028`）判定。
2. **Nethekurse intro 兜底触发**：目前依赖 AT 4347（客户端发包）或开门轮询。可在 BOSS `UpdateAI` 加"有玩家进入 X 码即启动 intro"，彻底摆脱对 AT/门的依赖（对自定义客户端更稳）。

## 五、测试要点

1. 非 GM 号从入口正常清过去：y≈250 前必踩 AT 4347 → BOSS 开始 RP → 小怪可打。（GM 模式跳过 AT 脚本，`MiscHandler.cpp:727`；传送/飞行直接进房会绕过触发球。）
2. 门渲染：进副本门应为关闭姿态；点门开门 → 姿态/动画正确；BOSS 进战门自动关、死亡自动开。
3. 门旋转：182539/182540 朝向与门框吻合（180° 案例）。
4. 无钥匙点锁门的行为（取决于是否做前端锁检查 / 服务端锁校验）。

## 六、相关文件

服务端（`D:\UnityWow\azerothcore\azerothcore-wotlk`）：- `src/server/scripts/Outland/HellfireCitadel/ShatteredHalls/boss_nethekurse.cpp`（intro/门轮询/小怪免疫解除）
- `src/server/scripts/Outland/HellfireCitadel/ShatteredHalls/instance_shattered_halls.cpp`（doorData）
- `src/server/game/Entities/GameObject/GameObject.cpp`（Use/UseDoorOrButton/SwitchDoorOrButton/SetGoState/GetSpellForLock）
- `src/server/game/Handlers/SpellHandler.cpp:327`（HandleGameObjectUseOpcode）
- `src/server/game/Spells/SpellEffects.cpp:2093`（EffectOpenLock）
- `src/server/game/Entities/Object/Updates/UpdateFields.h:395`（GO 字段布局）
- `src/server/game/Instances/InstanceScript.cpp:258`（UpdateDoorState）

前端（`D:\Unity\clientproj\Assets\HotUpdate\MoonClient`）：
- `WMaps/WMapMgr.AreaTrigger.cs`（AT 检测，网格 key 已修）
- `WMaps/WMapMgr.Utils.cs`（ConvertPosition / ServerPosToLocal / **UnpackServerRotation / ServerQuatToLocal**）
- `WEntity/WGameObject.cs`（门状态机 `DoStateAnimation`、`SetDoorRotationFromPacked`、`SetupDoorBlocker`/`AllowPass`）
- `WEntity/WEntity.cs:1457`（`UpdateAttrGameObjectActive`：PARENTROTATION 仅电梯用）
- `WEntity/Mgr/WEntityMgr.cs:834`（GO 创建分支喂 packed rotation）
- `Input/TouchObject.cs:489`（点门发 177，`DoorUseDistance=5`）
- `WEntity/Models/WMovementInfo.cs`（UPDATEFLAG_ROTATION → `WMovementRotation.Rotation` long）
- `WEntity/Models/WPosition.cs`（ToServerPosition / ToClientVector）
- `WEntity/WPlayer.cs:774`（SendStartForwardMove 移动发包换算参考）
- `WNetwork/Models/Request/WTeamHandlerRequest.cs:75`（TriggerAreaRequest 发包格式参考）
- `WNetwork/Models/Request/WNPCHandlerRequest.cs:260`（`WGameObjectUse`：opcode 177 现成包类）

## 七、联调记录（2026-08-14）

- **门朝向 90° 偏差**：实测 packed 解包 + 手性转换后仍差 90°，原因是 glTF/M2 模型内禀朝向。在 `SetDoorRotationFromPacked` 补 `* Quaternion.Euler(0, -90, 0)`（注意：**实测方向是 -90**；先写成 +90 时门正/背面正好对调）。
- **182539 模板实况**（`data/sql/base/db_world/`）：`gameobject_template:11658` —— type 0、displayId 6773、Data0=0、**Data1=1662**、AIName/ScriptName 均空（排除脚本吞包）；`gameobject_template_addon:11595` —— **flags=2（GO_FLAG_LOCKED）**，但 `GameObject::Use` 的 DOOR 分支不查 LOCKED，直接 Use 仍会开；`gameobject:2661` —— guid 4700、orientation=π、rotation=(0,0,1,~0)、state=1(关)。
- **lootState 排查**：构造默认 `GO_NOT_READY`（`GameObject.cpp:71`），首个 Update tick 经 default 分支置 `GO_READY`（:596），所以 `UseDoorOrButton` 的 `m_lootState != GO_READY` 早退对正常门不成立。
- **177 全链静态排查结论**：opcode 注册 ✓（`Opcodes.cpp:308`）、handler 距离 5 码 ✓（`GetInteractionDistance`:2917）、无 ScriptName/AIName ✓、packed guid 往返与 NPC 交互同管线 ✓。静态上"≤5 码发 177 必开门"。若无反应，按以下顺序对分：
  1. **GM 命令**（免编译）：门旁执行 `.gobject activate 4700`（与 177 同走 `UseDoorOrButton`）。能开 → 问题只在 177 路径；不能开/能穿门但视觉不变 → 客户端收包/渲染侧。
  2. **network 日志**（免编译）：worldserver.conf 加 `Logger.network=5,Console Server`，点门看有无 `WORLD: Recvd CMSG_GAMEOBJ_USE Message`。无此行 → 包没到 handler；有此行 → 只剩距离/状态静默 return，再加临时日志（需重编译）定位。
- **联调结论（已实锤）**：177 全链正常——点门服务端切状态、客户端收包、`AllowPass` 关碰撞、人能过。当时"没反应"只是**视觉没动**。
- **门动画为何播不了**：GO/地图 doodad 走离线 glTF 预制体管线（`GLTFLoaderMgr`），导出时未出动画资产（exports 下 world/dungeons 无 Animations 目录）；m2anim（`M2RuntimeAnimator`）只对角色/生物的运行时 M2 管线（`Character`/`Creature`+`M2Simple`）接线，GO 预制体无 `Character` 组件 → `WAnimator.Play("Open")` 恒空跑（控制台有 `M2Animator 未初始化` 警告）。动画名表没问题：`AnimationDataWoW` 有 Open=148/Close=146/Opened=149/Closed=147。
- **门模拟动画（当前实现）**：`WGameObject.PlayDoorVisual(bool open, bool instant)` 是**视觉唯一入口**——**在世界空间**绕竖直轴（Unity Y）、以渲染包围盒中心为轴心翻转 90°（`DoorOpenAngle`），0.8s 插值（`DoorAnimDuration`），创建/刷新时 instant 定格。转的是预制体实例子节点，与实体朝向/DoorBlocker 无冲突。注意：翻转**必须做在世界空间**——最初在预制体局部空间 `Euler(0,angle,0)`，因 M2 模型局部 Y 轴不是世界竖直轴，门会"倒下"。**【以后要换真动画（M2 管线或 clip）只改 PlayDoorVisual 内部，DoStateAnimation 等调用方不动】**。可选方向：A) GO 接运行时 M2 管线（M2Simple 解析 doodad .m2 + InitAnimator）；B) 导出工程补 doodad 动画 clip 后按名播放。
- **DoorBlocker 最终方案 = BakeMesh 碰撞体**：踩坑历程——① 用 displayinfo GeoBox 算：与视觉差 90°（GeoBox 是 M2 模型空间，轴约定和运行时模型不一致）；② 渲染节点世界 AABB：太大（门框/装饰刺全包进去）；③ 直接拿蒙皮网格 sharedMesh 加 MeshCollider：错位横躺（**蒙皮渲染位置由骨骼驱动，sharedMesh 只是绑定姿态的原始顶点**）。最终：`SkinnedMeshRenderer.BakeMesh()` 烘焙当前渲染姿态成新网格（顶点在 SMR 本地空间），挂到 SMR 节点下（identity 局部变换）→ 与视觉严格重合；静态网格（MeshFilter）直接用原网格。layer 19 保证 CharacterController 碰撞；关门启用、开门禁用（`AllowPass` 遍历 `_doorBlockers`）。烘焙网格是运行时 new 的 Unity 对象，`OnDestrory`/`Initialize` 里 `CleanupDoorBakedMeshes()` 手动 Destroy 防泄漏。另注意：门模型走**运行时 M2 管线**（`SpellObjects : ModelRenderer`，`buildM2/*.m2.bytes` 现场建网格），管线本身不建碰撞体（`Character.cs:1091`/`Creature.cs:641` 的加碰撞代码是注释掉的），根部只有 `WModel` 自动加的 1×2×1 trigger BoxCollider（点选用）——"启用预制体现成碰撞体"这条路不存在，必须现加。

# 好友系统排查与修复记录（2026-08-06）

涉及两个工程：
- 服务端：`D:/UnityWow/azerothcore/azerothcore-wotlk`（AzerothCore fork，branch Playerbot）
- 客户端：`D:/Unity/clientproj`（Unity 热更 C# + tolua/Lua）

## 一、排查结论

### 服务端（无问题）
好友核心（`SocialMgr.*`、`Socialhandler.cpp`）与上游 master 逐字节一致，mod-playerbots 无相关 hook。
- 登录自动推送全量列表：`Player.cpp:11535` → `SendSocialList(SOCIAL_FLAG_ALL)`
- 数据来自 `character_social` 表（JOIN `characters`，`deleteinfos_name IS NULL`，`LIMIT 255`——JOIN 不上的行静默丢弃）
- opcode：CMSG_CONTACT_LIST=0x66 / SMSG_CONTACT_LIST=0x67 / SMSG_FRIEND_STATUS=0x68 / CMSG_ADD_FRIEND=0x69，客户端值一致，封包格式逐字段对齐

### 官方客户端好友状态一直灰色（非代码改坏）
`SocialMgr::GetFriendInfo`（SocialMgr.cpp:213-254）只有三种情况会灰显：
1. 好友账号 gmlevel ≥ 2（`IsGMAccount()`，官方设计，私服测试号最常踩）
2. 跨阵营 + 上游 5 月提交 `b3f882048`/`9b227c8cf` 删掉了 `AllowTwoSide.*` 配置，旧 conf 里的 key 已失效，需 RBAC 授权：`INSERT INTO rbac_linked_permissions (id, linkedId) VALUES (195, 28), (195, 29);`
3. 好友 GM 隐身（`.gm visible off`）

### 客户端两套网络栈
- 死栈：`SocialObjectServer.cs`（注册了但从不连接）、所有 `Network.Define.Rpc/Ptc.*` protobuf 好友协议（服务端没有）
- 活栈：`WNetClient.cs:228-229` 注册 `SMSG_CONTACT_LIST/SMSG_FRIEND_STATUS` → `WSocialMgr` → `LuaEngine.CallFunc("ModuleMgr.FriendMgr.ReceiveGetFriendInfo")`
- 私聊：收 = C# `WChatMgr.cs:113` CallFunc → `FriendMgr.ReceivePrivateChatNtf`；发 = `FriendMgr.SendChatMsgSimple`（`WChatMgr:SendChatMessage(msg, 20, name)`，20=whisper，按名字发）

## 二、本次修复内容

### C# `Assets/HotUpdate/MoonClient/WInfo/WSocialMgr.cs`（需重编译热更程序集）
- `OnFriendStatus` 重构：按 Result 分支处理；错误结果（NOT_FOUND/ALREADY/SELF/LIST_FULL/ENEMY）正确提示，不再误报成功/插幽灵记录；修复 REMOVED 时 null 引用风险；**所有好友结果统一调 `OnFriendUpdate()`**（解决之前只有 REMOVED 才刷新 Lua，上线/下线/新增不刷新的问题）
- 新增 `AddFriendByName(string name)`：按名字加好友
- 新增 `GetIgnoreInfos()`：屏蔽名单（原仅 guid 的 ignoreList）组装名字/职业/等级给 Lua；`OnPlayerCheckBack` 里对屏蔽条目补名字刷新
- `DelFriend` 去掉名字缓存依赖（删除只需 guid）
- `OnLogout` 清空 friendList/ignoreList（原来换角色残留）
- `OnIgnoreUpdate` 增加通知 `ModuleMgr.FriendMgr.OnIgnoreUpdateNtf`

### Lua `ModuleMgr/FriendMgr.lua`
- 修复 `setUnReadData`/`GetUnReadData` 括号错位（`tostring(a == b)` 恒真 → 未读永远只有 1 条、红点指错人）
- 新增 `ClearUnReadData(uid)`，`RequestReadPrivateMessage` 内调用（已读消红点）
- 新增 `GetIgnoreDatas()`、`OnIgnoreUpdateNtf()`、事件 `IgnoreListChangeEvent`
- `GetContactsData` 去掉陌生人场景的 logError 刷屏

### Lua `UI/Ctrl/WowFriendsCtrl.lua`（新好友面板：聊天/好友/屏蔽三页签）
- 添加好友：`ShowYesNoInputDlg` 输入名字 → `WSocialMgr:AddFriendByName`（仿战队/公会邀请）
- 删除好友：改 `WSocialMgr:DelFriend(tonumber(uid))`（原走死协议 Rpc.DeleteFriend）
- 发消息按钮：切到本面板聊天页签并选中（原跳旧 Community 面板）
- 发送聊天：改 `SendChatMsgSimple`（原走死协议 Ptc.SendPrivateChatMsg）；选中联系人时记录 `_currentContactData`
- 聊天记录：`GetChatSqlite()` 懒加载（原直接读字段基本为 nil）；新增 `_appendChatMsg` 统一去重+落库（修一条消息显示两遍、`SaveData` 传 Lua table 报错）；落库用 `MoonClient.ChatDataMgr.ChatData.New()`
- 屏蔽页签：数据源改 `GetIgnoreDatas()`；取消屏蔽后靠 `IgnoreListChangeEvent` 刷新
- 空态提示改用 Panel 绑定字段 `FriendsAreaEmptyText`/`ChatAreaEmptyText`/`IgnoreAreaEmptyText`

### 模板
- `WowFriendsItemTemplate` / `WowFriendsContactTemplate`：在线判定 `status == 1` → `status ~= 0`（WoW 状态 0=离线 1=在线 2=AFK 4=DND）

### 注解 stub
- `UnityLuaAPI/MoonClient_WSocialMgr.lua` 补 `AddFriendByName` / `GetIgnoreInfos`

## 三、遗留/注意事项

### 2026-08-06 补充：陌生人私聊不显示修复
问题：陌生人发私聊，聊天页签联系人列表里查不到就把数据丢了，消息看不到。
- C# `WSocialMgr.MakeTempContact`：名字缓存未命中时，先用 `ChatMessage` 自带的 SenderName/Class/Level/Icon 顶上，名字仍空则主动 `QueryPlayerName`（回来后 `OnPlayerCheckBack` 自动回填并刷新 Lua）
- Lua `WowFriendsCtrl._refreshContacts`：未读 sender 在 ContactsDatas 查不到时，用消息自带信息构造临时条目（ContactFlags=0x8）显示出来，不再丢弃

## 三、遗留/注意事项（原内容）

- 收到私聊会强制切到聊天页签（`WowFriendsCtrl:OnReceivePrivateChat` 末尾），属产品行为，未改
- 陌生人名字未缓存时不能发私聊（whisper 按名字），会提示"无法找到当前好友"，等 name query 回来即可
- 旧 `FriendsHandler`（Community 面板）仍在用部分老路径，未动
- 服务端 `character_social` 的 JOIN 会静默丢行（好友被删号等），排查数据问题先看这里
- C# 改动未跑 Unity 编译验证，热更构建时留意输出
- **tolua 是生成式 wrap 绑定**：`MoonClient` 类型方法要暴露给 Lua，需在 `Assets/HotUpdate/MoonClient/ThirdParty/MoonClient_<类名>Wrap.cs` 里注册（`_GT` 列表在 `MoonClientExportSettings.cs`，`LuaBinderOfMoonClient.cs` 统一 Register）。新增 C# 方法后要么重新跑生成工具，要么照现有模式手写 RegFunction + 包装方法（string 参数参考 ShowTips，List 返回值参考 GetSocialContactInfos 用 `ToLua.PushSealed`）

## 四、测试清单

1. 输入名字加好友：在线 / 不在线 / 不存在的名字 / 自己 / 重复添加 → 提示正确
2. 删除好友 → 列表自动刷新
3. 好友上线/下线 → 列表状态实时变化（不重开面板）
4. 私聊收发、重开面板后历史记录、已读红点消除
5. 屏蔽（玩家菜单 AddIgnore）→ 屏蔽页签显示 → 点击条目取消屏蔽
6. 换角色登录 → 列表不残留上一个角色的数据

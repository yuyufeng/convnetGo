# ConvnetGo

ConvnetGo 是一个 **QQ 风格的即时通讯（IM）+ 虚拟组网** 应用，采用**双层混合模型**：

- **IM / 社交层**：账号密码登录、好友、用户组、富文本（表情）聊天，全部经 Go 信令服务器（存储转发）。
- **网络互联层**：成为好友或同属一个组的用户之间，通过 **STUN P2P + TAP 虚拟网卡**组成同一个虚拟局域网；
  P2P 打洞失败时经**信令服务器中继**（替代 TURN）。默认不做 NAT / 不转发广播，按目的 IP 路由，并可做防火墙管控。

> 由旧版「Go P2P 组网工具 + Vue Web 客户端」演进而来。**Vue/Electron 客户端已淘汰**，改为全新的 **Qt6 桌面客户端**（Windows 优先）。

---

## 架构

| 组件 | 说明 |
|------|------|
| **服务端**（Go） | 扩展自原信令服务器。TCP 信令、TURN/中继、bbolt 持久化、HTTP 管理后台。服务端与客户端是同一个 Go 程序，`-s` 进入服务端模式。 |
| **客户端**（`convnet-qt/`，Qt6 Widgets / C++） | 全新桌面客户端：登录/好友/群组/聊天 UI、libdatachannel P2P、TAP 虚拟网卡、防火墙。 |

- 帧格式：`[4 字节大端长度] + JSON{Version, CMDType, Message[]}`。
- 身份：账号 + bcrypt 密码；`PublicID = md5(account):userID`；虚拟 IP 从 `10.110.0.0/8` 按 userID 分配。
- P2P：libdatachannel（C++ WebRTC + libjuice），DataChannel 标签 `data`，非 trickle 完整 SDP。

## 功能

- **账号**：账号密码注册 / 登录（bcrypt），昵称可被搜索。
- **好友**：按昵称/ID 搜索、加好友申请与审批、好友列表（在线状态 + 虚拟 IP）、删除。
- **群组**：建群（每人 ≤5 组，每组 ≤10 人，服务端强制）、搜索、申请入群、群主审批；**入群密码**（凭密码免审批直接加入）。
- **群管理**：创建者为群主，可授予/撤销管理员、踢人、转移群主、解散；群主退出前须先转移或解散。
- **聊天**：单聊 + 群聊，`QTextEdit` 富文本 + emoji 表情（10 列网格选择器），回车发送；离线消息在对方重连后送达。
- **在线状态**：上线/下线实时推送；断线自动重连。
- **网络互联**：好友/同组成员自动建立 P2P（失败回退服务器中继）+ TAP 虚拟网卡组成虚拟 LAN；用户资料里显示对接方式（P2P/中继）、虚拟 IP、收发字节、信号强度（按 RTT）。
- **防火墙**：本地按方向/动作/对端/协议/端口段的规则（拉黑某对端即隔离）。
- **界面**：单一树形（好友/群组可折叠、群可展开显示成员）、深色主题、彩色/灰色头像区分在线、系统托盘、消息提示音 + 角标闪烁、免打扰（全屏游戏自动免打扰）。
- **服务器管理后台**：Web 后台管控每用户是否可用服务器中继、中继限速（KB/s）、已用流量。

## 目录结构

```
convnetgo.go, shServer.go, imserver.go, store.go, im_types.go,
adminserver.go, relaycontrol.go, portocol.go, user.go   # Go 服务端 + 协议
convnet-qt/                                              # Qt6 客户端（CMake 工程）
  src/core/   信令 + 协议(FrameCodec/SignalingClient/Protocol.h)
  src/model/  AppModel(单一数据源) / Identity / Types
  src/p2p/    P2PManager / PeerLink / RelayTransport(libdatachannel)
  src/net/    TapManager / LinuxTun / WinTun / Router / Firewall
  src/ui/     MainWindow / ChatWindow / LoginDialog / GroupDialog / ...
  build-win.bat, README.md
DEPLOY-LINUX.md    # 服务端 Linux 编译/运行/部署文档
dist/              # 预编译二进制（客户端 zip + 各平台服务端）
```

## 快速开始

### 服务端

```bash
# 编译（纯 Go，无 CGO）
go build -o convnetgo .          # 或 convnetgo.exe (Windows)
# 运行（工作目录需有 convnet.json，会生成 convnet.db）
./convnetgo -s
```

监听端口：

| 端口 | 协议 | 用途 |
|------|------|------|
| 13903 | TCP | 信令（客户端连这里） |
| 13902 | UDP | TURN / 中继 |
| 8099  | TCP | 管理后台（Basic Auth，默认密码 `admin`，**勿对公网开放**） |

Linux 交叉编译、systemd/Docker 部署、防火墙与排错详见 **[DEPLOY-LINUX.md](DEPLOY-LINUX.md)**。
预编译的服务端/客户端二进制见 **[dist/](dist/)**。

### 客户端（Qt6，Windows）

需 Qt 6（MSVC 2022 64 位）、vcpkg（libdatachannel）、Visual Studio C++ 工具链。

```bat
cd convnet-qt
build-win.bat            :: 配置 + 编译 + windeployqt 打包 DLL/插件
```

产物 `convnet-qt/build-win/convnet-qt.exe`。详见 **[convnet-qt/README.md](convnet-qt/README.md)**（含 WSL/Linux 编译、字体/emoji 说明）。
启用虚拟网卡需把 `wintun.dll` 放到 exe 同目录并**以管理员身份**运行。

## 配置 `convnet.json`

服务端读取工作目录下的 `convnet.json`（缺失即退出）：

```json
{
  "Server": "your-domain-or-ip",
  "ServerPort": "13903",
  "ServerTurnPort": "13902",
  "ServerTurnUser": "turnuser",
  "ServerTurnPass": "CHANGE_ME",
  "ServerTurnRealm": "your-domain-or-ip"
}
```

管理后台端口/密码可选加 `"AdminPort"`/`"AdminPassword"`（默认 `8099` / `admin`）。
> 注意：信令端口 `13903` 在代码中硬编码，`ServerPort` 字段不改变监听端口。

## 安全

- 账号密码用 **bcrypt** 存储；P2P 流量走 DTLS 加密。
- 中继（服务器转发）默认对每对用户按「好友/同组」授权，可在管理后台按用户禁用/限速。
- 管理后台仅校验密码（用户名任意），**默认密码弱、且监听 0.0.0.0**——生产环境务必改密码并限制在内网/SSH 隧道访问。

## 路线图 / 未完成

- 网络拓扑图（图形化 P2P/中继边着色）——**未实现**。
- ACL 黑白名单与服务器同步（`ACL_SET/GET`）——服务端已有，客户端尚未接入（本地防火墙可先用）。
- 中继帧端到端加密（当前中继转发的是明文帧，经服务器）。

---

历史说明：旧版通过 `autoConnectPeer.txt` 手动连节点、Vue Web 界面（:8094）与 `/api/...` HTTP 接口的用法已由上述
IM 社交图 + Qt 客户端取代。

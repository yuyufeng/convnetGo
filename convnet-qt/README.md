# convnet-qt —— ConvnetGo QQ 风格 IM 客户端（Qt6）

M2 里程碑：登录 + 好友 + 群组 + 富文本/表情聊天，全部经 Go 信令服务器（存储转发）。
P2P/TAP 组网、拓扑、防火墙在后续 M3–M6 加入。

## 依赖

- Qt 6（Widgets + Network）
- CMake ≥ 3.16
- C++17 编译器（MSVC 2019+/MinGW/Clang）
- **libdatachannel**（P2P，M3 起）：CMake 先 `find_package(LibDataChannel)`，找不到才 FetchContent 源码构建。
  **源码构建首次很慢（要编译 libjuice/usrsctp），强烈建议直接装预编译包跳过：**
  - **首选**：`sudo apt install libdatachannel-dev`，然后 `rm -rf build` 重新 cmake —— find_package 命中，零源码编译。
    先查是否有：`apt-cache policy libdatachannel-dev`。
  - 若发行版无此包（如 Ubuntu 20.04/22.04，`apt` 找不到 libdatachannel-dev）：**一次性手动编译安装**，
    之后 app 构建直接命中、不再重编：
    ```bash
    sudo apt install -y git cmake build-essential libssl-dev
    git clone --recursive --shallow-submodules --depth 1 \
      --branch v0.22.4 https://github.com/paullouisageneau/libdatachannel.git
    cd libdatachannel
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_MEDIA=ON -DNO_WEBSOCKET=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON
    cmake --build build -j$(nproc)
    sudo cmake --install build && sudo ldconfig
    ```
    `--shallow-submodules --depth 1` 可避免拉子模块完整历史导致的长时间卡顿；
    `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` 是 CMake 4.x 下绕过老子模块最低版本号报错所必需。
  - 卡在 `Cloning` 是网络问题（挂代理 / 换镜像）；`cmake --build` 有编译输出则只是慢，加 `-j$(nproc)` 并行等几分钟。

## 构建（Windows, MSVC 示例）

```bat
cd convnet-qt
cmake -B build -S . -G "Ninja" -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2019_64"
cmake --build build --config Release
```

或用 Qt Creator 直接打开 `CMakeLists.txt` 构建运行。

> 若本机仅有 Qt5，把 `CMakeLists.txt` 里的 `find_package(Qt6 ...)` 改为
> `find_package(Qt5 ...)`，并把 `Qt6::` 前缀换成 `Qt5::`。

## 构建（Windows 原生, MSVC + vcpkg）

推荐 MSVC + vcpkg（vcpkg 自动提供 libdatachannel 及其 OpenSSL 依赖）：

1. **Visual Studio 2022**（或 Build Tools），勾选“使用 C++ 的桌面开发”（含 MSVC/CMake/Ninja）。
2. **Qt 6**（在线安装器，选 MSVC 2022 64-bit kit）。
3. **vcpkg + libdatachannel**：
   ```bat
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   C:\vcpkg\vcpkg install libdatachannel:x64-windows
   ```
4. 在 **“x64 Native Tools Command Prompt for VS 2022”** 里构建：
   ```bat
   cd convnet-qt
   cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
     -DCMAKE_PREFIX_PATH=C:\Qt\6.7.2\msvc2022_64
   cmake --build build
   ```
5. 部署 Qt DLL：`C:\Qt\6.7.2\msvc2022_64\bin\windeployqt.exe build\convnet-qt.exe`。
6. 虚拟网卡：从 wintun.net 下载 `wintun.dll`（amd64）放到 exe 同目录，并**以管理员运行**。

> MSVC 下源码 UTF-8 中文字面量由 CMake 的 `/utf-8` 处理（已在 CMakeLists 配置）。
> 也可用 Qt Creator 打开 `CMakeLists.txt`，在 CMake 设置里加
> `CMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake` 后构建。

## 运行

1. 先启动服务器（在仓库根目录）：`go run . -s`（监听 :13903）。
2. 运行 `convnet-qt`，登录框选「注册」创建账号（账号+密码+昵称），或「登录」用已有账号。
   服务器地址默认 `127.0.0.1:13903`。
3. 同机开多个**不同身份**实例：身份绑定**账号**（不再是机器 UUID），所以直接开两个实例
   分别登录不同账号即可，**无需 profile**。身份的 userID/PublicID 由服务器按账号分配。
   （QSettings 只缓存"上次登录账号/服务器/伪MAC"，位置：Linux/WSL
   `~/.config/convnet/convnet-qt.conf`；Windows 注册表 `HKCU\Software\convnet\convnet-qt`。）

## 功能（M2）

- **登录 / 注册**：账号 + 密码认证（服务端 bcrypt 哈希）；服务器按账号分配
  userID / PublicID（`md5(account):userID`）；昵称为显示名、可被搜索、登录后以服务器回传为准。
- **好友**：搜索（昵称/ID）、发申请、收到申请弹窗接受/拒绝、好友列表（在线状态）、删除。
- **群组**：创建（≤5，服务端强制）、搜索、申请加入、群主审批（≤10）、群列表。
- **聊天**：单聊 + 群聊，`QTextEdit` 富文本 + emoji 表情，Ctrl+Enter 发送；
  离线消息在对方重连后送达。
- **新消息提示**：收到消息且对应会话窗口未激活时——任务栏/标题闪烁（`QApplication::alert`）
  + 列表项红点未读数 + 背景黄色闪烁；打开或激活该会话即清除未读。
- **查看资料 / 虚拟IP**：好友列表右键，或群聊窗口右侧**成员列表**右键/双击 → 弹出资料
  （昵称、用户ID、虚拟IP、MAC、在线状态、PublicID，可选中复制）。好友项另有发送消息 / 删除好友。
- **群成员列表**：群聊窗口右侧栏显示全部成员（在线状态着色、标注“我”），随加入/退出/在线变化实时刷新。
- **P2P 对接（M3-1）**：与在线好友/组员经 libdatachannel + STUN 自动建立 WebRTC 数据通道；
  资料弹窗的“对接方式”按对端实时显示 **P2P 直连 / 服务器中继 / 连接中**。
  （复用信令 `C_CONNTOWS_PEERCALL`，与遗留 Go/pion 互通。）
- **退出登录**：工具栏「退出登录」→ 断开、清空好友/群/聊天、停网卡、重回登录框。
- **连接失败提示**：连不上服务器时弹窗给出中文原因（拒绝连接/找不到主机/超时…）并可重登。
- **网卡状态**：工具栏「网卡状态」→ 查看虚拟网卡是否启用、接口名、本机虚拟IP；未启用时给出原因
  （缺 wintun.dll / 非管理员 / 非 root）。主窗状态栏也常驻显示"网卡:cvn0(10.110.x)"或"网卡:未启用"。
- **收发流量**：好友/成员资料弹窗显示与该对端的累计**已发送/已接收字节**（P2P + 中继统一计，人类可读单位）。
- **防火墙**：工具栏「防火墙」→ 规则管理（方向 双向/入站/出站、动作 允许/拒绝、对端用户ID(0=任意)、
  协议 TCP/UDP/ICMP/任意、目的端口范围）。自上而下匹配、首条命中生效、无命中默认放行；
  出站(发送前)与入站(写网卡前)各评估一次，规则本地持久化。可用"拒绝 + 对端ID"直接拉黑某人。
- **虚拟网卡互联（M3-2）**：登录后自动创建 L3 虚拟网卡（Linux `/dev/net/tun`，接口名 `cvn0`；
  Windows Wintun），IP=服务器分配的虚拟IP（10.110.x/8）。好友/组员的机器据此**真正互联**，
  可 `ping` 对方虚拟IP、跑 TCP/UDP。数据默认走 **P2P 直连**，失败自动回退**服务器中继**（RELAY_DATA）。
  纯 L3 按目的 IP 路由，**不转发广播、不做 NAT**。

## 运行虚拟网卡（M3-2）

创建/配置 TUN 需要权限，且**两端要能互相成为好友/同组**：

- **Linux/WSL**：以 root 运行客户端（`sudo ./build/convnet-qt`），否则网卡创建失败——
  此时会提示“虚拟网卡启动失败”，但 IM（好友/群/聊天）仍照常可用。
- **Windows**：需管理员权限 + 同目录放 `wintun.dll`（从 wireguard.com/wintun 下载）。
- **测试拓扑（推荐两台独立主机 / 两个 WSL 发行版）**：各跑一个客户端、登录不同账号、互加好友。
  连上后本机会有 `cvn0`（各自 10.110.x）。在 A 上 `ping <B的虚拟IP>` 应通。
  资料弹窗“对接方式”显示 P2P 直连或服务器中继。
  > 同一台机器开两个实例会因两个 TUN 处于同一 netns、同网段而路由冲突，不建议；
  > 要单机测请用两个 network namespace 隔离。

## 托盘与通知

- **最小化到托盘**：最小化或关闭窗口 → 缩到系统托盘后台运行；双击托盘图标或右键「显示主窗口」恢复；右键「退出」才真正退出。
- **新消息提示**：非活动时——提示音（系统 beep）+ 托盘气泡 + 托盘图标红点闪烁 + 任务栏闪烁 + 列表项 `(N)` 闪烁。
- **免打扰**：工具栏/托盘菜单「免打扰」勾选后——**只闪烁、不响铃、不弹气泡**。
- **全屏游戏自动免打扰**：检测到前台为全屏程序（游戏/全屏视频）时自动进入免打扰（Windows，`GetForegroundWindow`+全屏判定，每 3 秒轮询）。

## 虚拟网卡模式：TUN / TAP

工具栏「网卡状态」弹窗里可**切换 TUN/TAP**（本地设置，切换会重启网卡；模式存 QSettings）。

- **TUN（L3，默认）**：收发裸 IP 包，按目的 IP 路由，无 ARP/广播——最干净，满足「默认不转发广播」。
  Windows 用 **Wintun**（`wintun.dll`，免装驱动）；Linux 用 `/dev/net/tun` 的 `IFF_TUN`，接口名 `cvn0`。
- **TAP（L2）**：收发**完整以太网帧**，可承载 **IPX 等非 IP 协议**，并支持**局域网广播发现**（很多老游戏靠广播找对局）。
  Windows 用 **tap-windows6 驱动**（随 OpenVPN 安装，或单独装 tap-windows）；Linux 用 `IFF_TAP`，接口名 `cvntap0`。

**广播语义**（学习式交换，全网状拓扑）：本机产生的广播/组播/未知单播 → **发给所有对端各一份**；
收到对端的帧 → 只写本地网卡、**不再向其它对端转发**（避免洪泛/环路）。单播按学习到的 MAC 直达对端。

**重要**：
- 同一虚拟局域网内**所有节点必须用相同模式**（一端 TAP 一端 TUN 无法互通）。
- TAP 需**管理员**运行；Windows 需预装 **tap-windows6**（未装会提示「未找到 tap-windows6 网卡」，此时可切回 TUN）。
- L2/TAP 下防火墙的**端口/协议规则不适用**（对非 IP 帧无意义），但**按对端「拉黑」仍有效**。

## 常见问题

### 中文 / 表情（emoji）显示为方框（tofu），尤其在 WSL/WSLg

方框 = 当前系统里没有能提供该字形的字体（不是编码问题；编码错会是乱码）。原生 Windows
自带微软雅黑（中文）和 Segoe UI Emoji（彩色表情），一般不会出现；WSL 默认两者都不装，
需手动安装（**表情方框的根因就是没装 emoji 字体，代码无法凭空造字形**）：

```bash
sudo apt-get update
sudo apt-get install -y fonts-noto-cjk fonts-wqy-zenhei fonts-wqy-microhei \
                        fonts-noto-color-emoji
fc-cache -f
```

装好后重启程序即可。`main.cpp` 的 `applyAppFonts()` 会：① 在候选（微软雅黑 / Noto CJK /
文泉驿 等）里挑一个已装的 CJK 字体作主字体；② 在 emoji 候选（Noto Color Emoji /
Segoe UI Emoji 等）里挑一个已装的，用 Qt 6.9+ 的 `QFontDatabase::setApplicationEmojiFontFamilies()`
指定给表情段使用。但系统里至少要各装一个字体它才有得选。

> 用 MSVC 在原生 Windows 编译时，若源码里的中文字面量出问题，给编译器加 `/utf-8`
> （本项目源码均为 UTF-8）。GCC/Clang 默认按 UTF-8 处理，无需额外设置。

## 主界面（单一树形，取代 TAB）

主窗用一个 `QTreeWidget` 展示：顶层「好友 (N)」与「群组 (M)」两个**可折叠分组**。
**群可展开**（点箭头）显示其**成员列表**——每个成员带信号格/头像、昵称、**虚拟IP**、在线状态（同好友样式）。
双击好友→单聊、双击群→群聊、双击成员→与该成员私聊。右键：好友（查看资料/发消息/删除）、
群（进入群聊/退出群组）、成员（查看资料·虚拟IP/私聊）。展开状态在刷新后保留。
未读消息在对应项显示 `(N)` 计数并柔和高亮闪烁。
- **群管理**：创建者=群主。可管理的群显示 `[群主]`/`[管理]` 标记。群右键（群主可转移群主/解散群/退出）、
  成员右键（群主/管理员可踢人；群主可设/取消管理员、转移群主）。**群主有其他成员时不能直接退出**——
  须先转移群主（选继任成员，转移即退出）或解散群。管理员可踢普通成员，但不能踢群主/其他管理员。
- **头像 + 信号格**：好友项左侧 4 格信号强度 + 圆形头像（按昵称取色，首字母居中）。
  信号格数按 P2P 往返延时递减——**每 50ms 少一格**（<50ms=4格；≥3格绿、1-2格黄、0格红；离线/未测灰）；
  每 2 秒按最新 RTT 刷新。群组用圆角方形图标。
- **排序**：好友**在线优先**（`AppModel::friends()` 按在线降序排序），离线靠后；上下线时自动重排+变色。
- **深色主题（参考 Radmin LAN）**：全局 QSS 深底 + 圆角 + 青色强调（`ui/Theme.h`）。顶部有「我」的卡片
  （青色电源头像 + 昵称 + 虚拟IP + 在线/离线徽标）；树为两列，右侧显示对端**虚拟IP**（好友）/成员数（群）。

## 架构

```
src/
  core/    Protocol.h(opcode镜像)  FrameCodec(4字节大端+JSON帧)  SignalingClient(异步TCP)
  model/   Identity(QSettings)  Types  AppModel(状态+opcode分发，单一数据源)
  ui/      LoginDialog  MainWindow  ChatWindow  AddFriendDialog  GroupDialog
```

线程模型：M2 只有控制面，`QTcpSocket` 异步事件驱动，全部在 GUI 线程；
M3 的 P2P/TAP 阻塞循环届时另起 worker 线程，通过 Qt 信号回编组到 GUI。

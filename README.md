# ConvnetGo

ConvnetGo 是一个基于 Go 语言开发的 P2P 网络连接工具，支持 Windows 和 Linux 系统。它允许用户通过 TURN/STUN 服务建立点对点连接，实现安全的网络通信。

## 功能特点

- 支持 Windows、Linux 和 macOS 系统
- 支持多架构：32位(386)和64位(amd64)
- 基于 WebRTC 的 P2P 连接
- TAP 虚拟网卡支持
- 自动重连机制
- Web 界面控制
- 支持 TCP/UDP 端口范围配置
- 支持服务端和客户端模式
- 自动保存连接配置

## 系统要求

### Windows
- 需要安装 TAP 驱动
  - 下载地址：https://openvpn.net/community-downloads/
  - 安装时仅选择 TAP 驱动组件

### Linux
- 需要 root 权限来创建和配置 TAP 设备

## 构建说明

### 本地构建

项目提供了跨平台构建脚本：

- Windows平台运行 `编译全平台.bat`
- 编译后的文件将保存在 `build` 目录下

### 自动发布流程

项目使用GitHub Actions实现自动化构建和发布：

1. 创建新的发布标签：
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```

2. 推送标签后，GitHub Actions将自动：
   - 构建所有平台的二进制文件
   - 创建GitHub Release
   - 上传构建产物

## 配置说明

配置文件 `convnet.json` 包含以下主要参数：

```json
{
  "Server": "服务器地址",
  "UUID": "客户端唯一标识",
  "ClientID": "客户端昵称",
  "ServerPort": "服务器端口",
  "AllowConnect": "是否允许被连接",
  "AllowTcpPortRange": [
    {
      "Start": 10000,
      "End": 20000
    }
  ],
  "AllowUdpPortRange": [
    {
      "Start": 10000,
      "End": 20000
    }
  ]
}
```

## 运行模式

### 服务端模式
```bash
convnetgo -s
```
- 默认监听端口：13903 (TCP)
- TURN 服务端口：13902 (UDP)

### 客户端模式
```bash
convnetgo
```
- 启动后会自动打开 Web 控制界面：http://127.0.0.1:8094
- 自动生成 UUID 和随机昵称（首次运行）
- 支持自动重连服务器

## 连接管理

### 自动连接
- 通过 `autoConnectPeer.txt` 文件管理自动连接的节点
- 格式示例：`CVNID://saiboot.com:13903@[PublicID]`

### 连接状态
- 通过 Web 界面查看连接状态
- 支持手动连接/断开操作
- 实时显示在线用户列表

## API 接口

主要 HTTP API 端点：
- `/api/user/list` - 获取用户列表
- `/api/info` - 获取客户端信息
- `/api/info/update` - 更新用户信息
- `/api/peer/connect` - 连接到指定节点
- `/api/client/connect` - 连接到服务器
- `/api/client/disconnect` - 断开服务器连接

## 技术实现

### 网络通信
- 使用 WebRTC 进行 P2P 通信
- 基于 TAP 设备实现虚拟网络
- 支持 NAT 穿透
- 使用 TCP 长连接保持会话

### 依赖项

- Go 1.22.1 或更高版本
- 主要依赖包：
  - github.com/pion/webrtc/v4
  - github.com/pion/turn/v2
  - github.com/google/gopacket
  - github.com/labstack/echo

## 许可证

[MIT License](LICENSE)
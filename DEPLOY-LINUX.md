# ConvnetGo 服务端 —— Linux 编译 / 运行 / 部署文档

本项目的**服务端与客户端是同一个 Go 程序**（模块 `convnetgo`），通过命令行参数 `-s`
进入服务端模式（见 `convnetgo.go` 的 `main`）。本文档只讲 **Linux 服务端** 的编译、运行与部署。

服务端职责：
- **信令服务器**（TCP `:13903`）：账号注册/登录、好友、群组、聊天存储转发、P2P SDP 盲转发。
- **TURN/中继**（UDP `:13902`）：P2P 打洞失败时的流量中继（可按用户授权/限速）。
- **管理后台**（HTTP `:8099`）：查看用户、开关中继、限速（HTTP Basic Auth）。
- **持久化**：`convnet.db`（bbolt，纯 Go），运行时在**工作目录**自动创建。

---

## 1. 编译产物

已提供预编译的静态二进制（`CGO_ENABLED=0`，无 libc 依赖，可直接在任意同架构 Linux 上跑）：

| 文件 | 架构 | 说明 |
|------|------|------|
| `convnetgo-linux-amd64` | x86-64 | 云主机 / PC 服务器 |
| `convnetgo-linux-arm64` | ARM64  | ARM 云主机 / 树莓派等 |

传到 Linux 后需加可执行权限：

```bash
chmod +x convnetgo-linux-amd64
```

---

## 2. 从源码编译

纯 Go、无 CGO、依赖只有 `go.etcd.io/bbolt` 和 `golang.org/x/crypto`（都在 `go.mod`），
编译很干净。

### 2.1 在 Windows 上交叉编译（本仓库的做法）

```bash
# Git Bash / WSL 里，仓库根目录 H:\convnetgo
GOOS=linux GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o convnetgo-linux-amd64 .
GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o convnetgo-linux-arm64 .
```

PowerShell 版：

```powershell
$env:GOOS="linux"; $env:GOARCH="amd64"; $env:CGO_ENABLED="0"
go build -trimpath -ldflags "-s -w" -o convnetgo-linux-amd64 .
```

> `-ldflags "-s -w"` 去符号表减小体积；`-trimpath` 去掉本机绝对路径。都可省略。

### 2.2 在 Linux 主机上本地编译

```bash
# 需已装 Go 1.22+（本仓库用 go 1.22.1；开发机可用更高版本）
cd /path/to/convnetGo
CGO_ENABLED=0 go build -o convnetgo .
```

---

## 3. 配置文件 `convnet.json`

服务端**必须**能在工作目录读到 `convnet.json`（缺失会直接退出）。服务端实际用到的字段：

| 字段 | 作用 | 服务端是否必需 |
|------|------|----------------|
| `Server` | 本服务器的公网域名/IP，用于 TURN 对外通告的中继地址（会做 DNS 解析） | 建议填对 |
| `ServerTurnPort` | TURN 监听端口（UDP），默认示例 `13902` | 是（须为数字字符串） |
| `ServerTurnUser` / `ServerTurnPass` / `ServerTurnRealm` | TURN 凭据/域 | 建议改成自己的 |
| `AdminPort` | 管理后台端口，缺省 `8099` | 否（有默认） |
| `AdminPassword` | 管理后台密码（Basic Auth，用户名固定 `admin`），缺省 `admin` | **强烈建议改** |

> 其余字段（`UUID` / `ClientID` / `AllowTcpPortRange` 等）是客户端模式用的，服务端不读，
> 但 JSON 必须能正常解析（保留即可）。

**服务端最小示例** `convnet.json`：

```json
{
  "Server": "your-domain-or-ip.example.com",
  "ServerPort": "13903",
  "ServerTurnPort": "13902",
  "ServerTurnUser": "turnuser",
  "ServerTurnPass": "CHANGE_ME_turn_pass",
  "ServerTurnRealm": "your-domain-or-ip.example.com",
  "AdminPort": "8099",
  "AdminPassword": "CHANGE_ME_admin_pass"
}
```

> 注意：信令端口 `13903` 在代码里是硬编码 `0.0.0.0:13903`，`ServerPort` 字段不改变监听端口。

---

## 4. 运行

```bash
# 工作目录必须是 convnet.json 所在目录（convnet.db 也会在此生成）
cd /opt/convnetgo
./convnetgo-linux-amd64 -s
```

启动后监听：

| 端口 | 协议 | 用途 | 是否对公网开放 |
|------|------|------|----------------|
| `13903` | TCP | 信令（客户端连这里） | 是 |
| `13902` | UDP | TURN 中继 | 是 |
| `8099`  | TCP | 管理后台 | **否**（仅内网/本机，见 §7） |

验证是否起来：

```bash
ss -ltnp | grep 13903      # TCP 信令
ss -lunp | grep 13902      # UDP TURN
curl -u admin:你的密码 http://127.0.0.1:8099/api/users   # 管理后台
```

---

## 5. 防火墙 / 安全组

以 `ufw` 为例（云厂商安全组同理）：

```bash
sudo ufw allow 13903/tcp   # 信令
sudo ufw allow 13902/udp   # TURN
# 8099 不要对公网开放！只在本机或运维内网访问（默认 admin/admin，风险高）
```

所有端口都 > 1024，**不需要 root 或 `CAP_NET_BIND_SERVICE`**，用普通用户即可运行。

---

## 6. 用 systemd 部署（推荐）

```bash
# 1) 放置文件
sudo mkdir -p /opt/convnetgo
sudo cp convnetgo-linux-amd64 /opt/convnetgo/
sudo cp convnet.json          /opt/convnetgo/
sudo useradd -r -s /usr/sbin/nologin convnet 2>/dev/null || true
sudo chown -R convnet:convnet /opt/convnetgo
sudo chmod +x /opt/convnetgo/convnetgo-linux-amd64
```

创建 `/etc/systemd/system/convnetgo.service`：

```ini
[Unit]
Description=ConvnetGo IM/P2P Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=convnet
Group=convnet
WorkingDirectory=/opt/convnetgo
ExecStart=/opt/convnetgo/convnetgo-linux-amd64 -s
Restart=on-failure
RestartSec=3
LimitNOFILE=65536
# 加固（可选）
NoNewPrivileges=true
ProtectSystem=full
ReadWritePaths=/opt/convnetgo

[Install]
WantedBy=multi-user.target
```

启用并查看日志：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now convnetgo
sudo systemctl status convnetgo
journalctl -u convnetgo -f      # 实时日志
```

> `WorkingDirectory` 必须指向 `convnet.json` 所在目录——服务端用相对路径读配置、写 `convnet.db`。

---

## 7. 管理后台（`:8099`）

- 地址：`http://<服务器>:8099/`，HTTP Basic Auth。**代码只校验密码、不校验用户名**（用户名随便填，习惯用 `admin`），密码 = `convnet.json` 的 `AdminPassword`（缺省 `admin`）。后台监听 `0.0.0.0:<AdminPort>`。
- 功能：用户列表（在线/虚拟IP）、按用户开关服务器中继、每用户 + 全局中继限速（KB/s）、已中继流量。
- API 示例：`GET /api/users`、`POST /api/user/relay`、`POST /api/settings`。

**安全强烈建议**：
1. 把 `AdminPassword` 改成强密码。
2. **不要**把 `8099` 直接暴露公网。要么只监听/放行内网，要么用 Nginx 反代加 HTTPS + 额外鉴权，
   或用 SSH 隧道访问：`ssh -L 8099:127.0.0.1:8099 user@server` 后本机开 `http://127.0.0.1:8099`。

---

## 8. 数据与备份

- 唯一状态文件：`convnet.db`（bbolt），位于 `WorkingDirectory`。含账号（bcrypt 哈希）、好友、群组、
  黑白名单、离线消息、中继策略。
- 备份（bbolt 是单文件，停服拷贝最稳）：

```bash
sudo systemctl stop convnetgo
sudo cp /opt/convnetgo/convnet.db /backup/convnet.db.$(date +%F)
sudo systemctl start convnetgo
```

---

## 9. 升级

```bash
sudo systemctl stop convnetgo
sudo cp convnetgo-linux-amd64 /opt/convnetgo/     # 覆盖二进制
sudo systemctl start convnetgo
```

`convnet.db` 会保留，账号数据不丢。

---

## 10. Docker 部署（可选）

静态二进制可放进极小镜像。`Dockerfile`：

```dockerfile
FROM alpine:3.20
WORKDIR /app
COPY convnetgo-linux-amd64 /app/convnetgo
EXPOSE 13903/tcp 13902/udp 8099/tcp
ENTRYPOINT ["/app/convnetgo", "-s"]
```

构建与运行（`convnet.json` 和 `convnet.db` 用挂载卷持久化）：

```bash
docker build -t convnetgo-server .
docker run -d --name convnetgo \
  -p 13903:13903/tcp \
  -p 13902:13902/udp \
  -p 127.0.0.1:8099:8099/tcp \
  -v /opt/convnetgo/convnet.json:/app/convnet.json:ro \
  -v /opt/convnetgo/data:/app \
  --restart unless-stopped \
  convnetgo-server
```

> 说明：`-v .../data:/app` 让 `convnet.db` 落到宿主机 `data/` 目录持久化；`8099` 只绑本机。
> 若同时挂 `convnet.json` 只读文件和 `/app` 目录，请把 `convnet.json` 也放进 `data/` 里，避免挂载冲突。

---

## 11. 常见问题排错

| 现象 | 排查 |
|------|------|
| 启动即退出 `Failed to load client configuration` | `convnet.json` 不在工作目录或 JSON 格式错误 |
| `无法将 ServerTurnPort 转换为整数` | `ServerTurnPort` 必须是数字字符串，如 `"13902"` |
| 客户端连不上 | 确认 `13903/tcp` 已放行；`ss -ltnp | grep 13903` 看是否在监听 |
| P2P 老是走中继 | 确认 `13902/udp` 放行、`Server`/TURN 凭据正确；NAT 太严时属正常回退 |
| 管理后台 401 | 只校验密码（用户名随意），密码须等于 `AdminPassword` |
| 权限/端口占用 | 端口都 >1024 无需 root；`ss -ltnp`/`ss -lunp` 查占用 |

---

## 附：与客户端的关系

- 客户端连接的服务器地址 = 客户端 `convnet.json` 的 `Server` + 信令端口 `13903`（或登录界面里填的服务器）。
- 客户端（Qt）编译见 `convnet-qt/README.md` 与 `convnet-qt/build-win.bat`（Windows）。本服务端不依赖客户端。

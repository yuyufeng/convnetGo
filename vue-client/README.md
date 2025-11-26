# ConvnetGo Vue 客户端

这是 ConvnetGo P2P 网络连接工具的 Vue 3 前端客户端，支持浏览器运行和打包成 Windows/macOS/Linux 桌面应用。

## 功能特性

- 客户端配置管理（服务器地址、端口、UUID、昵称等）
- 连接状态实时显示
- 在线用户列表展示
- P2P 连接管理（连接、断开、重连）
- 流量统计（上传/下载字节数）
- 端口范围配置
- 深色/浅色主题切换
- 响应式设计，支持移动端

## 技术栈

- Vue 3
- Vite
- Electron（桌面应用）
- electron-builder（打包工具）

## 开发

### 安装依赖

```bash
cd vue-client
npm install
```

### 启动 Web 开发服务器

```bash
npm run dev
```

开发服务器将在 http://localhost:3000 启动，并自动代理 API 请求到 `http://127.0.0.1:8094`。

### 启动 Electron 开发模式

```bash
npm run electron:dev
```

## 构建

### 构建 Web 版本

```bash
npm run build
```

构建产物将输出到 `dist` 目录。

### 构建 Windows 桌面应用

```bash
npm run electron:build:win
```

这将在 `release` 目录下生成：
- `ConvnetGo Setup x.x.x.exe` - Windows 安装程序 (NSIS)
- `ConvnetGo x.x.x.exe` - 便携版

### 构建 macOS 桌面应用

```bash
npm run electron:build:mac
```

### 构建 Linux 桌面应用

```bash
npm run electron:build:linux
```

### 构建所有平台

```bash
npm run electron:build
```

## 项目结构

```
vue-client/
├── electron/
│   └── main.cjs          # Electron 主进程
├── public/
│   └── favicon.svg       # 应用图标
├── src/
│   ├── api/
│   │   └── index.js      # API 服务层
│   ├── assets/
│   │   └── main.css      # 全局样式
│   ├── components/
│   │   ├── ClientInfo.vue    # 客户端信息组件
│   │   └── UserList.vue      # 用户列表组件
│   ├── App.vue           # 主应用组件
│   └── main.js           # 入口文件
├── index.html
├── package.json
└── vite.config.js
```

## API 接口

| 接口 | 方法 | 描述 |
|------|------|------|
| `/api/info` | GET | 获取客户端信息 |
| `/api/info/update` | PUT | 更新客户端配置 |
| `/api/user/list` | GET | 获取用户列表 |
| `/api/client/connect` | GET | 连接到服务器 |
| `/api/client/disconnect` | GET | 断开服务器连接 |
| `/api/peer/connect` | GET | 连接到指定节点 |
| `/api/peer/removePublicId` | GET | 移除自动连接节点 |
| `/api/client/allowConnect` | GET | 更新连接密码 |

## 使用说明

### Web 模式

1. 确保 ConvnetGo 后端服务正在运行（默认监听 127.0.0.1:8094）
2. 启动 Vue 客户端开发服务器
3. 在浏览器中访问 http://localhost:3000

### 桌面应用模式

1. 确保 ConvnetGo 后端服务正在运行
2. 运行打包好的桌面应用程序
3. 应用会自动连接到本地后端服务 (127.0.0.1:8094)

## 注意事项

- 桌面应用需要 ConvnetGo 后端服务在本地运行
- Windows 打包需要在 Windows 系统上进行（或使用 CI/CD）
- macOS 打包需要在 macOS 系统上进行
- 如需自定义应用图标，请替换 `public/` 目录下的图标文件：
  - Windows: `icon.ico`
  - macOS: `icon.icns`
  - Linux: `icon.png`

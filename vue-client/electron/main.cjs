const { app, BrowserWindow, Menu, shell } = require('electron')
const path = require('path')

// 禁用硬件加速（可选，某些系统上可能需要）
// app.disableHardwareAcceleration()

// 保持窗口对象的全局引用
let mainWindow = null

// 后端服务地址
const BACKEND_URL = 'http://127.0.0.1:8094'

function createWindow() {
  // 创建浏览器窗口
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 800,
    minWidth: 800,
    minHeight: 600,
    title: 'ConvnetGo',
    icon: path.join(__dirname, '../public/favicon.svg'),
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      webSecurity: true
    },
    show: false, // 等待加载完成后再显示
    backgroundColor: '#f5f7fa'
  })

  // 加载应用
  if (process.env.VITE_DEV_SERVER_URL) {
    // 开发模式：加载 Vite 开发服务器
    mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL)
    mainWindow.webContents.openDevTools()
  } else {
    // 生产模式：加载打包后的文件
    mainWindow.loadFile(path.join(__dirname, '../dist/index.html'))
  }

  // 窗口准备好后显示
  mainWindow.once('ready-to-show', () => {
    mainWindow.show()
  })

  // 处理外部链接
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url)
    return { action: 'deny' }
  })

  // 窗口关闭时的处理
  mainWindow.on('closed', () => {
    mainWindow = null
  })

  // 创建菜单
  createMenu()
}

function createMenu() {
  const template = [
    {
      label: '文件',
      submenu: [
        {
          label: '刷新',
          accelerator: 'CmdOrCtrl+R',
          click: () => {
            if (mainWindow) {
              mainWindow.reload()
            }
          }
        },
        { type: 'separator' },
        {
          label: '退出',
          accelerator: process.platform === 'darwin' ? 'Cmd+Q' : 'Alt+F4',
          click: () => {
            app.quit()
          }
        }
      ]
    },
    {
      label: '编辑',
      submenu: [
        { label: '撤销', accelerator: 'CmdOrCtrl+Z', role: 'undo' },
        { label: '重做', accelerator: 'Shift+CmdOrCtrl+Z', role: 'redo' },
        { type: 'separator' },
        { label: '剪切', accelerator: 'CmdOrCtrl+X', role: 'cut' },
        { label: '复制', accelerator: 'CmdOrCtrl+C', role: 'copy' },
        { label: '粘贴', accelerator: 'CmdOrCtrl+V', role: 'paste' },
        { label: '全选', accelerator: 'CmdOrCtrl+A', role: 'selectAll' }
      ]
    },
    {
      label: '视图',
      submenu: [
        {
          label: '开发者工具',
          accelerator: process.platform === 'darwin' ? 'Alt+Cmd+I' : 'Ctrl+Shift+I',
          click: () => {
            if (mainWindow) {
              mainWindow.webContents.toggleDevTools()
            }
          }
        },
        { type: 'separator' },
        { label: '实际大小', accelerator: 'CmdOrCtrl+0', role: 'resetZoom' },
        { label: '放大', accelerator: 'CmdOrCtrl+Plus', role: 'zoomIn' },
        { label: '缩小', accelerator: 'CmdOrCtrl+-', role: 'zoomOut' },
        { type: 'separator' },
        { label: '全屏', accelerator: 'F11', role: 'togglefullscreen' }
      ]
    },
    {
      label: '帮助',
      submenu: [
        {
          label: '关于 ConvnetGo',
          click: () => {
            const { dialog } = require('electron')
            dialog.showMessageBox(mainWindow, {
              type: 'info',
              title: '关于 ConvnetGo',
              message: 'ConvnetGo',
              detail: '版本: 1.0.0\n\nP2P 网络连接工具\n\n基于 WebRTC 技术实现 NAT 穿透和点对点通信。'
            })
          }
        },
        {
          label: '检查后端服务',
          click: async () => {
            const { dialog, net } = require('electron')
            try {
              const request = net.request(BACKEND_URL + '/api/info')
              request.on('response', (response) => {
                if (response.statusCode === 200) {
                  dialog.showMessageBox(mainWindow, {
                    type: 'info',
                    title: '后端服务状态',
                    message: '后端服务正常运行',
                    detail: `服务地址: ${BACKEND_URL}`
                  })
                } else {
                  dialog.showMessageBox(mainWindow, {
                    type: 'warning',
                    title: '后端服务状态',
                    message: '后端服务响应异常',
                    detail: `状态码: ${response.statusCode}`
                  })
                }
              })
              request.on('error', (error) => {
                dialog.showMessageBox(mainWindow, {
                  type: 'error',
                  title: '后端服务状态',
                  message: '无法连接到后端服务',
                  detail: `请确保 ConvnetGo 后端已启动\n\n服务地址: ${BACKEND_URL}\n\n错误: ${error.message}`
                })
              })
              request.end()
            } catch (error) {
              dialog.showMessageBox(mainWindow, {
                type: 'error',
                title: '后端服务状态',
                message: '检查失败',
                detail: error.message
              })
            }
          }
        }
      ]
    }
  ]

  // macOS 需要特殊处理
  if (process.platform === 'darwin') {
    template.unshift({
      label: app.getName(),
      submenu: [
        { label: '关于 ConvnetGo', role: 'about' },
        { type: 'separator' },
        { label: '服务', role: 'services' },
        { type: 'separator' },
        { label: '隐藏 ConvnetGo', accelerator: 'Cmd+H', role: 'hide' },
        { label: '隐藏其他', accelerator: 'Cmd+Alt+H', role: 'hideOthers' },
        { label: '显示全部', role: 'unhide' },
        { type: 'separator' },
        { label: '退出 ConvnetGo', accelerator: 'Cmd+Q', role: 'quit' }
      ]
    })
  }

  const menu = Menu.buildFromTemplate(template)
  Menu.setApplicationMenu(menu)
}

// Electron 初始化完成后创建窗口
app.whenReady().then(() => {
  createWindow()

  // macOS 点击 dock 图标时重新创建窗口
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow()
    }
  })
})

// 所有窗口关闭时退出应用（macOS 除外）
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})

// 安全性：阻止导航到外部 URL
app.on('web-contents-created', (event, contents) => {
  contents.on('will-navigate', (event, navigationUrl) => {
    const parsedUrl = new URL(navigationUrl)
    // 只允许加载本地文件和后端 API
    if (parsedUrl.origin !== 'file://' && !navigationUrl.startsWith(BACKEND_URL)) {
      event.preventDefault()
    }
  })
})

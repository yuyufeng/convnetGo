/**
 * ConvnetGo API 服务层
 */

// 检测是否在 Electron 环境中运行
const isElectron = window.location.protocol === 'file:'

// 在 Electron 中使用完整的后端地址，在浏览器中使用相对路径（通过 Vite 代理）
const API_BASE = isElectron ? 'http://127.0.0.1:8094/api' : '/api'

/**
 * 通用请求方法
 */
async function request(url, options = {}) {
  try {
    const response = await fetch(API_BASE + url, {
      headers: {
        'Content-Type': 'application/json',
        ...options.headers
      },
      ...options
    })

    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`)
    }

    const data = await response.json()
    return data
  } catch (error) {
    console.error('API request failed:', error)
    throw error
  }
}

/**
 * 获取客户端信息
 */
export async function getClientInfo() {
  return request('/info')
}

/**
 * 更新客户端配置
 * @param {Object} config - 配置对象
 */
export async function updateClientInfo(config) {
  return request('/info/update', {
    method: 'PUT',
    body: JSON.stringify(config)
  })
}

/**
 * 获取用户列表
 */
export async function getUserList() {
  return request('/user/list')
}

/**
 * 连接到服务器
 */
export async function connectToServer() {
  return request('/client/connect')
}

/**
 * 断开服务器连接
 */
export async function disconnectFromServer() {
  return request('/client/disconnect')
}

/**
 * 连接到指定的 PublicID 节点
 * @param {string} publicId - 目标节点的 PublicID
 * @param {string} password - 连接密码（可选）
 */
export async function connectToPeer(publicId, password = '') {
  const params = new URLSearchParams({ publicId })
  if (password) {
    params.append('pass', password)
  }
  return request(`/peer/connect?${params.toString()}`)
}

/**
 * 移除自动连接的节点
 * @param {string} publicId - 要移除的节点 PublicID
 */
export async function removePeer(publicId) {
  return request(`/peer/removePublicId?publicId=${encodeURIComponent(publicId)}`)
}

/**
 * 更新连接密码
 * @param {string} password - 新密码
 */
export async function updateAllowConnectPassword(password) {
  return request(`/client/allowConnect?pass=${encodeURIComponent(password)}`)
}

/**
 * 发送聊天消息
 * @param {string} to - 接收者 PublicID，空字符串表示群发
 * @param {string} content - 消息内容
 */
export async function sendChatMessage(to, content) {
  return request('/chat/send', {
    method: 'POST',
    body: JSON.stringify({ to, content })
  })
}

/**
 * 获取聊天历史
 * @param {string} publicId - 指定用户的 PublicID，空字符串表示获取所有
 * @param {number} limit - 返回消息数量限制
 */
export async function getChatHistory(publicId = '', limit = 100) {
  const params = new URLSearchParams()
  if (publicId) params.append('publicId', publicId)
  if (limit) params.append('limit', limit.toString())
  return request(`/chat/history?${params.toString()}`)
}

/**
 * 清空聊天历史
 */
export async function clearChatHistory() {
  return request('/chat/clear')
}

/**
 * 格式化字节数为可读格式
 * @param {number} bytes - 字节数
 * @returns {string} 格式化后的字符串
 */
export function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

/**
 * 生成 UUID
 * @returns {string} UUID 字符串
 */
export function generateUUID() {
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
    const r = Math.random() * 16 | 0
    const v = c === 'x' ? r : (r & 0x3 | 0x8)
    return v.toString(16)
  })
}

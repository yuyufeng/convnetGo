<template>
  <div class="user-list card">
    <div class="card-header">
      <h2>在线用户</h2>
      <div class="header-actions">
        <button @click="showConnectModal = true" class="btn btn-primary btn-small">
          连接节点
        </button>
        <button @click="$emit('refresh')" class="btn btn-small">
          刷新
        </button>
      </div>
    </div>

    <div class="card-body">
      <div v-if="!users || users.length === 0" class="empty-state">
        <div class="empty-icon">
          <svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
            <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/>
            <circle cx="9" cy="7" r="4"/>
            <path d="M23 21v-2a4 4 0 0 0-3-3.87"/>
            <path d="M16 3.13a4 4 0 0 1 0 7.75"/>
          </svg>
        </div>
        <p>暂无在线用户</p>
        <span>连接到服务器后，其他在线节点将显示在此处</span>
      </div>

      <div v-else class="table-container">
        <table>
          <thead>
            <tr>
              <th>用户</th>
              <th>虚拟 IP</th>
              <th>状态</th>
              <th>流量统计</th>
              <th>操作</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="user in users" :key="user.PublicID">
              <td>
                <div class="user-info">
                  <div class="user-avatar" :class="{ online: user.IsOnline }">
                    {{ user.UserNickName ? user.UserNickName[0].toUpperCase() : '?' }}
                  </div>
                  <div class="user-details">
                    <span class="user-name">{{ user.UserNickName || '未知用户' }}</span>
                    <span class="user-id" :title="user.PublicID">
                      {{ truncateId(user.PublicID) }}
                    </span>
                  </div>
                </div>
              </td>
              <td>
                <code class="ip-address">{{ user.CvnIP || '-' }}</code>
              </td>
              <td>
                <div class="status-info">
                  <span
                    class="status-badge"
                    :class="{
                      connected: user.IsConnected,
                      online: user.IsOnline && !user.IsConnected
                    }"
                  >
                    {{ getStatusText(user) }}
                  </span>
                  <span v-if="user.IsConnected" class="connection-type">
                    {{ user.IsRelay ? '中继' : 'P2P' }}
                  </span>
                </div>
              </td>
              <td>
                <div class="traffic-stats">
                  <span class="traffic-item">
                    <span class="traffic-label">↑</span>
                    {{ formatBytes(user.Con_send || 0) }}
                  </span>
                  <span class="traffic-item">
                    <span class="traffic-label">↓</span>
                    {{ formatBytes(user.Con_recv || 0) }}
                  </span>
                </div>
              </td>
              <td>
                <div class="actions">
                  <button
                    v-if="!user.IsConnected && user.IsOnline"
                    @click="connectToPeer(user)"
                    class="btn btn-small btn-success"
                    :disabled="connecting === user.PublicID"
                  >
                    {{ connecting === user.PublicID ? '连接中...' : '连接' }}
                  </button>
                  <button
                    v-if="user.IsConnected"
                    @click="reconnect(user)"
                    class="btn btn-small"
                    :disabled="reconnecting === user.PublicID"
                  >
                    {{ reconnecting === user.PublicID ? '重连中...' : '重连' }}
                  </button>
                  <button
                    @click="removeUser(user)"
                    class="btn btn-small btn-danger"
                    :disabled="removing === user.PublicID"
                    title="从自动连接列表中移除"
                  >
                    删除
                  </button>
                </div>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- 连接节点弹窗 -->
    <div v-if="showConnectModal" class="modal-overlay" @click.self="showConnectModal = false">
      <div class="modal">
        <div class="modal-header">
          <h3>连接到节点</h3>
          <button @click="showConnectModal = false" class="close-btn">&times;</button>
        </div>
        <div class="modal-body">
          <div class="form-group">
            <label>PublicID</label>
            <input
              v-model="connectForm.publicId"
              type="text"
              placeholder="输入目标节点的 PublicID"
            />
          </div>
          <div class="form-group">
            <label>连接密码（可选）</label>
            <input
              v-model="connectForm.password"
              type="password"
              placeholder="如果需要密码，请输入"
            />
          </div>
        </div>
        <div class="modal-footer">
          <button @click="showConnectModal = false" class="btn">取消</button>
          <button
            @click="doConnect"
            class="btn btn-primary"
            :disabled="!connectForm.publicId || modalConnecting"
          >
            {{ modalConnecting ? '连接中...' : '连接' }}
          </button>
        </div>
      </div>
    </div>

    <!-- 消息提示 -->
    <div v-if="message" class="message" :class="messageType">
      {{ message }}
    </div>
  </div>
</template>

<script>
import { connectToPeer as apiConnectToPeer, removePeer, formatBytes } from '../api'

export default {
  name: 'UserList',

  props: {
    users: {
      type: Array,
      default: () => []
    }
  },

  emits: ['refresh'],

  data() {
    return {
      showConnectModal: false,
      connectForm: {
        publicId: '',
        password: ''
      },
      connecting: null,
      reconnecting: null,
      removing: null,
      modalConnecting: false,
      message: '',
      messageType: 'success'
    }
  },

  methods: {
    formatBytes,

    truncateId(id) {
      if (!id) return '-'
      if (id.length <= 16) return id
      return id.substring(0, 8) + '...' + id.substring(id.length - 8)
    },

    getStatusText(user) {
      if (user.IsConnected) return '已连接'
      if (user.IsOnline) return '在线'
      return '离线'
    },

    showMessage(msg, type = 'success') {
      this.message = msg
      this.messageType = type
      setTimeout(() => {
        this.message = ''
      }, 3000)
    },

    async connectToPeer(user) {
      const password = user.Needpass ? prompt('请输入连接密码:') : ''
      if (user.Needpass && password === null) return

      this.connecting = user.PublicID
      try {
        await apiConnectToPeer(user.PublicID, password || '')
        this.showMessage('连接请求已发送')
        setTimeout(() => this.$emit('refresh'), 1000)
      } catch (error) {
        this.showMessage('连接失败: ' + error.message, 'error')
      } finally {
        this.connecting = null
      }
    },

    async reconnect(user) {
      this.reconnecting = user.PublicID
      try {
        await apiConnectToPeer(user.PublicID, '')
        this.showMessage('重连请求已发送')
        setTimeout(() => this.$emit('refresh'), 1000)
      } catch (error) {
        this.showMessage('重连失败: ' + error.message, 'error')
      } finally {
        this.reconnecting = null
      }
    },

    async removeUser(user) {
      if (!confirm(`确定要移除 ${user.UserNickName || user.PublicID} 吗？`)) return

      this.removing = user.PublicID
      try {
        await removePeer(user.PublicID)
        this.showMessage('已移除')
        this.$emit('refresh')
      } catch (error) {
        this.showMessage('移除失败: ' + error.message, 'error')
      } finally {
        this.removing = null
      }
    },

    async doConnect() {
      if (!this.connectForm.publicId) return

      this.modalConnecting = true
      try {
        await apiConnectToPeer(this.connectForm.publicId, this.connectForm.password)
        this.showMessage('连接请求已发送')
        this.showConnectModal = false
        this.connectForm = { publicId: '', password: '' }
        setTimeout(() => this.$emit('refresh'), 1000)
      } catch (error) {
        this.showMessage('连接失败: ' + error.message, 'error')
      } finally {
        this.modalConnecting = false
      }
    }
  }
}
</script>

<style scoped>
.user-list {
  background: var(--card-bg);
  border-radius: 12px;
  box-shadow: var(--shadow);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border-color);
}

.card-header h2 {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
  color: var(--text-primary);
}

.header-actions {
  display: flex;
  gap: 8px;
}

.card-body {
  padding: 0;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 20px;
  color: var(--text-secondary);
}

.empty-icon {
  margin-bottom: 16px;
  opacity: 0.5;
}

.empty-state p {
  margin: 0 0 8px;
  font-size: 16px;
  font-weight: 500;
}

.empty-state span {
  font-size: 14px;
  opacity: 0.8;
}

.table-container {
  overflow-x: auto;
}

table {
  width: 100%;
  border-collapse: collapse;
}

th, td {
  padding: 12px 16px;
  text-align: left;
}

th {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-secondary);
  background: var(--table-header-bg);
  border-bottom: 1px solid var(--border-color);
}

td {
  border-bottom: 1px solid var(--border-color);
}

tr:last-child td {
  border-bottom: none;
}

tr:hover {
  background: var(--hover-bg);
}

.user-info {
  display: flex;
  align-items: center;
  gap: 12px;
}

.user-avatar {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: var(--avatar-bg);
  color: var(--avatar-text);
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 600;
  font-size: 14px;
}

.user-avatar.online {
  background: #28a745;
  color: white;
}

.user-details {
  display: flex;
  flex-direction: column;
}

.user-name {
  font-weight: 500;
  color: var(--text-primary);
}

.user-id {
  font-size: 12px;
  color: var(--text-secondary);
  font-family: monospace;
}

.ip-address {
  padding: 4px 8px;
  background: var(--code-bg);
  border-radius: 4px;
  font-size: 13px;
  color: var(--text-primary);
}

.status-info {
  display: flex;
  align-items: center;
  gap: 8px;
}

.status-badge {
  padding: 4px 10px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 500;
  background: var(--badge-offline-bg);
  color: var(--badge-offline-text);
}

.status-badge.online {
  background: #e8f5e9;
  color: #2e7d32;
}

.status-badge.connected {
  background: #28a745;
  color: white;
}

.connection-type {
  font-size: 12px;
  color: var(--text-secondary);
  padding: 2px 6px;
  background: var(--code-bg);
  border-radius: 4px;
}

.traffic-stats {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.traffic-item {
  font-size: 13px;
  color: var(--text-secondary);
}

.traffic-label {
  color: var(--text-primary);
  margin-right: 4px;
}

.actions {
  display: flex;
  gap: 8px;
}

.btn {
  padding: 8px 14px;
  border: none;
  border-radius: 6px;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  background: var(--btn-bg);
  color: var(--text-primary);
  transition: all 0.2s;
}

.btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.btn-small {
  padding: 6px 12px;
  font-size: 12px;
}

.btn-primary {
  background: var(--primary-color);
  color: white;
}

.btn-primary:hover:not(:disabled) {
  background: var(--primary-hover);
}

.btn-success {
  background: #28a745;
  color: white;
}

.btn-success:hover:not(:disabled) {
  background: #218838;
}

.btn-danger {
  background: #dc3545;
  color: white;
}

.btn-danger:hover:not(:disabled) {
  background: #c82333;
}

/* Modal styles */
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.modal {
  background: var(--card-bg);
  border-radius: 12px;
  width: 100%;
  max-width: 400px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border-color);
}

.modal-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
}

.close-btn {
  background: none;
  border: none;
  font-size: 24px;
  cursor: pointer;
  color: var(--text-secondary);
  line-height: 1;
}

.modal-body {
  padding: 20px;
}

.modal-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  padding: 16px 20px;
  border-top: 1px solid var(--border-color);
}

.form-group {
  margin-bottom: 16px;
}

.form-group:last-child {
  margin-bottom: 0;
}

.form-group label {
  display: block;
  margin-bottom: 6px;
  font-size: 14px;
  font-weight: 500;
  color: var(--text-secondary);
}

.form-group input {
  width: 100%;
  padding: 10px 12px;
  border: 1px solid var(--border-color);
  border-radius: 8px;
  font-size: 14px;
  background: var(--input-bg);
  color: var(--text-primary);
}

.form-group input:focus {
  outline: none;
  border-color: var(--primary-color);
}

.message {
  padding: 12px 20px;
  font-size: 14px;
}

.message.success {
  background: #d4edda;
  color: #155724;
}

.message.error {
  background: #f8d7da;
  color: #721c24;
}
</style>

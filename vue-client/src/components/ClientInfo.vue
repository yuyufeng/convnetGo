<template>
  <div class="client-info card">
    <div class="card-header">
      <h2>客户端信息</h2>
      <div class="connection-status" :class="{ connected: clientInfo.IsConnected }">
        <span class="status-dot"></span>
        {{ clientInfo.IsConnected ? '已连接' : '未连接' }}
      </div>
    </div>

    <div class="card-body">
      <!-- 服务器配置 -->
      <div class="form-group">
        <label>服务器地址</label>
        <div class="input-group">
          <input
            v-model="form.Server"
            type="text"
            placeholder="服务器地址"
            :disabled="clientInfo.IsConnected"
          />
          <span class="separator">:</span>
          <input
            v-model="form.ServerPort"
            type="text"
            class="port-input"
            placeholder="端口"
            :disabled="clientInfo.IsConnected"
          />
        </div>
      </div>

      <!-- TURN 服务配置 -->
      <div class="form-group">
        <label>TURN 服务端口</label>
        <input
          v-model="form.ServerTurnPort"
          type="text"
          :disabled="clientInfo.IsConnected"
        />
      </div>

      <!-- UUID -->
      <div class="form-group">
        <label>UUID (私有身份)</label>
        <div class="input-with-action">
          <input
            v-model="form.UUID"
            type="text"
            readonly
            class="readonly-input"
          />
          <button @click="regenerateUUID" class="btn btn-small" :disabled="clientInfo.IsConnected">
            重新生成
          </button>
        </div>
      </div>

      <!-- 客户端昵称 -->
      <div class="form-group">
        <label>客户端昵称</label>
        <input
          v-model="form.ClientID"
          type="text"
          placeholder="输入您的昵称"
        />
      </div>

      <!-- 公开身份 -->
      <div class="form-group" v-if="clientInfo.PublicID">
        <label>公开身份 (PublicID)</label>
        <div class="input-with-action">
          <input
            :value="clientInfo.PublicID"
            type="text"
            readonly
            class="readonly-input"
          />
          <button @click="copyPublicID" class="btn btn-small">
            复制
          </button>
        </div>
      </div>

      <!-- 虚拟IP -->
      <div class="form-group" v-if="clientInfo.MyCvnIP">
        <label>虚拟 IP</label>
        <input
          :value="clientInfo.MyCvnIP"
          type="text"
          readonly
          class="readonly-input"
        />
      </div>

      <!-- 自动连接密码 -->
      <div class="form-group">
        <label>自动连接密码</label>
        <div class="input-with-action">
          <input
            v-model="form.AutoConnectPassword"
            type="text"
            placeholder="设置连接密码"
          />
          <button @click="updatePassword" class="btn btn-small">
            更新
          </button>
        </div>
      </div>

      <!-- 端口范围配置 -->
      <div class="form-group">
        <label>TCP 端口范围</label>
        <div class="port-range">
          <input
            v-model.number="tcpPortStart"
            type="number"
            placeholder="起始端口"
          />
          <span>-</span>
          <input
            v-model.number="tcpPortEnd"
            type="number"
            placeholder="结束端口"
          />
        </div>
      </div>

      <div class="form-group">
        <label>UDP 端口范围</label>
        <div class="port-range">
          <input
            v-model.number="udpPortStart"
            type="number"
            placeholder="起始端口"
          />
          <span>-</span>
          <input
            v-model.number="udpPortEnd"
            type="number"
            placeholder="结束端口"
          />
        </div>
      </div>
    </div>

    <div class="card-footer">
      <button @click="saveConfig" class="btn btn-primary" :disabled="saving">
        {{ saving ? '保存中...' : '保存配置' }}
      </button>
      <button
        v-if="!clientInfo.IsConnected"
        @click="connect"
        class="btn btn-success"
        :disabled="connecting"
      >
        {{ connecting ? '连接中...' : '连接服务器' }}
      </button>
      <button
        v-else
        @click="disconnect"
        class="btn btn-danger"
        :disabled="disconnecting"
      >
        {{ disconnecting ? '断开中...' : '断开连接' }}
      </button>
    </div>

    <!-- 消息提示 -->
    <div v-if="message" class="message" :class="messageType">
      {{ message }}
    </div>
  </div>
</template>

<script>
import {
  getClientInfo,
  updateClientInfo,
  connectToServer,
  disconnectFromServer,
  updateAllowConnectPassword,
  generateUUID
} from '../api'

export default {
  name: 'ClientInfo',

  props: {
    clientInfo: {
      type: Object,
      default: () => ({})
    }
  },

  emits: ['refresh'],

  data() {
    return {
      form: {
        Server: '',
        ServerPort: '',
        ServerTurnPort: '',
        UUID: '',
        ClientID: '',
        AutoConnectPassword: ''
      },
      tcpPortStart: 10000,
      tcpPortEnd: 20000,
      udpPortStart: 10000,
      udpPortEnd: 20000,
      saving: false,
      connecting: false,
      disconnecting: false,
      message: '',
      messageType: 'success'
    }
  },

  watch: {
    clientInfo: {
      handler(newVal) {
        if (newVal) {
          this.form = {
            Server: newVal.Server || '',
            ServerPort: newVal.ServerPort || '',
            ServerTurnPort: newVal.ServerTurnPort || '',
            UUID: newVal.UUID || '',
            ClientID: newVal.ClientID || '',
            AutoConnectPassword: newVal.AutoConnectPassword || ''
          }

          if (newVal.AllowTcpPortRange && newVal.AllowTcpPortRange.length > 0) {
            this.tcpPortStart = newVal.AllowTcpPortRange[0].Start || 10000
            this.tcpPortEnd = newVal.AllowTcpPortRange[0].End || 20000
          }

          if (newVal.AllowUdpPortRange && newVal.AllowUdpPortRange.length > 0) {
            this.udpPortStart = newVal.AllowUdpPortRange[0].Start || 10000
            this.udpPortEnd = newVal.AllowUdpPortRange[0].End || 20000
          }
        }
      },
      immediate: true,
      deep: true
    }
  },

  methods: {
    showMessage(msg, type = 'success') {
      this.message = msg
      this.messageType = type
      setTimeout(() => {
        this.message = ''
      }, 3000)
    },

    async saveConfig() {
      this.saving = true
      try {
        const config = {
          ...this.form,
          AllowTcpPortRange: [{ Start: this.tcpPortStart, End: this.tcpPortEnd }],
          AllowUdpPortRange: [{ Start: this.udpPortStart, End: this.udpPortEnd }]
        }
        await updateClientInfo(config)
        this.showMessage('配置保存成功')
        this.$emit('refresh')
      } catch (error) {
        this.showMessage('保存失败: ' + error.message, 'error')
      } finally {
        this.saving = false
      }
    },

    async connect() {
      this.connecting = true
      try {
        await connectToServer()
        this.showMessage('连接请求已发送')
        setTimeout(() => this.$emit('refresh'), 1000)
      } catch (error) {
        this.showMessage('连接失败: ' + error.message, 'error')
      } finally {
        this.connecting = false
      }
    },

    async disconnect() {
      this.disconnecting = true
      try {
        await disconnectFromServer()
        this.showMessage('已断开连接')
        this.$emit('refresh')
      } catch (error) {
        this.showMessage('断开失败: ' + error.message, 'error')
      } finally {
        this.disconnecting = false
      }
    },

    regenerateUUID() {
      this.form.UUID = generateUUID()
      this.showMessage('UUID 已重新生成，请保存配置')
    },

    copyPublicID() {
      if (this.clientInfo.PublicID) {
        navigator.clipboard.writeText(this.clientInfo.PublicID)
          .then(() => this.showMessage('PublicID 已复制到剪贴板'))
          .catch(() => this.showMessage('复制失败', 'error'))
      }
    },

    async updatePassword() {
      try {
        await updateAllowConnectPassword(this.form.AutoConnectPassword)
        this.showMessage('密码更新成功')
      } catch (error) {
        this.showMessage('密码更新失败: ' + error.message, 'error')
      }
    }
  }
}
</script>

<style scoped>
.client-info {
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

.connection-status {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  color: var(--text-secondary);
}

.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #dc3545;
}

.connection-status.connected .status-dot {
  background: #28a745;
}

.card-body {
  padding: 20px;
}

.form-group {
  margin-bottom: 16px;
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
  transition: border-color 0.2s, box-shadow 0.2s;
}

.form-group input:focus {
  outline: none;
  border-color: var(--primary-color);
  box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1);
}

.form-group input:disabled {
  background: var(--disabled-bg);
  cursor: not-allowed;
}

.readonly-input {
  background: var(--disabled-bg) !important;
}

.input-group {
  display: flex;
  align-items: center;
  gap: 8px;
}

.input-group input {
  flex: 1;
}

.input-group .separator {
  color: var(--text-secondary);
}

.input-group .port-input {
  width: 100px;
  flex: none;
}

.input-with-action {
  display: flex;
  gap: 8px;
}

.input-with-action input {
  flex: 1;
}

.port-range {
  display: flex;
  align-items: center;
  gap: 12px;
}

.port-range input {
  flex: 1;
}

.port-range span {
  color: var(--text-secondary);
}

.card-footer {
  display: flex;
  gap: 12px;
  padding: 16px 20px;
  border-top: 1px solid var(--border-color);
}

.btn {
  padding: 10px 20px;
  border: none;
  border-radius: 8px;
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.btn-small {
  padding: 8px 14px;
  font-size: 13px;
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

.message {
  padding: 12px 20px;
  font-size: 14px;
  border-radius: 0 0 12px 12px;
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

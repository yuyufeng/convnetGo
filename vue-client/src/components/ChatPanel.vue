<template>
  <div class="chat-panel card">
    <div class="card-header">
      <h2>
        <span v-if="selectedUser">与 {{ selectedUser.UserNickName || '未知用户' }} 聊天</span>
        <span v-else>群聊</span>
      </h2>
      <div class="header-actions">
        <select v-model="chatTarget" class="chat-target-select" @change="onTargetChange">
          <option value="">群聊（所有人）</option>
          <option
            v-for="user in connectedUsers"
            :key="user.PublicID"
            :value="user.PublicID"
          >
            {{ user.UserNickName || user.PublicID }}
          </option>
        </select>
        <button @click="clearHistory" class="btn btn-small" title="清空聊天记录">
          清空
        </button>
      </div>
    </div>

    <div class="chat-messages" ref="messagesContainer">
      <div v-if="messages.length === 0" class="empty-chat">
        <p>暂无消息</p>
        <span>发送第一条消息开始聊天</span>
      </div>
      <div
        v-for="msg in messages"
        :key="msg.id"
        class="message"
        :class="{ 'message-me': msg.isMe, 'message-other': !msg.isMe }"
      >
        <div class="message-header">
          <span class="message-sender">{{ msg.isMe ? '我' : (msg.fromName || '未知') }}</span>
          <span class="message-time">{{ formatTime(msg.timestamp) }}</span>
        </div>
        <div class="message-content">{{ msg.content }}</div>
      </div>
    </div>

    <div class="chat-input">
      <input
        v-model="inputMessage"
        type="text"
        placeholder="输入消息..."
        @keyup.enter="sendMessage"
        :disabled="sending"
      />
      <button
        @click="sendMessage"
        class="btn btn-primary"
        :disabled="!inputMessage.trim() || sending"
      >
        {{ sending ? '发送中...' : '发送' }}
      </button>
    </div>

    <!-- 消息提示 -->
    <div v-if="error" class="message-error">
      {{ error }}
    </div>
  </div>
</template>

<script>
import { sendChatMessage, getChatHistory, clearChatHistory } from '../api'

export default {
  name: 'ChatPanel',

  props: {
    users: {
      type: Array,
      default: () => []
    }
  },

  data() {
    return {
      chatTarget: '', // 空字符串表示群聊
      messages: [],
      inputMessage: '',
      sending: false,
      error: '',
      refreshTimer: null
    }
  },

  computed: {
    connectedUsers() {
      return this.users.filter(u => u.IsConnected)
    },
    selectedUser() {
      if (!this.chatTarget) return null
      return this.users.find(u => u.PublicID === this.chatTarget)
    }
  },

  mounted() {
    this.loadHistory()
    // 定时刷新消息
    this.refreshTimer = setInterval(() => {
      this.loadHistory()
    }, 3000)
  },

  beforeUnmount() {
    if (this.refreshTimer) {
      clearInterval(this.refreshTimer)
    }
  },

  methods: {
    async loadHistory() {
      try {
        const data = await getChatHistory(this.chatTarget, 100)
        if (data.messages) {
          this.messages = data.messages
          this.$nextTick(() => {
            this.scrollToBottom()
          })
        }
      } catch (err) {
        console.error('Failed to load chat history:', err)
      }
    },

    async sendMessage() {
      if (!this.inputMessage.trim()) return

      this.sending = true
      this.error = ''

      try {
        await sendChatMessage(this.chatTarget, this.inputMessage.trim())
        this.inputMessage = ''
        // 立即刷新消息列表
        await this.loadHistory()
      } catch (err) {
        this.error = '发送失败: ' + err.message
        setTimeout(() => {
          this.error = ''
        }, 3000)
      } finally {
        this.sending = false
      }
    },

    async clearHistory() {
      if (!confirm('确定要清空聊天记录吗？')) return

      try {
        await clearChatHistory()
        this.messages = []
      } catch (err) {
        this.error = '清空失败: ' + err.message
      }
    },

    onTargetChange() {
      this.loadHistory()
    },

    formatTime(timestamp) {
      const date = new Date(timestamp)
      const now = new Date()
      const isToday = date.toDateString() === now.toDateString()

      if (isToday) {
        return date.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })
      }
      return date.toLocaleString('zh-CN', {
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit'
      })
    },

    scrollToBottom() {
      const container = this.$refs.messagesContainer
      if (container) {
        container.scrollTop = container.scrollHeight
      }
    }
  }
}
</script>

<style scoped>
.chat-panel {
  display: flex;
  flex-direction: column;
  height: 500px;
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
  flex-shrink: 0;
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
  align-items: center;
}

.chat-target-select {
  padding: 6px 10px;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  font-size: 13px;
  background: var(--input-bg);
  color: var(--text-primary);
  cursor: pointer;
}

.chat-target-select:focus {
  outline: none;
  border-color: var(--primary-color);
}

.chat-messages {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.empty-chat {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-secondary);
}

.empty-chat p {
  margin: 0 0 8px;
  font-size: 16px;
}

.empty-chat span {
  font-size: 14px;
  opacity: 0.7;
}

.message {
  max-width: 80%;
  padding: 10px 14px;
  border-radius: 12px;
  word-break: break-word;
}

.message-me {
  align-self: flex-end;
  background: var(--primary-color);
  color: white;
}

.message-other {
  align-self: flex-start;
  background: var(--code-bg);
  color: var(--text-primary);
}

.message-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
  font-size: 12px;
  opacity: 0.8;
}

.message-sender {
  font-weight: 500;
}

.message-time {
  margin-left: 8px;
}

.message-content {
  font-size: 14px;
  line-height: 1.5;
}

.chat-input {
  display: flex;
  gap: 8px;
  padding: 16px;
  border-top: 1px solid var(--border-color);
  flex-shrink: 0;
}

.chat-input input {
  flex: 1;
  padding: 10px 14px;
  border: 1px solid var(--border-color);
  border-radius: 8px;
  font-size: 14px;
  background: var(--input-bg);
  color: var(--text-primary);
}

.chat-input input:focus {
  outline: none;
  border-color: var(--primary-color);
}

.chat-input input:disabled {
  background: var(--disabled-bg);
  cursor: not-allowed;
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
  padding: 6px 12px;
  font-size: 13px;
}

.btn-primary {
  background: var(--primary-color);
  color: white;
}

.btn-primary:hover:not(:disabled) {
  background: var(--primary-hover);
}

.message-error {
  padding: 10px 16px;
  background: #f8d7da;
  color: #721c24;
  font-size: 14px;
  border-radius: 0 0 12px 12px;
}
</style>

<template>
  <div id="app" :class="{ 'dark-mode': isDarkMode }">
    <header class="app-header">
      <div class="header-content">
        <div class="logo">
          <svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="10"/>
            <circle cx="12" cy="12" r="4"/>
            <line x1="21.17" y1="8" x2="12" y2="8"/>
            <line x1="3.95" y1="6.06" x2="8.54" y2="14"/>
            <line x1="10.88" y1="21.94" x2="15.46" y2="14"/>
          </svg>
          <h1>ConvnetGo</h1>
        </div>
        <div class="header-actions">
          <span class="version">v1.0.0</span>
          <button @click="toggleDarkMode" class="theme-toggle" :title="isDarkMode ? '切换到浅色模式' : '切换到深色模式'">
            <svg v-if="isDarkMode" xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <circle cx="12" cy="12" r="5"/>
              <line x1="12" y1="1" x2="12" y2="3"/>
              <line x1="12" y1="21" x2="12" y2="23"/>
              <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/>
              <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/>
              <line x1="1" y1="12" x2="3" y2="12"/>
              <line x1="21" y1="12" x2="23" y2="12"/>
              <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/>
              <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>
            </svg>
            <svg v-else xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>
            </svg>
          </button>
        </div>
      </div>
    </header>

    <main class="app-main">
      <div class="container">
        <div class="grid">
          <div class="grid-sidebar">
            <ClientInfo
              :client-info="clientInfo"
              @refresh="fetchClientInfo"
            />
          </div>
          <div class="grid-content">
            <UserList
              :users="userList"
              @refresh="fetchUserList"
            />
            <ChatPanel
              :users="userList"
              class="chat-section"
            />
          </div>
        </div>
      </div>
    </main>

    <footer class="app-footer">
      <p>ConvnetGo - P2P 网络连接工具</p>
    </footer>
  </div>
</template>

<script>
import ClientInfo from './components/ClientInfo.vue'
import UserList from './components/UserList.vue'
import ChatPanel from './components/ChatPanel.vue'
import { getClientInfo, getUserList } from './api'

export default {
  name: 'App',

  components: {
    ClientInfo,
    UserList,
    ChatPanel
  },

  data() {
    return {
      clientInfo: {},
      userList: [],
      isDarkMode: false,
      refreshTimer: null
    }
  },

  mounted() {
    // 检查系统主题偏好
    this.isDarkMode = window.matchMedia('(prefers-color-scheme: dark)').matches

    // 从本地存储加载主题设置
    const savedTheme = localStorage.getItem('theme')
    if (savedTheme) {
      this.isDarkMode = savedTheme === 'dark'
    }

    // 初始加载数据
    this.fetchClientInfo()
    this.fetchUserList()

    // 设置定时刷新用户列表
    this.refreshTimer = setInterval(() => {
      this.fetchUserList()
    }, 5000)
  },

  beforeUnmount() {
    if (this.refreshTimer) {
      clearInterval(this.refreshTimer)
    }
  },

  methods: {
    async fetchClientInfo() {
      try {
        this.clientInfo = await getClientInfo()
      } catch (error) {
        console.error('Failed to fetch client info:', error)
      }
    },

    async fetchUserList() {
      try {
        const data = await getUserList()
        this.userList = data.userList || []
      } catch (error) {
        console.error('Failed to fetch user list:', error)
      }
    },

    toggleDarkMode() {
      this.isDarkMode = !this.isDarkMode
      localStorage.setItem('theme', this.isDarkMode ? 'dark' : 'light')
    }
  }
}
</script>

<style>
#app {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: var(--bg-primary);
  color: var(--text-primary);
}

.app-header {
  background: var(--header-bg);
  border-bottom: 1px solid var(--border-color);
  position: sticky;
  top: 0;
  z-index: 100;
}

.header-content {
  max-width: 1400px;
  margin: 0 auto;
  padding: 16px 24px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.logo {
  display: flex;
  align-items: center;
  gap: 12px;
  color: var(--primary-color);
}

.logo h1 {
  margin: 0;
  font-size: 22px;
  font-weight: 700;
  color: var(--text-primary);
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 16px;
}

.version {
  font-size: 13px;
  color: var(--text-secondary);
  padding: 4px 10px;
  background: var(--badge-bg);
  border-radius: 12px;
}

.theme-toggle {
  background: none;
  border: none;
  padding: 8px;
  cursor: pointer;
  color: var(--text-secondary);
  border-radius: 8px;
  transition: all 0.2s;
}

.theme-toggle:hover {
  background: var(--hover-bg);
  color: var(--text-primary);
}

.app-main {
  flex: 1;
  padding: 24px 0;
}

.container {
  max-width: 1400px;
  margin: 0 auto;
  padding: 0 24px;
}

.grid {
  display: grid;
  grid-template-columns: 380px 1fr;
  gap: 24px;
}

.grid-sidebar {
  position: sticky;
  top: 90px;
  height: fit-content;
}

.grid-content {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.chat-section {
  margin-top: 0;
}

.app-footer {
  text-align: center;
  padding: 20px;
  border-top: 1px solid var(--border-color);
}

.app-footer p {
  margin: 0;
  font-size: 13px;
  color: var(--text-secondary);
}

/* Responsive design */
@media (max-width: 1024px) {
  .grid {
    grid-template-columns: 1fr;
  }

  .grid-sidebar {
    position: static;
  }
}

@media (max-width: 768px) {
  .container {
    padding: 0 16px;
  }

  .header-content {
    padding: 12px 16px;
  }

  .logo h1 {
    font-size: 18px;
  }

  .app-main {
    padding: 16px 0;
  }
}
</style>

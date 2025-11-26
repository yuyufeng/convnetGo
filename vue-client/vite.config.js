import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  // 使用相对路径，以便 Electron 可以正确加载资源
  base: './',
  server: {
    port: 3002,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8094',
        changeOrigin: true
      }
    }
  },
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    // 确保生成正确的资源路径
    rollupOptions: {
      output: {
        manualChunks: undefined
      }
    }
  }
})

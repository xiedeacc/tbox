import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vite'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [vue()],
  server: {
    host: '0.0.0.0',
    proxy: {
      '/login': {
        target: 'http://127.0.0.1:10003',
        changeOrigin: true,
        // rewrite: (path) => path.replace(/^\/login/, '/login'),
      },
      '/server': {
        target: 'http://127.0.0.1:10003',
        changeOrigin: true,
      },
      '/file': {
        target: 'http://127.0.0.1:10003',
        changeOrigin: true,
      },
      '/file_json': {
        target: 'http://127.0.0.1:10003',
        changeOrigin: true,
      },
    },
  },
})

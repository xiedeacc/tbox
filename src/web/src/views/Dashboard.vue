<template>
  <div class="dashboard-container">
    <div class="dashboard-grid">
      <!-- IP Address Card -->
      <div class="card info-card" v-if="ipAddrs">
        <div class="card-body">
          <pre class="json-display">{{ ipAddrs }}</pre>
        </div>
        <div class="card-footer">
          <button @click="fetchServerData" class="btn-refresh" :disabled="loading">
            <svg class="btn-icon" :class="{ spinning: loading }" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <polyline points="23 4 23 10 17 10"/>
              <polyline points="1 20 1 14 7 14"/>
              <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/>
            </svg>
            Refresh
          </button>
        </div>
      </div>

      <!-- Loading State -->
      <div class="card loading-card" v-if="loading && !ipAddrs">
        <div class="loading-content">
          <div class="loading-spinner-large">
            <svg viewBox="0 0 24 24">
              <circle class="spinner-circle" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="3" fill="none"/>
            </svg>
          </div>
          <p>Loading server data...</p>
        </div>
      </div>

      <!-- Error State -->
      <div class="card error-card" v-if="error">
        <div class="error-content">
          <div class="error-icon-large">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <circle cx="12" cy="12" r="10"/>
              <line x1="12" y1="8" x2="12" y2="12"/>
              <line x1="12" y1="16" x2="12.01" y2="16"/>
            </svg>
          </div>
          <h3>Error Loading Data</h3>
          <p>{{ error }}</p>
          <button @click="fetchServerData" class="btn-retry">
            Try Again
          </button>
        </div>
      </div>

      <!-- Stats Cards -->
      <div class="card stat-card">
        <div class="stat-icon success">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>
          </svg>
        </div>
        <div class="stat-content">
          <p class="stat-label">Connection Status</p>
          <p class="stat-value">{{ connectionStatus }}</p>
        </div>
      </div>

      <div class="card stat-card">
        <div class="stat-icon info">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <circle cx="12" cy="12" r="10"/>
            <polyline points="12 6 12 12 16 14"/>
          </svg>
        </div>
        <div class="stat-content">
          <p class="stat-label">Last Updated</p>
          <p class="stat-value">{{ lastUpdated }}</p>
        </div>
      </div>

      <div class="card stat-card">
        <div class="stat-icon warning">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/>
            <polyline points="3.27 6.96 12 12.01 20.73 6.96"/>
            <line x1="12" y1="22.08" x2="12" y2="12"/>
          </svg>
        </div>
        <div class="stat-content">
          <p class="stat-label">Data Size</p>
          <p class="stat-value">{{ dataSize }}</p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue';
import { v4 as uuidv4 } from 'uuid';

const user = ref(localStorage.getItem('user'));
const token = ref(localStorage.getItem('token'));
const ipAddrs = ref(null);
const error = ref(null);
const loading = ref(false);
const lastUpdatedTime = ref(null);

const generateRequestId = () => uuidv4();

const connectionStatus = computed(() => {
  if (loading.value) return 'Connecting...';
  if (error.value) return 'Disconnected';
  if (ipAddrs.value) return 'Connected';
  return 'Unknown';
});

const lastUpdated = computed(() => {
  if (!lastUpdatedTime.value) return 'Never';
  const now = new Date();
  const diff = Math.floor((now - lastUpdatedTime.value) / 1000);
  if (diff < 60) return 'Just now';
  if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
  return `${Math.floor(diff / 3600)}h ago`;
});

const dataSize = computed(() => {
  if (!ipAddrs.value) return 'N/A';
  const bytes = new Blob([ipAddrs.value]).size;
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(2)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
});

const fetchServerData = async () => {
  loading.value = true;
  error.value = null;
  
  try {
    const response = await fetch('/server', { method: 'GET' });

    if (response.ok) {
      const data = await response.json();
      ipAddrs.value = JSON.stringify(data, null, 2);
      lastUpdatedTime.value = new Date();
    } else {
      error.value = 'Failed to fetch server data. Please try again.';
    }
  } catch (err) {
    error.value = 'Unable to connect to server. Please check your connection.';
  } finally {
    loading.value = false;
  }
};

onMounted(() => {
  fetchServerData();
});
</script>

<style scoped>
.dashboard-container {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  width: 100vw;
  height: 100vh;
  padding: 5rem 2rem 2rem;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  overflow-y: auto;
  animation: fadeIn 0.5s ease-out;
}

.dashboard-container::before {
  content: '';
  position: fixed;
  top: -50%;
  left: -50%;
  width: 200%;
  height: 200%;
  background: radial-gradient(circle, rgba(255,255,255,0.1) 1px, transparent 1px);
  background-size: 50px 50px;
  animation: moveBackground 20s linear infinite;
  pointer-events: none;
  z-index: 0;
}

@keyframes moveBackground {
  0% { transform: translate(0, 0); }
  100% { transform: translate(50px, 50px); }
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}

.dashboard-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  gap: 0.75rem;
  max-width: 1200px;
  margin: 0 auto;
  position: relative;
  z-index: 1;
}

.info-card {
  grid-column: 1 / -1;
}

.card-body {
  margin-bottom: 1rem;
}

.json-display {
  background: rgba(255, 255, 255, 0.95);
  padding: 1.5rem;
  border-radius: var(--border-radius-sm);
  font-family: 'Courier New', monospace;
  font-size: 0.9rem;
  color: var(--text-primary);
  white-space: pre-wrap;
  word-wrap: break-word;
  border: 1px solid rgba(255, 255, 255, 0.3);
  line-height: 1.6;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

.card-footer {
  display: flex;
  justify-content: flex-end;
  padding-top: 1rem;
  border-top: 1px solid var(--border-color);
}

.btn-refresh {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.625rem 1.25rem;
}

.btn-icon {
  width: 18px;
  height: 18px;
}

.btn-icon.spinning {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.loading-card,
.error-card {
  grid-column: 1 / -1;
  min-height: 300px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.loading-content,
.error-content {
  text-align: center;
}

.loading-spinner-large {
  width: 64px;
  height: 64px;
  margin: 0 auto 1.5rem;
}

.loading-spinner-large svg {
  width: 100%;
  height: 100%;
  color: var(--primary-color);
  animation: spin 1s linear infinite;
}

.spinner-circle {
  stroke-dasharray: 50;
  stroke-dashoffset: 25;
}

.error-icon-large {
  width: 64px;
  height: 64px;
  margin: 0 auto 1.5rem;
  color: var(--danger-color);
}

.error-content h3 {
  color: var(--danger-color);
  margin-bottom: 0.5rem;
}

.error-content p {
  margin-bottom: 1.5rem;
}

.btn-retry {
  background: var(--danger-color);
}

.btn-retry:hover {
  background: #dc2626;
}

.stat-card {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 1rem;
}

.stat-icon {
  width: 32px;
  height: 32px;
  padding: 0.5rem;
  border-radius: var(--border-radius-sm);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.stat-icon.success {
  background: rgba(16, 185, 129, 0.1);
  color: var(--success-color);
}

.stat-icon.info {
  background: rgba(99, 102, 241, 0.1);
  color: var(--primary-color);
}

.stat-icon.warning {
  background: rgba(245, 158, 11, 0.1);
  color: var(--warning-color);
}

.stat-content {
  flex: 1;
  line-height: 1.3;
}

.stat-label {
  font-size: 0.75rem;
  color: var(--text-tertiary);
  margin-bottom: 0.125rem;
  line-height: 1.2;
}

.stat-value {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--text-primary);
  line-height: 1.2;
}

@media (prefers-color-scheme: dark) {
  .json-display {
    background: var(--dark-bg-tertiary);
    color: var(--dark-text-primary);
    border-color: var(--dark-border-color);
  }
  
  .card-footer {
    border-top-color: var(--dark-border-color);
  }
  
  .stat-label {
    color: var(--dark-text-tertiary);
  }
  
  .stat-value {
    color: var(--dark-text-primary);
  }
}

@media (max-width: 768px) {
  .dashboard-grid {
    grid-template-columns: 1fr;
  }
  
  .card {
    padding: 1.5rem;
  }
}
</style>
<template>
  <div id="app">
    <nav class="navbar" v-if="isAuthenticated">
      <div class="navbar-container">
        <div class="navbar-brand">
          <svg class="logo-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <circle cx="12" cy="12" r="10"/>
            <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>
            <path d="M2 12h20"/>
          </svg>
          <span class="navbar-title">Host IP Dashboard</span>
        </div>
        <div class="navbar-menu">
          <router-link to="/dashboard" class="nav-link">
            <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="3" y="3" width="7" height="7"/>
              <rect x="14" y="3" width="7" height="7"/>
              <rect x="14" y="14" width="7" height="7"/>
              <rect x="3" y="14" width="7" height="7"/>
            </svg>
            Dashboard
          </router-link>
          <button class="btn-logout" @click="logout">
            <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/>
              <polyline points="16 17 21 12 16 7"/>
              <line x1="21" y1="12" x2="9" y2="12"/>
            </svg>
            Logout
          </button>
        </div>
      </div>
    </nav>
    <main class="main-content">
      <router-view />
    </main>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue';
import { useRouter, useRoute } from 'vue-router';

const isAuthenticated = ref(!!localStorage.getItem('token'));
const router = useRouter();
const route = useRoute();

// Watch for route changes to update authentication state
watch(() => route.path, () => {
  isAuthenticated.value = !!localStorage.getItem('token');
});

const logout = () => {
  localStorage.removeItem('token');
  localStorage.removeItem('user');
  isAuthenticated.value = false;
  router.push('/login');
};
</script>

<style>
.navbar {
  background: var(--bg-primary);
  border-bottom: 1px solid var(--border-color);
  box-shadow: 0 1px 3px var(--shadow);
  position: sticky;
  top: 0;
  z-index: 100;
  backdrop-filter: blur(10px);
}

.navbar-container {
  max-width: 1200px;
  margin: 0 auto;
  padding: 1rem 2rem;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.navbar-brand {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.logo-icon {
  width: 32px;
  height: 32px;
  color: var(--primary-color);
}

.navbar-title {
  font-size: 1.25rem;
  font-weight: 700;
  color: var(--text-primary);
  background: linear-gradient(135deg, var(--primary-color), var(--secondary-color));
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.navbar-menu {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.nav-link {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 1rem;
  border-radius: var(--border-radius-sm);
  font-weight: 500;
  color: var(--text-secondary);
  transition: var(--transition);
}

.nav-link:hover,
.nav-link.router-link-active {
  color: var(--primary-color);
  background: rgba(99, 102, 241, 0.1);
}

.nav-icon {
  width: 20px;
  height: 20px;
}

.btn-logout {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 1rem;
  background: transparent;
  color: var(--text-secondary);
  box-shadow: none;
  border: 1px solid var(--border-color);
}

.btn-logout:hover {
  background: var(--danger-color);
  color: white;
  border-color: var(--danger-color);
}

.main-content {
  max-width: 1200px;
  margin: 0 auto;
  padding: 2rem;
  min-height: calc(100vh - 80px);
}

@media (prefers-color-scheme: dark) {
  .navbar {
    background: rgba(30, 41, 59, 0.8);
    border-bottom-color: var(--dark-border-color);
  }
  
  .navbar-title {
    color: var(--dark-text-primary);
  }
  
  .nav-link {
    color: var(--dark-text-secondary);
  }
  
  .nav-link:hover,
  .nav-link.router-link-active {
    color: var(--primary-light);
    background: rgba(99, 102, 241, 0.15);
  }
  
  .btn-logout {
    color: var(--dark-text-secondary);
    border-color: var(--dark-border-color);
  }
}

@media (max-width: 768px) {
  .navbar-container {
    flex-direction: column;
    gap: 1rem;
    padding: 1rem;
  }
  
  .navbar-menu {
    width: 100%;
    justify-content: center;
  }
  
  .main-content {
    padding: 1rem;
  }
}
</style>
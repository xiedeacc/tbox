<template>
  <div class="login-container">
    <div class="login-card card">
      <div class="login-header">
        <div class="login-icon">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <circle cx="12" cy="12" r="10"/>
            <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>
            <path d="M2 12h20"/>
          </svg>
        </div>
        <h2>Welcome Back</h2>
        <p class="login-subtitle">Sign in to access your dashboard</p>
      </div>
      
      <form @submit.prevent="login" class="login-form">
        <div class="form-group">
          <label for="user" class="form-label">
            <svg class="label-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/>
              <circle cx="12" cy="7" r="4"/>
            </svg>
            User
          </label>
          <input 
            v-model="user" 
            id="user" 
            type="text" 
            required 
            placeholder="Enter your username"
            class="form-input"
          />
        </div>
        
        <div class="form-group">
          <label for="password" class="form-label">
            <svg class="label-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="3" y="11" width="18" height="11" rx="2" ry="2"/>
              <path d="M7 11V7a5 5 0 0 1 10 0v4"/>
            </svg>
            Password
          </label>
          <input 
            v-model="password" 
            id="password" 
            type="password" 
            required 
            placeholder="Enter your password"
            class="form-input"
          />
        </div>
        
        <button type="submit" class="btn-login" :disabled="loading">
          <span v-if="!loading">Sign In</span>
          <svg v-else class="spinner" viewBox="0 0 24 24">
            <circle class="spinner-circle" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" fill="none"/>
          </svg>
        </button>
      </form>
      
      <div v-if="error" class="error-message">
        <svg class="error-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <circle cx="12" cy="12" r="10"/>
          <line x1="12" y1="8" x2="12" y2="12"/>
          <line x1="12" y1="16" x2="12.01" y2="16"/>
        </svg>
        {{ error }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue';
import { useRouter } from 'vue-router';
import { v4 as uuidv4 } from 'uuid';
import { sha256StringHex } from '../utils/utils.js';

const user = ref('');
const password = ref('');
const error = ref(null);
const loading = ref(false);
const router = useRouter();

const generateRequestId = () => uuidv4();

const login = async () => {
  loading.value = true;
  error.value = null;
  
  const userCredentials = {
    user: user.value,
    password: sha256StringHex(password.value),
    op: 'OP_USER_LOGIN',
    request_id: generateRequestId(),
  };

  try {
    const response = await fetch('/user', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(userCredentials),
    });

    const data = await response.json();
    if (response.ok && data && data.token) {
      localStorage.setItem('user', user.value);
      localStorage.setItem('token', data.token);
      router.push('/dashboard');
    } else {
      error.value = 'Invalid username or password. Please try again.';
    }
  } catch (err) {
    error.value = 'Unable to connect to server. Please try again later.';
  } finally {
    loading.value = false;
  }
};
</script>

<style scoped>
.login-container {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  width: 100vw;
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 2rem;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  overflow: hidden;
}

.login-container::before {
  content: '';
  position: absolute;
  top: -50%;
  left: -50%;
  width: 200%;
  height: 200%;
  background: radial-gradient(circle, rgba(255,255,255,0.1) 1px, transparent 1px);
  background-size: 50px 50px;
  animation: moveBackground 20s linear infinite;
}

@keyframes moveBackground {
  0% { transform: translate(0, 0); }
  100% { transform: translate(50px, 50px); }
}

.login-card {
  width: 100%;
  max-width: 440px;
  position: relative;
  z-index: 1;
  animation: slideUp 0.5s ease-out;
}

@keyframes slideUp {
  from {
    opacity: 0;
    transform: translateY(30px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.login-header {
  text-align: center;
  margin-bottom: 2rem;
}

.login-icon {
  width: 64px;
  height: 64px;
  margin: 0 auto 1.5rem;
  padding: 1rem;
  background: linear-gradient(135deg, var(--primary-color), var(--secondary-color));
  border-radius: var(--border-radius);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 10px 25px rgba(99, 102, 241, 0.3);
}

.login-icon svg {
  width: 100%;
  height: 100%;
  color: white;
}

.login-header h2 {
  font-size: 2rem;
  margin-bottom: 0.5rem;
  color: var(--text-primary);
}

.login-subtitle {
  color: var(--text-secondary);
  font-size: 0.95rem;
}

.login-form {
  margin-bottom: 1.5rem;
}

.form-group {
  margin-bottom: 1.5rem;
  text-align: left;
}

.form-label {
  display: inline-flex;
  align-items: center;
  justify-content: flex-start;
  gap: 0.5rem;
  margin-bottom: 0.5rem;
  font-weight: 500;
  color: var(--text-primary);
  font-size: 0.9rem;
}

.label-icon {
  width: 18px;
  height: 18px;
  color: var(--primary-color);
}

.form-input {
  width: 100%;
  padding: 0.875rem 1rem;
  font-size: 1rem;
  text-align: left;
}

.btn-login {
  width: auto;
  min-width: 200px;
  padding: 1rem 2rem;
  font-size: 1rem;
  font-weight: 600;
  margin: 0.5rem auto 0;
  display: block;
}

.spinner {
  width: 20px;
  height: 20px;
  animation: spin 1s linear infinite;
  margin: 0 auto;
  display: block;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.spinner-circle {
  stroke-dasharray: 50;
  stroke-dashoffset: 25;
}

.error-message {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 1rem;
  background: rgba(239, 68, 68, 0.1);
  border: 1px solid var(--danger-color);
  border-radius: var(--border-radius-sm);
  color: var(--danger-color);
  font-size: 0.9rem;
  animation: shake 0.5s ease-in-out;
}

@keyframes shake {
  0%, 100% { transform: translateX(0); }
  25% { transform: translateX(-5px); }
  75% { transform: translateX(5px); }
}

.error-icon {
  width: 20px;
  height: 20px;
  flex-shrink: 0;
}

@media (prefers-color-scheme: dark) {
  .login-header h2 {
    color: var(--dark-text-primary);
  }
  
  .login-subtitle {
    color: var(--dark-text-secondary);
  }
  
  .form-label {
    color: var(--dark-text-primary);
  }
}

@media (max-width: 640px) {
  .login-container {
    padding: 1rem;
  }
  
  .login-card {
    padding: 1.5rem;
  }
  
  .login-header h2 {
    font-size: 1.75rem;
  }
}
</style>
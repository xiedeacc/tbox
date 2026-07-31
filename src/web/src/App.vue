<template>
  <div id="app">
    <nav v-if="isAuthenticated" class="topbar">
      <router-link to="/dashboard" class="brand">
        <span class="brand-mark"><i></i><i></i><i></i></span>
        <span><b>TBOX</b><small>Network Console</small></span>
      </router-link>
      <div class="nav-actions">
        <span class="user-pill">{{ username }}</span>
        <button class="logout" title="Sign out" @click="logout">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><path d="m16 17 5-5-5-5M21 12H9"/></svg>
          Sign out
        </button>
      </div>
    </nav>
    <main><router-view /></main>
  </div>
</template>

<script setup>
import { computed, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
const route = useRoute();
const router = useRouter();
const isAuthenticated = ref(!!localStorage.getItem('token'));
const username = computed(() => localStorage.getItem('user') || 'admin');
watch(() => route.path, () => { isAuthenticated.value = !!localStorage.getItem('token'); });
const logout = () => {
  localStorage.removeItem('token');
  localStorage.removeItem('user');
  isAuthenticated.value = false;
  router.push('/login');
};
</script>

<style scoped>
.topbar{height:68px;display:flex;align-items:center;justify-content:space-between;padding:0 max(24px,calc((100vw - 1240px)/2 + 28px));position:sticky;top:0;z-index:20;background:rgba(7,18,23,.82);backdrop-filter:blur(18px);border-bottom:1px solid var(--border)}.brand{display:flex;align-items:center;gap:11px;color:var(--text)}.brand-mark{width:35px;height:35px;display:flex;align-items:flex-end;justify-content:center;gap:3px;padding:8px;background:linear-gradient(145deg,#21c7aa,#82e3c8);border-radius:10px;box-shadow:0 7px 24px rgba(33,199,170,.18)}.brand-mark i{width:4px;background:#07232a;border-radius:4px}.brand-mark i:nth-child(1){height:8px}.brand-mark i:nth-child(2){height:17px}.brand-mark i:nth-child(3){height:12px}.brand>span:last-child{display:flex;flex-direction:column}.brand b{font-size:15px;letter-spacing:.14em}.brand small{font-size:9px;color:var(--muted);letter-spacing:.08em}.nav-actions{display:flex;align-items:center;gap:10px}.user-pill{font-size:12px;color:var(--muted);padding:8px 12px;border:1px solid var(--border);border-radius:999px}.logout{display:flex;align-items:center;gap:7px;padding:8px 11px;background:transparent;border:0;color:var(--muted);box-shadow:none}.logout:hover{color:var(--text);background:var(--panel-soft);transform:none}.logout svg{width:16px}@media(max-width:520px){.topbar{padding:0 16px}.user-pill{display:none}.logout{font-size:0}.logout svg{width:19px}}
</style>

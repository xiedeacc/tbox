<template>
  <div class="login-page">
    <section class="login-intro">
      <div class="intro-mark"><i></i><i></i><i></i></div>
      <span class="overline">TBOX NETWORK CONSOLE</span>
      <h1>Every host.<br><em>One clear view.</em></h1>
      <p>Securely monitor the public IPv4 and IPv6 addresses reported by your distributed clients.</p>
      <div class="signal"><span></span><b>Control plane online</b><small>ip.xiedeacc.com</small></div>
    </section>

    <section class="login-panel">
      <form @submit.prevent="login">
        <div class="form-heading"><span>SECURE ACCESS</span><h2>Sign in</h2><p>Use your administrator credentials to continue.</p></div>
        <label>Username<input v-model.trim="user" type="text" autocomplete="username" required placeholder="admin"></label>
        <label>Password<input v-model="password" type="password" autocomplete="current-password" required placeholder="Enter your password"></label>
        <div v-if="error" class="form-error"><b>!</b>{{ error }}</div>
        <button type="submit" :disabled="loading">
          <span>{{ loading ? 'Authenticating…' : 'Continue to dashboard' }}</span>
          <svg v-if="!loading" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14m-6-6 6 6-6 6"/></svg>
          <i v-else></i>
        </button>
        <small class="security-note"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="4" y="10" width="16" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/></svg>Credentials are protected in transit with TLS.</small>
      </form>
    </section>
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
const login = async () => {
  loading.value = true;
  error.value = null;
  try {
    const response = await fetch('/user', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ user:user.value, password:sha256StringHex(password.value), op:'OP_USER_LOGIN', request_id:uuidv4() }) });
    const data = await response.json();
    if (!response.ok || !data?.token) throw new Error('The username or password is incorrect.');
    localStorage.setItem('user', user.value);
    localStorage.setItem('token', data.token);
    await router.push('/dashboard');
  } catch (err) {
    error.value = err.message === 'The username or password is incorrect.' ? err.message : 'The server could not be reached. Please try again.';
  } finally { loading.value = false; }
};
</script>

<style scoped>
.login-page{min-height:100vh;display:grid;grid-template-columns:minmax(400px,1.05fr) minmax(420px,.95fr)}.login-intro{padding:9vh 8vw;display:flex;flex-direction:column;justify-content:center;position:relative;overflow:hidden}.login-intro:after{content:"";position:absolute;width:460px;height:460px;right:-230px;bottom:-230px;border:1px solid rgba(68,214,178,.15);border-radius:50%;box-shadow:0 0 0 70px rgba(68,214,178,.025),0 0 0 140px rgba(68,214,178,.018)}.intro-mark{width:54px;height:54px;display:flex;align-items:flex-end;gap:5px;padding:13px;margin-bottom:32px;border-radius:15px;background:linear-gradient(145deg,#2bc9ac,#84e3c9);box-shadow:0 18px 48px rgba(43,201,172,.2)}.intro-mark i{width:6px;background:#06252b;border-radius:5px}.intro-mark i:nth-child(1){height:13px}.intro-mark i:nth-child(2){height:28px}.intro-mark i:nth-child(3){height:20px}.overline,.form-heading span{color:var(--accent);font-size:11px;font-weight:800;letter-spacing:.19em}.login-intro h1{font-size:clamp(44px,6vw,76px);line-height:.98;letter-spacing:-.065em;margin:18px 0 24px;max-width:690px}.login-intro h1 em{font-style:normal;color:var(--accent)}.login-intro>p{font-size:16px;max-width:510px}.signal{display:grid;grid-template-columns:auto 1fr;column-gap:10px;align-items:center;margin-top:58px;font-size:12px}.signal span{grid-row:1/3;width:9px;height:9px;border-radius:50%;background:var(--success);box-shadow:0 0 0 7px rgba(73,213,166,.1)}.signal b{font-size:12px}.signal small{color:var(--muted);font:10px var(--mono)}.login-panel{display:grid;place-items:center;padding:40px;background:rgba(13,29,36,.64);border-left:1px solid var(--border);backdrop-filter:blur(18px)}form{width:min(100%,430px)}.form-heading{margin-bottom:38px}.form-heading h2{font-size:38px;letter-spacing:-.05em;margin:9px 0 8px}.form-heading p{font-size:14px}label{display:block;font-size:12px;font-weight:750;color:#bfd0d1;margin-bottom:20px}input{display:block;width:100%;margin-top:8px;padding:14px 15px;border:1px solid var(--border);border-radius:10px;background:#09191f;color:var(--text);outline:none;transition:border .2s,box-shadow .2s}input::placeholder{color:#526b70}input:focus{border-color:rgba(68,214,178,.65);box-shadow:0 0 0 4px rgba(68,214,178,.08)}form>button{width:100%;height:50px;margin-top:8px;display:flex;justify-content:center;align-items:center;gap:12px}form>button svg{width:18px}form>button i{width:18px;height:18px;border:2px solid rgba(7,32,41,.35);border-top-color:#072029;border-radius:50%;animation:spin .7s linear infinite}.form-error{display:flex;align-items:center;gap:9px;padding:11px 12px;margin:4px 0 8px;border:1px solid rgba(255,102,112,.3);background:rgba(255,102,112,.08);border-radius:9px;color:#ff9ca3;font-size:12px}.form-error b{display:grid;place-items:center;width:19px;height:19px;background:var(--danger);color:white;border-radius:50%}.security-note{display:flex;justify-content:center;align-items:center;gap:7px;color:var(--muted);font-size:10px;margin-top:20px}.security-note svg{width:13px}@keyframes spin{to{transform:rotate(360deg)}}@media(max-width:820px){.login-page{grid-template-columns:1fr}.login-intro{min-height:38vh;padding:54px 28px 42px}.login-intro h1{font-size:48px}.login-intro>p,.signal{display:none}.intro-mark{width:44px;height:44px;padding:11px;margin-bottom:20px}.login-panel{border-left:0;border-top:1px solid var(--border);padding:48px 24px;place-items:start center}}@media(max-width:460px){.login-intro h1{font-size:39px}.login-panel{padding:40px 20px}.form-heading h2{font-size:32px}}
</style>

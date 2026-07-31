<template>
  <section class="dashboard-shell">
    <header class="hero">
      <div>
        <div class="eyebrow"><span class="pulse"></span> TBOX NETWORK</div>
        <h1>Host overview</h1>
        <p>Public addresses reported by every connected TBox node.</p>
      </div>
      <button class="refresh-button" :disabled="loading" @click="fetchServerData">
        <svg :class="{ spinning: loading }" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M20 11a8.1 8.1 0 0 0-15.5-2M4 4v5h5"/>
          <path d="M4 13a8.1 8.1 0 0 0 15.5 2M20 20v-5h-5"/>
        </svg>
        {{ loading ? 'Refreshing' : 'Refresh' }}
      </button>
    </header>

    <div v-if="error" class="alert">
      <span>!</span>
      <div><strong>Unable to load network status</strong><p>{{ error }}</p></div>
      <button @click="fetchServerData">Retry</button>
    </div>

    <template v-if="serverData">
      <div class="summary-grid">
        <article class="summary-card accent">
          <span class="summary-label">Registered nodes</span>
          <strong>{{ clients.length }}</strong>
          <small>{{ onlineCount }} reporting recently</small>
        </article>
        <article class="summary-card">
          <span class="summary-label">Your public address</span>
          <strong class="address-value">{{ serverData.current_client_ip || 'Unknown' }}</strong>
          <small>Observed by the server</small>
        </article>
        <article class="summary-card">
          <span class="summary-label">Server endpoints</span>
          <strong>{{ serverData.server_ip?.length || 0 }}</strong>
          <small>IPv4 and IPv6 addresses</small>
        </article>
        <article class="summary-card">
          <span class="summary-label">Build</span>
          <strong class="build-value">{{ shortCommit }}</strong>
          <small>{{ dirtyBuild ? 'Uncommitted build' : 'Deployed revision' }}</small>
        </article>
      </div>

      <section class="panel server-panel">
        <div class="panel-heading">
          <div><span class="section-kicker">CONTROL PLANE</span><h2>Server addresses</h2></div>
          <span class="status-chip"><span class="status-dot"></span> Online</span>
        </div>
        <div class="address-list">
          <span v-for="address in serverData.server_ip" :key="address" class="address-pill">
            <b>{{ address.includes(':') ? 'IPv6' : 'IPv4' }}</b>{{ address }}
          </span>
          <span v-if="!serverData.server_ip?.length" class="muted">No server address available</span>
        </div>
      </section>

      <section class="nodes-section">
        <div class="section-heading">
          <div><span class="section-kicker">CONNECTED HOSTS</span><h2>Client nodes</h2></div>
          <span class="updated">Updated {{ lastUpdatedLabel }}</span>
        </div>

        <div v-if="clients.length" class="node-grid">
          <article v-for="client in clients" :key="client.id" class="node-card">
            <div class="node-topline">
              <div class="node-icon">{{ initials(client.id) }}</div>
              <div class="node-title"><h3>{{ friendlyName(client.id) }}</h3><code>{{ client.id }}</code></div>
              <span class="node-state" :class="{ stale: !client.online }">
                <span></span>{{ client.online ? 'Active' : 'Stale' }}
              </span>
            </div>

            <div class="ip-group">
              <div class="ip-heading"><span>IPv4</span><b>{{ client.ipv4.length }}</b></div>
              <code v-for="ip in client.ipv4" :key="ip" class="ip-row">{{ ip }}</code>
              <span v-if="!client.ipv4.length" class="empty-ip">Not reported</span>
            </div>
            <div class="ip-group">
              <div class="ip-heading"><span>IPv6</span><b>{{ client.ipv6.length }}</b></div>
              <code v-for="ip in client.ipv6" :key="ip" class="ip-row">{{ ip }}</code>
              <span v-if="!client.ipv6.length" class="empty-ip">Not reported</span>
            </div>

            <footer>
              <span>Last report</span>
              <time>{{ client.last_report_time || 'Unknown' }}</time>
            </footer>
          </article>
        </div>

        <div v-else class="empty-state">
          <div class="empty-orbit"><span></span></div>
          <h3>No clients have reported yet</h3>
          <p>Clients will appear automatically after authentication and their next IP report.</p>
          <button @click="fetchServerData">Check again</button>
        </div>
      </section>
    </template>

    <div v-else-if="loading" class="loading-state">
      <div class="loader"></div><p>Contacting the TBox server…</p>
    </div>
  </section>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';

const serverData = ref(null);
const error = ref(null);
const loading = ref(false);
const lastUpdatedTime = ref(null);
const clock = ref(Date.now());
let refreshTimer;
let clockTimer;

const parseReportTime = (value) => {
  if (!value) return 0;
  const parsed = Date.parse(value.replace(' ', 'T'));
  return Number.isNaN(parsed) ? 0 : parsed;
};

const clients = computed(() => Object.entries(serverData.value?.registered_clients || {})
  .map(([id, info]) => ({
    id,
    ...info,
    ipv4: info.ipv4 || [],
    ipv6: info.ipv6 || [],
    online: clock.value - parseReportTime(info.last_report_time) < 120_000,
  }))
  .sort((a, b) => a.id.localeCompare(b.id)));

const onlineCount = computed(() => clients.value.filter((client) => client.online).length);
const dirtyBuild = computed(() => serverData.value?.git_commit?.endsWith('-dirty'));
const shortCommit = computed(() => {
  const commit = serverData.value?.git_commit || 'unknown';
  return commit.replace('-dirty', '').slice(0, 8);
});
const lastUpdatedLabel = computed(() => {
  if (!lastUpdatedTime.value) return 'never';
  const seconds = Math.max(0, Math.floor((clock.value - lastUpdatedTime.value) / 1000));
  if (seconds < 5) return 'just now';
  if (seconds < 60) return `${seconds}s ago`;
  return `${Math.floor(seconds / 60)}m ago`;
});

const friendlyName = (id) => {
  if (/macmini/i.test(id)) return 'Mac mini';
  if (/openwrt/i.test(id)) return 'OpenWrt';
  if (/nas/i.test(id)) return 'NAS';
  if (/windows/i.test(id)) return 'Windows';
  return id.replace(/[-_]+/g, ' ').replace(/\b\w/g, (letter) => letter.toUpperCase());
};
const initials = (id) => friendlyName(id).split(' ').map((part) => part[0]).join('').slice(0, 2);

const fetchServerData = async () => {
  if (loading.value) return;
  loading.value = true;
  error.value = null;
  try {
    const response = await fetch('/server', { method: 'GET', cache: 'no-store' });
    if (!response.ok) throw new Error(`Server returned HTTP ${response.status}`);
    serverData.value = await response.json();
    lastUpdatedTime.value = Date.now();
  } catch (err) {
    error.value = err.message || 'Please check your connection and try again.';
  } finally {
    loading.value = false;
  }
};

onMounted(() => {
  fetchServerData();
  refreshTimer = window.setInterval(fetchServerData, 30_000);
  clockTimer = window.setInterval(() => { clock.value = Date.now(); }, 1_000);
});
onBeforeUnmount(() => {
  window.clearInterval(refreshTimer);
  window.clearInterval(clockTimer);
});
</script>

<style scoped>
.dashboard-shell{max-width:1240px;margin:0 auto;padding:52px 28px 80px}.hero{display:flex;align-items:flex-end;justify-content:space-between;gap:24px;margin-bottom:36px}.eyebrow,.section-kicker{font-size:11px;font-weight:800;letter-spacing:.18em;color:var(--accent);text-transform:uppercase}.eyebrow{display:flex;align-items:center;gap:9px;margin-bottom:14px}.pulse{width:8px;height:8px;border-radius:50%;background:var(--success);box-shadow:0 0 0 7px rgba(73,213,166,.12)}h1{font-size:clamp(34px,5vw,58px);letter-spacing:-.055em;margin:0 0 10px}.hero p{font-size:16px}.refresh-button{display:flex;align-items:center;gap:9px;padding:11px 16px;background:var(--panel);color:var(--text);border:1px solid var(--border);box-shadow:none}.refresh-button svg{width:17px}.spinning{animation:spin .8s linear infinite}.summary-grid{display:grid;grid-template-columns:1.05fr 1.5fr 1fr 1fr;gap:14px;margin-bottom:14px}.summary-card,.panel,.node-card{background:var(--panel);border:1px solid var(--border);box-shadow:var(--shadow);border-radius:18px}.summary-card{padding:20px 22px;min-height:132px;display:flex;flex-direction:column;justify-content:space-between;overflow:hidden}.summary-card.accent{background:linear-gradient(145deg,#163b47,#102c36);border-color:#285766}.summary-label{font-size:12px;color:var(--muted);font-weight:700}.summary-card strong{font-size:34px;line-height:1;letter-spacing:-.04em}.summary-card small{color:var(--muted);font-size:12px}.address-value{font-family:var(--mono);font-size:17px!important;overflow-wrap:anywhere}.build-value{font-family:var(--mono);font-size:24px!important;color:var(--accent)}.panel{padding:24px 26px;margin-bottom:34px}.panel-heading,.section-heading{display:flex;align-items:center;justify-content:space-between;gap:16px}.panel-heading h2,.section-heading h2{font-size:22px;margin:4px 0 0;letter-spacing:-.025em}.status-chip,.node-state{display:inline-flex;align-items:center;gap:7px;color:var(--success);font-size:12px;font-weight:700;background:rgba(73,213,166,.09);padding:7px 10px;border-radius:999px}.status-dot,.node-state span{width:7px;height:7px;border-radius:50%;background:currentColor}.address-list{display:flex;gap:10px;flex-wrap:wrap;margin-top:22px}.address-pill{display:flex;align-items:center;gap:10px;font:13px var(--mono);padding:10px 12px;border:1px solid var(--border);background:var(--panel-soft);border-radius:10px}.address-pill b{font:700 10px var(--sans);color:var(--accent);letter-spacing:.08em}.nodes-section{margin-top:4px}.section-heading{margin-bottom:18px}.updated{font-size:12px;color:var(--muted)}.node-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.node-card{padding:22px}.node-topline{display:flex;align-items:center;gap:12px;margin-bottom:22px}.node-icon{width:42px;height:42px;display:grid;place-items:center;border-radius:12px;color:#07242a;font-size:13px;font-weight:900;background:linear-gradient(135deg,var(--accent),#7ee0c2)}.node-title{min-width:0;flex:1}.node-title h3{margin:0 0 3px;font-size:17px}.node-title code{display:block;color:var(--muted);font-size:11px;overflow:hidden;text-overflow:ellipsis}.node-state.stale{color:var(--warning);background:rgba(244,181,72,.1)}.ip-group{border-top:1px solid var(--border);padding:14px 0 4px}.ip-heading{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;color:var(--muted);font-size:11px;font-weight:800;letter-spacing:.08em}.ip-heading b{font-size:10px;color:var(--accent)}.ip-row{display:block;padding:7px 9px;margin:5px 0;background:var(--panel-soft);border-radius:7px;color:var(--text);font-size:12px;overflow-wrap:anywhere}.empty-ip{display:block;color:var(--muted);font-size:12px;padding:7px 0}.node-card footer{display:flex;justify-content:space-between;gap:14px;padding-top:14px;margin-top:8px;border-top:1px solid var(--border);font-size:11px;color:var(--muted)}.node-card footer time{font-family:var(--mono);text-align:right}.empty-state,.loading-state{display:grid;place-items:center;text-align:center;padding:68px 24px;background:var(--panel);border:1px dashed var(--border);border-radius:18px}.empty-state h3{margin:20px 0 7px}.empty-state p{max-width:480px}.empty-state button{margin-top:20px}.empty-orbit{width:58px;height:58px;border:1px solid var(--border);border-radius:50%;display:grid;place-items:center}.empty-orbit:before,.empty-orbit span{content:'';border-radius:50%;background:var(--accent)}.empty-orbit:before{width:10px;height:10px;box-shadow:0 0 22px var(--accent)}.empty-orbit span{width:7px;height:7px;position:absolute;transform:translate(29px,-15px)}.loading-state{min-height:340px}.loader{width:34px;height:34px;border:3px solid var(--border);border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;margin-bottom:12px}.alert{display:flex;align-items:center;gap:14px;padding:16px 18px;margin-bottom:18px;background:rgba(255,102,112,.08);border:1px solid rgba(255,102,112,.3);border-radius:14px}.alert>span{width:28px;height:28px;display:grid;place-items:center;border-radius:50%;background:var(--danger);color:white;font-weight:900}.alert div{flex:1}.alert p{font-size:12px;margin-top:2px}.alert button{padding:8px 12px}@keyframes spin{to{transform:rotate(360deg)}}@media(max-width:900px){.summary-grid{grid-template-columns:repeat(2,1fr)}.node-grid{grid-template-columns:1fr}}@media(max-width:600px){.dashboard-shell{padding:34px 16px 60px}.hero{align-items:flex-start;flex-direction:column}.summary-grid{grid-template-columns:1fr}.panel{padding:20px}.node-card{padding:18px}.node-topline{flex-wrap:wrap}.node-state{margin-left:54px}.panel-heading,.section-heading{align-items:flex-start}.updated{margin-top:19px}.address-pill{width:100%;overflow-wrap:anywhere}.alert{align-items:flex-start;flex-wrap:wrap}.alert button{margin-left:42px}}
</style>

<script setup>
import { computed, inject, onBeforeUnmount, onMounted, ref } from 'vue'
import AppShell from '../ui/AppShell.vue'
import { api } from '../ui/api'
import { confirmAction, notify } from '../ui/dialogs'
import ApolloVersion from '../apollo_version'
import { parseLog } from '../troubleshooting/logs'

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const status = ref(null)
const statusError = ref('')
const fatal = ref([])
const update = ref(null)  // {kind: 'stable' | 'pre', name, url} or {kind: 'latest'} / {kind: 'ahead'}
const busy = ref(false)
let timer = null

async function refresh() {
  try {
    status.value = await api('./api/v1/status')
    statusError.value = ''
  } catch (e) {
    if (e.status !== 401) {
      statusError.value = e.message
    }
  }
}

async function loadLogs() {
  try {
    const text = await (await fetch('./api/logs', { credentials: 'include' })).text()
    fatal.value = parseLog(text).filter((l) => l.level === 'Fatal')
  } catch {
    // The log is optional here.
  }
}

async function checkUpdates(version, preReleases) {
  try {
    const current = new ApolloVersion(null, version)
    const stable = new ApolloVersion(await (await fetch('https://api.github.com/repos/Nori-Oppi/Norishine/releases/latest')).json(), null)
    if (stable.isGreater(current)) {
      update.value = { kind: 'stable', name: stable.release.name || stable.version, url: stable.release.html_url }
      return
    }
    if (preReleases) {
      const list = await (await fetch('https://api.github.com/repos/Nori-Oppi/Norishine/releases')).json()
      const pre = list.find((r) => r.prerelease)
      if (pre) {
        const preVersion = new ApolloVersion(pre, null)
        if (preVersion.isGreater(current, true)) {
          update.value = { kind: 'pre', name: pre.name || pre.tag_name, url: pre.html_url }
          return
        }
      }
    }
    update.value = { kind: current.isGreater(stable) ? 'ahead' : 'latest' }
  } catch {
    update.value = null  // No releases yet, or offline: say nothing.
  }
}

onMounted(async () => {
  await refresh()
  timer = setInterval(refresh, 5000)
  loadLogs()
  if (status.value) {
    let preReleases = false
    try {
      preReleases = !!(await api('./api/v1/config')).values.notify_pre_releases
    } catch { /* default off */ }
    checkUpdates(status.value.version, preReleases)
  }
})
onBeforeUnmount(() => clearInterval(timer))

const devBuild = computed(() => (status.value?.version || '').includes('dirty') || /^0\.0\.0/.test(status.value?.version || ''))

async function closeApp() {
  if (!await confirmAction({ title: t('home.close_title', { app: status.value.running_app.name }), body: t('home.close_body'), confirm: t('home.close'), danger: true })) {
    return
  }
  busy.value = true
  try {
    await api('./api/apps/close', { method: 'POST', body: {} })
    notify(t('home.closed'))
  } catch (e) {
    notify(t('home.close_failed', { error: e.message }), 'error')
  } finally {
    busy.value = false
    refresh()
  }
}

async function disconnect(session) {
  if (!await confirmAction({ title: t('home.disconnect_title', { name: session.name }), body: t('home.disconnect_body'), confirm: t('home.disconnect'), danger: true })) {
    return
  }
  try {
    await api('./api/clients/disconnect', { method: 'POST', body: { uuid: session.uuid } })
    notify(t('home.disconnected', { name: session.name }))
  } catch (e) {
    notify(t('home.disconnect_failed', { error: e.message }), 'error')
  }
  setTimeout(refresh, 800)
}

const clients = [
  { name: 'Norilight', platform: 'Windows', icon: 'fa-brands fa-windows', href: 'https://github.com/Nori-Oppi/Norilight', note: 'home.client_norilight' },
  { name: 'Artemis', platform: 'Android', icon: 'fa-brands fa-android', href: 'https://github.com/ClassicOldSong/moonlight-android', note: 'home.client_artemis' },
  { name: 'Moonlight', platform: 'home.client_everywhere', icon: 'fa-solid fa-display', href: 'https://moonlight-stream.org', note: 'home.client_moonlight' },
]
</script>

<template>
  <AppShell page="home">
    <main class="page-main">
      <div class="page-head">
        <div>
          <h1>{{ status ? status.hostname : 'Norishine' }}</h1>
          <p v-if="status" class="muted">
            Norishine {{ status.version }} · {{ status.platform }}
          </p>
        </div>
      </div>

      <div v-if="statusError" class="notice notice-error section">
        <span class="grow">{{ $t('home.status_failed', { error: statusError }) }}</span>
      </div>

      <div v-if="fatal.length" class="notice notice-error section" role="alert">
        <div class="grow">
          <strong>{{ $t('home.startup_errors') }}</strong>
          <ul class="fatal-list">
            <li v-for="(line, i) in fatal" :key="i">{{ line.message }}</li>
          </ul>
          <a href="./troubleshooting#logs">{{ $t('home.view_logs') }}</a>
        </div>
      </div>

      <div v-if="update && (update.kind === 'stable' || update.kind === 'pre')" class="notice notice-warning section">
        <span class="grow">
          <strong>{{ update.kind === 'stable' ? $t('home.update_stable') : $t('home.update_pre') }}</strong>
          {{ update.name }}
        </span>
        <a class="btn btn-small" :href="update.url" target="_blank" rel="noopener">{{ $t('home.download') }}</a>
      </div>

      <section class="section" v-if="status">
        <h2>{{ $t('home.now') }}</h2>
        <div class="card-pad">
          <div class="list-row">
            <span class="status-dot" :class="{ on: !!status.running_app }"></span>
            <div class="grow">
              <div class="row-title">{{ status.running_app ? status.running_app.name : $t('home.no_app') }}</div>
              <div class="row-sub">{{ status.running_app ? $t('home.app_running') : $t('home.no_app_sub') }}</div>
            </div>
            <button v-if="status.running_app" type="button" class="btn btn-small btn-danger" :disabled="busy" @click="closeApp">
              <i class="fas fa-stop" aria-hidden="true"></i> {{ $t('home.close') }}
            </button>
          </div>

          <div v-for="s in status.sessions || []" :key="s.uuid" class="list-row">
            <span class="status-dot on"></span>
            <div class="grow">
              <div class="row-title">{{ s.name || $t('home.unknown_client') }}</div>
              <div class="row-sub">
                {{ s.address }}
                <template v-if="s.width"> · {{ s.width }}×{{ s.height }} @ {{ s.fps }} fps</template>
                · {{ s.codec }}<template v-if="s.hdr"> HDR</template>
                <template v-if="s.bitrate_kbps"> · {{ (s.bitrate_kbps / 1000).toFixed(1) }} Mbps</template>
              </div>
            </div>
            <button type="button" class="btn btn-small" @click="disconnect(s)">
              <i class="fas fa-link-slash" aria-hidden="true"></i> {{ $t('home.disconnect') }}
            </button>
          </div>
          <p v-if="!(status.sessions || []).length" class="row-sub no-sessions">{{ $t('home.no_sessions') }}</p>
        </div>
      </section>

      <section class="section">
        <h2>{{ $t('home.shortcuts') }}</h2>
        <div class="tiles">
          <a class="tile" href="./pin"><i class="fas fa-link" aria-hidden="true"></i><span class="row-title">{{ $t('home.pair') }}</span><span class="row-sub">{{ $t('home.pair_sub') }}</span></a>
          <a class="tile" href="./clients"><i class="fas fa-mobile-screen" aria-hidden="true"></i><span class="row-title">{{ $t('navbar.clients') }}</span><span class="row-sub">{{ $t('home.devices_sub') }}</span></a>
          <a class="tile" href="./apps"><i class="fas fa-table-cells-large" aria-hidden="true"></i><span class="row-title">{{ $t('navbar.applications') }}</span><span class="row-sub">{{ $t('home.apps_sub') }}</span></a>
          <a class="tile" href="./config"><i class="fas fa-sliders" aria-hidden="true"></i><span class="row-title">{{ $t('navbar.configuration') }}</span><span class="row-sub">{{ $t('home.settings_sub') }}</span></a>
        </div>
      </section>

      <section class="section">
        <h2>{{ $t('home.clients') }}</h2>
        <div class="card-pad">
          <a v-for="c in clients" :key="c.name" class="list-row client-link" :href="c.href" target="_blank" rel="noopener">
            <i :class="c.icon" class="client-icon" aria-hidden="true"></i>
            <div class="grow">
              <div class="row-title">{{ c.name }} <span class="muted small">· {{ c.platform.startsWith('home.') ? $t(c.platform) : c.platform }}</span></div>
              <div class="row-sub">{{ $t(c.note) }}</div>
            </div>
            <i class="fas fa-arrow-up-right-from-square muted" aria-hidden="true"></i>
          </a>
        </div>
      </section>

      <p class="small muted footer">
        <span v-if="update && update.kind === 'latest'">{{ $t('home.up_to_date') }} · </span>
        <span v-if="devBuild">{{ $t('home.dev_build') }} · </span>
        <a href="https://github.com/Nori-Oppi/Norishine" target="_blank" rel="noopener">GitHub</a> ·
        <a href="https://github.com/Nori-Oppi/Norishine/blob/master/LICENSE" target="_blank" rel="noopener">{{ $t('home.license') }}</a> ·
        <a href="https://github.com/Nori-Oppi/Norishine/blob/master/NOTICE" target="_blank" rel="noopener">{{ $t('home.notices') }}</a>
      </p>
    </main>
  </AppShell>
</template>

<style scoped>
.fatal-list { margin: 6px 0; padding-left: 18px; }
.no-sessions { padding: 12px 0 2px; border-top: 1px solid var(--row-divider); margin: 0; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px; }
.tile {
  display: flex; flex-direction: column; gap: 4px; padding: 16px; border-radius: var(--radius);
  background: var(--card); text-decoration: none; color: inherit; border: 1px solid transparent;
}
.tile:hover { background: var(--card-hover); border-color: var(--border); }
.tile > i { color: var(--accent); font-size: 18px; margin-bottom: 4px; }
.client-link { text-decoration: none; color: inherit; }
.client-link:hover .row-title { color: var(--accent); }
.client-icon { width: 24px; text-align: center; font-size: 18px; color: var(--text-secondary); }
.footer { text-align: center; margin-top: 30px; }
</style>

<script setup>
import { computed, inject, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import AppShell from '../ui/AppShell.vue'
import HelpLink from '../ui/HelpLink.vue'
import { api, restartHost } from '../ui/api'
import { confirmAction, notify } from '../ui/dialogs'
import { levelRank, parseLog } from './logs'

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const raw = ref('')
const loadError = ref('')
const minLevel = ref('Info')
const query = ref('')
const live = ref(true)
const follow = ref(true)
const platform = ref('')
const busy = ref('')
const logBox = ref(null)
let timer = null

const MAX_SHOWN = 3000

async function loadLogs() {
  try {
    const r = await fetch('./api/logs', { credentials: 'include', cache: 'no-store' })
    if (r.status === 401) {
      location.href = './login?redir=' + encodeURIComponent('./troubleshooting')
      return
    }
    const type = r.headers.get('Content-Type') || ''
    const charset = (/charset=([^;]+)/i.exec(type) || [])[1] || 'utf-8'
    raw.value = new TextDecoder(charset.trim()).decode(await r.arrayBuffer())
    loadError.value = ''
  } catch (e) {
    loadError.value = e.message
  }
}

const entries = computed(() => parseLog(raw.value))
const filtered = computed(() => {
  const min = levelRank(minLevel.value)
  const words = query.value.toLowerCase().split(/\s+/).filter(Boolean)
  return entries.value.filter((e) => levelRank(e.level) >= min &&
    words.every((w) => e.message.toLowerCase().includes(w)))
})
const shown = computed(() => filtered.value.slice(-MAX_SHOWN))
const counts = computed(() => {
  const c = { Warning: 0, Error: 0, Fatal: 0 }
  for (const e of entries.value) {
    if (e.level in c) {
      c[e.level]++
    }
  }
  return c
})

watch(shown, async () => {
  if (follow.value) {
    await nextTick()
    if (logBox.value) {
      logBox.value.scrollTop = logBox.value.scrollHeight
    }
  }
})
watch(live, (on) => {
  clearInterval(timer)
  if (on) {
    timer = setInterval(loadLogs, 3000)
  }
})

onMounted(async () => {
  loadLogs()
  timer = setInterval(loadLogs, 3000)
  try {
    platform.value = (await api('./api/v1/status')).platform
  } catch { /* only hides the Windows-only action */ }
})
onBeforeUnmount(() => clearInterval(timer))

function onScroll() {
  const el = logBox.value
  follow.value = el.scrollHeight - el.scrollTop - el.clientHeight < 40
}

function copyLogs() {
  const text = filtered.value.map((e) => `[${e.time}]: ${e.level}: ${e.message}`).join('\n')
  navigator.clipboard.writeText(text).then(() => notify(t('trouble.copied')), () => notify(t('trouble.copy_failed'), 'error'))
}
function downloadLogs() {
  const link = document.createElement('a')
  link.href = URL.createObjectURL(new Blob([raw.value], { type: 'text/plain' }))
  link.download = 'norishine.log'
  link.click()
  URL.revokeObjectURL(link.href)
}

async function run(name, { confirmText, confirmBody, call, done }) {
  if (confirmText && !await confirmAction({ title: confirmText, body: confirmBody, confirm: t('trouble.' + name), danger: true })) {
    return
  }
  busy.value = name
  try {
    await call()
    if (done) {
      notify(done)
    }
  } catch (e) {
    notify(t('trouble.failed', { error: e.message }), 'error')
  } finally {
    busy.value = ''
  }
}

const closeApp = () => run('force_close', {
  call: () => api('./api/apps/close', { method: 'POST', body: {} }),
  done: t('trouble.force_close_done'),
})
const resetDisplay = () => run('dd_reset', {
  confirmText: t('trouble.dd_reset_confirm'),
  confirmBody: t('trouble.dd_reset_desc'),
  call: () => api('./api/reset-display-device-persistence', { method: 'POST', body: {} }),
  done: t('trouble.dd_reset_done'),
})
const restart = () => run('restart', {
  confirmText: t('trouble.restart_confirm'),
  confirmBody: t('trouble.restart_desc'),
  call: async () => {
    notify(t('settings.restarting'), 'warning')
    if (!await restartHost()) {
      throw new Error(t('settings.restart_timeout'))
    }
    location.reload()
  },
})
const quit = () => run('quit', {
  confirmText: t('trouble.quit_confirm'),
  confirmBody: t('trouble.quit_desc'),
  call: async () => {
    fetch('./api/quit', { method: 'POST', credentials: 'include', headers: { 'Content-Type': 'application/json' }, body: '{}' }).catch(() => {})
    clearInterval(timer)
    live.value = false
  },
  done: t('trouble.quit_done'),
})
</script>

<template>
  <AppShell page="troubleshooting">
    <main class="page-main wide">
      <div class="page-head">
        <div>
          <h1>{{ $t('trouble.title') }} <HelpLink page="GUIDE.md#troubleshooting" :label="$t('help.page')" /></h1>
        </div>
      </div>

      <section class="section">
        <div class="actions">
          <button type="button" class="btn" :disabled="!!busy" :title="$t('trouble.force_close_desc')" @click="closeApp">
            <i class="fas fa-stop" aria-hidden="true"></i> {{ $t('trouble.force_close') }}
          </button>
          <button type="button" class="btn" :disabled="!!busy" :title="$t('trouble.restart_desc')" @click="restart">
            <i class="fas" :class="busy === 'restart' ? 'fa-spinner fa-spin' : 'fa-rotate'" aria-hidden="true"></i> {{ $t('trouble.restart') }}
          </button>
          <button v-if="platform === 'windows'" type="button" class="btn" :disabled="!!busy" :title="$t('trouble.dd_reset_desc')" @click="resetDisplay">
            <i class="fas fa-display" aria-hidden="true"></i> {{ $t('trouble.dd_reset') }}
          </button>
          <button type="button" class="btn btn-danger" :disabled="!!busy" :title="$t('trouble.quit_desc')" @click="quit">
            <i class="fas fa-power-off" aria-hidden="true"></i> {{ $t('trouble.quit') }}
          </button>
        </div>
      </section>

      <section class="section" id="logs">
        <div class="section-head">
          <h2>{{ $t('trouble.logs') }}
            <span v-if="counts.Error + counts.Fatal" class="badge badge-error">{{ $t('trouble.errors', { n: counts.Error + counts.Fatal }, counts.Error + counts.Fatal) }}</span>
            <span v-if="counts.Warning" class="badge badge-careful">{{ $t('trouble.warnings', { n: counts.Warning }, counts.Warning) }}</span>
          </h2>
          <div class="log-buttons">
            <button type="button" class="btn btn-small" @click="copyLogs"><i class="fas fa-copy" aria-hidden="true"></i> {{ $t('trouble.copy') }}</button>
            <button type="button" class="btn btn-small" @click="downloadLogs"><i class="fas fa-download" aria-hidden="true"></i> {{ $t('trouble.download') }}</button>
          </div>
        </div>
        <div class="log-tools">
          <div class="search">
            <i class="fas fa-magnifying-glass" aria-hidden="true"></i>
            <input v-model="query" type="search" class="field" :placeholder="$t('trouble.search')" :aria-label="$t('trouble.search')">
          </div>
          <select v-model="minLevel" class="field level" :aria-label="$t('trouble.level')">
            <option value="Verbose">{{ $t('trouble.level_all') }}</option>
            <option value="Info">{{ $t('trouble.level_info') }}</option>
            <option value="Warning">{{ $t('trouble.level_warning') }}</option>
            <option value="Error">{{ $t('trouble.level_error') }}</option>
          </select>
          <label class="live">
            <span class="switch"><input v-model="live" type="checkbox" role="switch"><span class="track"></span></span>
            {{ $t('trouble.live') }}
          </label>
        </div>
        <p v-if="loadError" class="error-text">{{ $t('trouble.load_failed', { error: loadError }) }}</p>
        <div ref="logBox" class="log" tabindex="0" :aria-label="$t('trouble.logs')" @scroll="onScroll">
          <div v-if="filtered.length > shown.length" class="log-more">{{ $t('trouble.older_hidden', { n: filtered.length - shown.length }) }}</div>
          <div v-for="(e, i) in shown" :key="i" class="log-line" :class="'lv-' + e.level.toLowerCase()">
            <span class="log-time">{{ e.time.slice(11) }}</span>
            <span class="log-level">{{ e.level }}</span>
            <span class="log-msg">{{ e.message }}</span>
          </div>
          <div v-if="!shown.length" class="empty-note">{{ $t('trouble.no_lines') }}</div>
        </div>
      </section>
    </main>
  </AppShell>
</template>

<style scoped>
.page-main.wide { max-width: 1240px; }
.actions { display: flex; gap: 8px; flex-wrap: wrap; }
.section-head h2 { display: flex; gap: 8px; align-items: center; }
.badge-error { background: var(--error-bg); color: var(--error); }
.log-buttons { display: flex; gap: 8px; }
.log-tools { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; margin-bottom: 10px; }
.search { position: relative; flex: 1 1 260px; }
.search i { position: absolute; left: 11px; top: 50%; transform: translateY(-50%); color: var(--text-tertiary); font-size: 13px; }
.search .field { padding-left: 32px; }
.level { width: auto; }
.live { display: flex; align-items: center; gap: 8px; color: var(--text-secondary); font-size: 14px; cursor: pointer; }
.log {
  height: 60vh; min-height: 320px; overflow: auto; background: var(--sidebar); border-radius: var(--radius);
  padding: 10px 12px; font-family: var(--mono); font-size: 12.5px; line-height: 1.5;
}
.log-line { display: grid; grid-template-columns: 92px 64px 1fr; gap: 10px; white-space: pre-wrap; overflow-wrap: anywhere; }
.log-time { color: var(--text-tertiary); }
.log-level { color: var(--text-secondary); }
.log-msg { color: var(--text-primary); }
.lv-warning .log-level, .lv-warning .log-msg { color: var(--warning); }
.lv-error .log-level, .lv-error .log-msg, .lv-fatal .log-level, .lv-fatal .log-msg { color: var(--error); }
.lv-debug .log-msg, .lv-verbose .log-msg { color: var(--text-secondary); }
.log-more { color: var(--text-tertiary); padding-bottom: 6px; }
@media (max-width: 640px) {
  .log-line { grid-template-columns: 1fr; gap: 0; padding: 4px 0; border-bottom: 1px solid var(--divider); }
}
</style>

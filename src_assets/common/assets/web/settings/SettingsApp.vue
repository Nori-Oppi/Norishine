<script setup>
import { computed, inject, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import AppShell from '../ui/AppShell.vue'
import { api, restartHost } from '../ui/api'
import SettingRow from './SettingRow.vue'
import {
  RESET, computeChange, effective, formatValue, isDependencyMet, matches, same, searchWords,
} from './model'

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const loading = ref(true)
const loadError = ref('')
const schema = ref(null)
const stored = ref({})
const invalid = ref({})
const unknown = ref({})
const draft = reactive({})
const errors = ref({})
const requestError = ref('')
const saving = ref(false)
const notice = ref(null)  // {kind: 'ok' | 'restart' | 'restarting' | 'error', text}
const confirmList = ref(null)
const confirmCancel = ref(null)
const restartAfterConfirm = ref(false)
const streamWarning = ref(0)  // Active streams that "Save and restart" would end; asks first.
const displays = ref(null)
const query = ref('')
const category = ref('general')
const highlightKey = ref('')
const searchInput = ref(null)

const RESTART_KEY = 'norishine.restartNeeded'
const ADVANCED_KEY = 'norishine.showAdvanced'
const readFlag = (store, key) => { try { return store.getItem(key) === '1' } catch { return false } }
const writeFlag = (store, key, on) => { try { on ? store.setItem(key, '1') : store.removeItem(key) } catch { /* ignore */ } }

const showAdvanced = ref(readFlag(localStorage, ADVANCED_KEY))
const restartNeeded = ref(readFlag(sessionStorage, RESTART_KEY))
watch(showAdvanced, (on) => writeFlag(localStorage, ADVANCED_KEY, on))
watch(restartNeeded, (on) => writeFlag(sessionStorage, RESTART_KEY, on))

const settings = computed(() => schema.value ? schema.value.settings.filter((s) => s.available) : [])
const byKey = computed(() => Object.fromEntries((schema.value?.settings || []).map((s) => [s.key, s])))
const categoryTitle = computed(() => Object.fromEntries((schema.value?.categories || []).map((c) => [c.id, c.title])))
const visibleTier = (s) => showAdvanced.value || s.tier === 'basic'

const categories = computed(() => {
  const list = (schema.value?.categories || [])
    .map((c) => ({ ...c, settings: settings.value.filter((s) => s.category === c.id && visibleTier(s)) }))
    .filter((c) => c.settings.length > 0)
  if (showAdvanced.value && Object.keys(unknown.value).length) {
    list.push({ id: 'unknown', title: t('settings.unknown_title'), settings: [] })
  }
  return list
})
const hiddenCategoryCount = computed(() => {
  if (showAdvanced.value || !schema.value) {
    return 0
  }
  return schema.value.categories.filter((c) => settings.value.some((s) => s.category === c.id)).length - categories.value.length
})
const currentCategory = computed(() => categories.value.find((c) => c.id === category.value) || categories.value[0])

const words = computed(() => searchWords(query.value))
const searching = computed(() => words.value.length > 0)
const results = computed(() => {
  if (!searching.value) {
    return []
  }
  // Search covers every setting, advanced ones too, so nothing is hard to find.
  return (schema.value?.categories || [])
    .map((c) => ({ ...c, settings: settings.value.filter((s) => s.category === c.id && matches(s, words.value, c.title)) }))
    .filter((c) => c.settings.length > 0)
})
const resultCount = computed(() => results.value.reduce((n, c) => n + c.settings.length, 0))

const change = computed(() => computeChange(byKey.value, stored.value, invalid.value, draft))
const changedKeys = computed(() => new Set([...Object.keys(change.value.set), ...change.value.reset]))
const changeCount = computed(() => changedKeys.value.size)
const errorEntries = computed(() => Object.entries(errors.value).map(([key, message]) => ({
  key, message, title: byKey.value[key]?.title || key, category: byKey.value[key]?.category,
})))
const categoriesWithErrors = computed(() => new Set(errorEntries.value.map((e) => e.category)))
const categoriesWithChanges = computed(() => new Set([...changedKeys.value].map((k) => byKey.value[k]?.category)))

function valueOf(s) {
  return effective(s, stored.value, draft)
}
function canReset(s) {
  if (draft[s.key] === RESET) {
    return false
  }
  return !same(s, valueOf(s), s.default) || (s.key in invalid.value)
}
// "Balanced (default)" -> "Balanced", for "Default: ..." where the marker would repeat itself.
function plain(text) {
  return text.replace(/,\s*default\)$/i, ')').replace(/\s*\(default(?:;[^()]*)?\)$/i, '')
}
function dependencyText(s) {
  if (isDependencyMet(s, byKey.value, stored.value, draft)) {
    return ''
  }
  const other = byKey.value[s.depends_on.key]
  const values = s.depends_on.values.map((v) => '“' + plain(formatValue(other, v, t)) + '”')
  return t('settings.depends', { setting: other.title, value: values.join(' / ') })
}
const remapGroup = computed(() => {
  const res = byKey.value.dd_resolution_option
  const rate = byKey.value.dd_refresh_rate_option
  if (!res || !rate) {
    return 'mixed'
  }
  if (valueOf(res) !== 'auto') {
    return 'refresh_rate_only'
  }
  return valueOf(rate) !== 'auto' ? 'resolution_only' : 'mixed'
})

function update(s, value) {
  draft[s.key] = value
  if (errors.value[s.key]) {
    const next = { ...errors.value }
    delete next[s.key]
    errors.value = next
  }
  if (notice.value && notice.value.kind !== 'restarting') {
    notice.value = null
  }
}
function reset(s) {
  update(s, RESET)
}
function discard() {
  for (const key of Object.keys(draft)) {
    delete draft[key]
  }
  errors.value = {}
  requestError.value = ''
}

async function load() {
  loading.value = true
  loadError.value = ''
  try {
    const [schemaData, values] = await Promise.all([api('./api/v1/config/schema'), api('./api/v1/config')])
    schema.value = schemaData
    stored.value = values.values || {}
    invalid.value = values.invalid || {}
    unknown.value = values.unknown || {}
    discard()
    openFromHash()
    api('./api/v1/displays').then((d) => { displays.value = d.displays || null }).catch(() => { displays.value = null })
  } catch (e) {
    if (e.status !== 401) {
      loadError.value = e.message
    }
  } finally {
    loading.value = false
  }
}

const needsRestart = computed(() => [...changedKeys.value].some((k) => byKey.value[k]?.restart_required))

async function save(confirmed = false, andRestart = false) {
  if (changeCount.value === 0 || saving.value) {
    return
  }
  const dangerous = [...changedKeys.value].map((k) => byKey.value[k]).filter((s) => s && s.tier === 'dangerous')
  if (dangerous.length && !confirmed) {
    confirmList.value = dangerous.map((s) => s.title)
    restartAfterConfirm.value = andRestart
    return
  }
  confirmList.value = null
  if (andRestart && !streamWarning.value) {
    // Restarting ends every stream: say so first.
    const status = await api('./api/v1/status').catch(() => null)
    if (status && status.active_sessions > 0) {
      streamWarning.value = status.active_sessions
      return
    }
  }
  streamWarning.value = 0
  const ok = await doSave()
  if (ok && andRestart) {
    await restart()
  }
}

async function doSave() {
  saving.value = true
  requestError.value = ''
  errors.value = {}
  notice.value = null
  const body = {}
  if (Object.keys(change.value.set).length) {
    body.set = change.value.set
  }
  if (change.value.reset.length) {
    body.reset = change.value.reset
  }
  try {
    const result = await api('./api/v1/config', { method: 'PATCH', body })
    // Take the saved values as the new baseline.
    const values = await api('./api/v1/config')
    stored.value = values.values || {}
    invalid.value = values.invalid || {}
    unknown.value = values.unknown || {}
    discard()
    if (result.restart_required) {
      restartNeeded.value = true
    }
    notice.value = restartNeeded.value
      ? { kind: 'restart', text: t('settings.saved_restart') }
      : { kind: 'ok', text: t('settings.saved') }
    return true
  } catch (e) {
    if (e.status === 400 && e.body && e.body.errors) {
      const errs = { ...e.body.errors }
      if (errs['']) {
        requestError.value = errs['']
        delete errs['']
      }
      errors.value = errs
      // Show the first problem if it isn't on screen.
      const first = Object.keys(errs)[0]
      if (first && !searching.value && byKey.value[first]) {
        reveal(first, false)
      }
    } else if (e.status !== 401) {
      requestError.value = t('settings.save_failed', { error: e.message })
    }
  } finally {
    saving.value = false
  }
  return false
}

async function restart() {
  notice.value = { kind: 'restarting', text: t('settings.restarting') }
  const back = await restartHost()
  if (back) {
    restartNeeded.value = false
    notice.value = { kind: 'ok', text: t('settings.restarted') }
    // Sessions survive the restart; load() still goes to the login page if this one ended.
    await load()
    notice.value = { kind: 'ok', text: t('settings.restarted') }
  } else {
    notice.value = { kind: 'error', text: t('settings.restart_timeout') }
  }
}

function selectCategory(id) {
  category.value = id
  query.value = ''
  history.replaceState(null, '', '#' + id)
  window.scrollTo({ top: 0 })
}

// Open a setting: switch to its category (showing advanced settings if needed) and scroll to it.
async function reveal(key, updateHash = true) {
  const s = byKey.value[key]
  if (!s) {
    return
  }
  if (!visibleTier(s)) {
    showAdvanced.value = true
  }
  query.value = ''
  category.value = s.category
  if (updateHash) {
    history.replaceState(null, '', '#' + key)
  }
  await nextTick()
  const el = document.getElementById(key)
  if (el) {
    el.scrollIntoView({ block: 'center', behavior: 'smooth' })
    highlightKey.value = ''
    await nextTick()
    highlightKey.value = key
  }
}

function openFromHash() {
  const id = decodeURIComponent(location.hash.slice(1))
  if (!id) {
    return
  }
  if (byKey.value[id]) {
    reveal(id, false)
  } else if ((schema.value?.categories || []).some((c) => c.id === id) || id === 'unknown') {
    if (id === 'unknown') {
      showAdvanced.value = true
    }
    category.value = id
  }
}

function onKey(e) {
  if (e.key === '/' && document.activeElement?.tagName !== 'INPUT' && document.activeElement?.tagName !== 'TEXTAREA') {
    e.preventDefault()
    searchInput.value?.focus()
  }
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
    e.preventDefault()
    save()
  }
  if (e.key === 'Escape' && (confirmList.value || streamWarning.value)) {
    confirmList.value = null
    streamWarning.value = 0
  }
}
function onBeforeUnload(e) {
  if (changeCount.value > 0) {
    e.preventDefault()
    e.returnValue = t('settings.leave')
  }
}

// The risky choice is never the default: focus Cancel when a confirmation opens.
watch(() => !!confirmList.value || !!streamWarning.value, async (open) => {
  if (open) {
    await nextTick()
    confirmCancel.value?.focus()
  }
})

onMounted(() => {
  load()
  window.addEventListener('keydown', onKey)
  window.addEventListener('beforeunload', onBeforeUnload)
  window.addEventListener('hashchange', openFromHash)
})
onBeforeUnmount(() => {
  window.removeEventListener('keydown', onKey)
  window.removeEventListener('beforeunload', onBeforeUnload)
  window.removeEventListener('hashchange', openFromHash)
})

// Group headings in the sidebar (encoders).
const navItems = computed(() => {
  const items = []
  let group = ''
  for (const c of categories.value) {
    if ((c.group || '') !== group) {
      group = c.group || ''
      if (group) {
        items.push({ heading: c.group_title, id: 'group-' + group })
      }
    }
    items.push(c)
  }
  return items
})

const rowProps = (s, inSearch) => ({
  setting: s,
  value: valueOf(s),
  modified: changedKeys.value.has(s.key),
  canReset: canReset(s),
  defaultText: plain(formatValue(s, s.default, t)),
  error: errors.value[s.key] || '',
  invalidEntry: invalid.value[s.key] || null,
  dependencyText: dependencyText(s),
  indented: !inSearch && !!s.depends_on,
  showTier: inSearch,
  platform: schema.value?.platform || '',
  remapGroup: remapGroup.value,
  highlighted: highlightKey.value === s.key,
  displays: displays.value,
})
</script>

<template>
  <AppShell page="config">
    <div class="page">
      <aside class="sidebar" v-if="schema">
        <nav :aria-label="$t('settings.title')">
          <template v-for="item in navItems" :key="item.id">
            <div v-if="item.heading" class="nav-heading">{{ item.heading }}</div>
            <a v-else :href="'#' + item.id" class="nav-item"
               :class="{ active: !searching && currentCategory && item.id === currentCategory.id }"
               :aria-current="!searching && currentCategory && item.id === currentCategory.id ? 'page' : null"
               @click.prevent="selectCategory(item.id)">
              <span>{{ item.title }}</span>
              <span v-if="categoriesWithErrors.has(item.id)" class="dot dot-error" :title="$t('settings.not_saved')"></span>
              <span v-else-if="categoriesWithChanges.has(item.id)" class="dot" :title="$t('settings.unsaved', { n: 1 }, 1)"></span>
            </a>
          </template>
        </nav>
        <button v-if="hiddenCategoryCount > 0" type="button" class="link-button small sidebar-note"
                @click="showAdvanced = true">
          {{ $t('settings.more_categories', { n: hiddenCategoryCount }, hiddenCategoryCount) }}
        </button>
      </aside>

      <main class="content">
        <div class="content-head">
          <h1>{{ $t('settings.title') }}</h1>
          <div class="tools">
            <div class="search">
              <i class="fas fa-magnifying-glass" aria-hidden="true"></i>
              <input ref="searchInput" v-model="query" type="search" class="field" :placeholder="$t('settings.search')"
                     :aria-label="$t('settings.search')">
            </div>
            <label class="advanced-toggle">
              <span class="switch">
                <input type="checkbox" role="switch" v-model="showAdvanced">
                <span class="track"></span>
              </span>
              <span>{{ $t('settings.show_advanced') }}</span>
            </label>
          </div>
          <!-- Narrow screens: categories as a list box instead of the sidebar. -->
          <select v-if="schema && !searching" class="field category-select" :aria-label="$t('settings.title')"
                  :value="currentCategory?.id" @change="selectCategory($event.target.value)">
            <option v-for="c in categories" :key="c.id" :value="c.id">{{ c.group_title ? c.group_title + ': ' : '' }}{{ c.title }}</option>
          </select>
        </div>

        <div class="notices" aria-live="polite">
          <div v-if="notice" class="notice" :class="{ 'notice-ok': notice.kind === 'ok', 'notice-error': notice.kind === 'error' }">
            <i v-if="notice.kind === 'restarting'" class="fas fa-spinner fa-spin" aria-hidden="true"></i>
            <i v-else-if="notice.kind === 'ok'" class="fas fa-circle-check ok-icon" aria-hidden="true"></i>
            <span class="grow">{{ notice.text }}</span>
            <button v-if="notice.kind === 'restart'" type="button" class="btn btn-primary btn-small" @click="restart">
              <i class="fas fa-rotate" aria-hidden="true"></i> {{ $t('settings.restart_now') }}
            </button>
          </div>
          <div v-else-if="restartNeeded" class="notice">
            <span class="grow">{{ $t('settings.saved_restart') }}</span>
            <button type="button" class="btn btn-primary btn-small" @click="restart">
              <i class="fas fa-rotate" aria-hidden="true"></i> {{ $t('settings.restart_now') }}
            </button>
          </div>
          <div v-if="requestError || errorEntries.length" class="notice notice-error" role="alert">
            <div class="grow">
              <strong>{{ requestError || $t('settings.not_saved') }}</strong>
              <ul v-if="errorEntries.length" class="error-list">
                <li v-for="e in errorEntries" :key="e.key">
                  <a :href="'#' + e.key" @click.prevent="reveal(e.key)">{{ e.title }}</a>: {{ e.message }}
                </li>
              </ul>
            </div>
          </div>
        </div>

        <div v-if="loading && !schema" class="state muted"><i class="fas fa-spinner fa-spin" aria-hidden="true"></i></div>
        <div v-else-if="loadError" class="notice notice-error">
          <span class="grow">{{ $t('settings.load_failed', { error: loadError }) }}</span>
          <button type="button" class="btn btn-small" @click="load">{{ $t('settings.retry') }}</button>
        </div>

        <template v-else-if="schema">
          <!-- Search results, across all categories -->
          <template v-if="searching">
            <p class="small muted results-count">{{ $t('settings.result_count', { n: resultCount }, resultCount) }}</p>
            <section v-for="c in results" :key="c.id" class="section">
              <h2 class="card-title">{{ c.group_title ? c.group_title + ': ' : '' }}{{ c.title }}</h2>
              <div class="card">
                <SettingRow v-for="s in c.settings" :key="s.key" v-bind="rowProps(s, true)"
                            @update="update(s, $event)" @reset="reset(s)" />
              </div>
            </section>
            <div v-if="results.length === 0" class="empty-state">
              <p>{{ $t('settings.no_results', { query }) }}</p>
            </div>
          </template>

          <!-- Config file entries the host doesn't know -->
          <section v-else-if="currentCategory && currentCategory.id === 'unknown'" class="section">
            <p class="muted">{{ $t('settings.unknown_desc') }}</p>
            <div class="card">
              <div v-for="(text, key) in unknown" :key="key" class="unknown-row">
                <code>{{ key }}</code><code class="muted">{{ text }}</code>
              </div>
            </div>
          </section>

          <section v-else-if="currentCategory" class="section">
            <h2 class="visually-hidden">{{ currentCategory.title }}</h2>
            <p v-if="currentCategory.description" class="muted category-desc">{{ currentCategory.description }}</p>
            <div class="card">
              <SettingRow v-for="s in currentCategory.settings" :key="s.key" v-bind="rowProps(s, false)"
                          @update="update(s, $event)" @reset="reset(s)" />
            </div>
          </section>
        </template>
      </main>
    </div>

    <!-- Save bar -->
    <div class="savebar" v-if="changeCount > 0 || saving" role="region" :aria-label="$t('settings.save')">
      <div class="savebar-inner">
        <span class="grow">{{ $t('settings.unsaved', { n: changeCount }, changeCount) }}</span>
        <button type="button" class="btn" :disabled="saving" @click="discard">{{ $t('settings.discard') }}</button>
        <button v-if="needsRestart" type="button" class="btn" :disabled="saving" @click="save(false, false)">
          {{ $t('settings.save_only') }}
        </button>
        <button type="button" class="btn btn-primary" :disabled="saving" @click="save(false, needsRestart)">
          <i v-if="saving" class="fas fa-spinner fa-spin" aria-hidden="true"></i>
          {{ saving ? $t('settings.saving') : needsRestart ? $t('settings.save_restart') : $t('settings.save') }}
        </button>
      </div>
    </div>

    <!-- Confirmation for dangerous settings -->
    <div v-if="confirmList" class="dialog-backdrop" @click.self="confirmList = null">
      <div class="dialog" role="alertdialog" aria-modal="true" aria-labelledby="confirm-title" aria-describedby="confirm-body">
        <h2 id="confirm-title">{{ $t('settings.confirm_title') }}</h2>
        <p id="confirm-body" class="muted">{{ $t('settings.confirm_body') }}</p>
        <ul class="confirm-list">
          <li v-for="name in confirmList" :key="name">{{ name }}</li>
        </ul>
        <div class="actions">
          <button ref="confirmCancel" type="button" class="btn" @click="confirmList = null">{{ $t('settings.cancel') }}</button>
          <button type="button" class="btn btn-primary" @click="save(true, restartAfterConfirm)">{{ $t('settings.confirm_save') }}</button>
        </div>
      </div>
    </div>

    <!-- "Save and restart" while someone is streaming -->
    <div v-if="streamWarning" class="dialog-backdrop" @click.self="streamWarning = 0">
      <div class="dialog" role="alertdialog" aria-modal="true" aria-labelledby="streams-title" aria-describedby="streams-body">
        <h2 id="streams-title">{{ $t('settings.restart_streams_title') }}</h2>
        <p id="streams-body" class="muted">{{ $t('settings.restart_streams', { n: streamWarning }, streamWarning) }}</p>
        <div class="actions">
          <button ref="confirmCancel" type="button" class="btn" @click="streamWarning = 0">{{ $t('settings.cancel') }}</button>
          <button type="button" class="btn" @click="streamWarning = 0; save(true, false)">{{ $t('settings.save_only') }}</button>
          <button type="button" class="btn btn-primary" @click="save(true, true)">{{ $t('settings.save_restart') }}</button>
        </div>
      </div>
    </div>
  </AppShell>
</template>

<style scoped>
.page {
  max-width: 1240px; margin: 0 auto; padding: 24px 16px 120px;
  display: grid; grid-template-columns: 230px minmax(0, 1fr); gap: 32px;
}
.sidebar { position: sticky; top: 80px; align-self: start; max-height: calc(100vh - 100px); overflow-y: auto; }
.nav-item {
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  padding: 8px 12px; border-radius: 6px; color: var(--text-primary); text-decoration: none; font-size: 14.5px;
}
.nav-item:hover { background: var(--nav-hover); }
.nav-item.active { background: var(--nav-selected); font-weight: 600; box-shadow: inset 3px 0 0 var(--accent); }
.nav-heading { margin: 14px 12px 4px; font-size: 12px; font-weight: 600; color: var(--text-tertiary); text-transform: uppercase; letter-spacing: 0.04em; }
.dot { width: 8px; height: 8px; border-radius: 50%; background: var(--accent); flex: none; }
.dot-error { background: var(--error); }
.sidebar-note { margin: 14px 12px 0; }

.content { max-width: var(--content-max); min-width: 0; }
.content-head { display: flex; flex-direction: column; gap: 14px; margin-bottom: 16px; }
.tools { display: flex; gap: 16px; align-items: center; flex-wrap: wrap; }
.search { position: relative; flex: 1 1 280px; }
.search i { position: absolute; left: 11px; top: 50%; transform: translateY(-50%); color: var(--text-tertiary); font-size: 13px; }
.search .field { padding-left: 32px; }
.category-desc { margin: 0 0 12px; }
.link-button { border: 0; background: none; padding: 2px 0; color: var(--accent); font: inherit; cursor: pointer; font-weight: 500; }
.link-button:hover { text-decoration: underline; }
.sidebar-note { text-align: left; margin-top: 12px; padding: 0 12px; }
.advanced-toggle { display: flex; align-items: center; gap: 10px; cursor: pointer; color: var(--text-secondary); font-size: 14px; }
.category-select { display: none; }

.notices { display: flex; flex-direction: column; gap: 10px; margin-bottom: 16px; }
.notices:empty { display: none; }
.ok-icon { color: var(--ok); }
.error-list { margin: 6px 0 0; padding-left: 18px; }
.error-list a { color: inherit; font-weight: 600; }

.section { margin-bottom: 24px; }
.results-count { margin: 0 0 12px; }
.state { padding: 40px; text-align: center; font-size: 20px; }
.empty-state { padding: 32px 0; color: var(--text-secondary); }
.unknown-row { display: flex; gap: 16px; padding: 12px 0; border-top: 1px solid var(--row-divider); overflow-wrap: anywhere; }
.unknown-row:first-child { border-top: 0; }

.savebar {
  position: fixed; left: 0; right: 0; bottom: 0; z-index: 30;
  background: var(--popup); border-top: 1px solid var(--border); box-shadow: var(--shadow);
}
.savebar-inner { max-width: 1240px; margin: 0 auto; padding: 12px 16px; display: flex; gap: 10px; align-items: center; }
.savebar .grow { flex: 1; font-weight: 500; }

.confirm-list { margin: 0; padding-left: 20px; font-weight: 600; }

@media (max-width: 900px) {
  .page { grid-template-columns: minmax(0, 1fr); gap: 0; padding-top: 16px; }
  .sidebar { display: none; }
  .category-select { display: block; }
}
</style>

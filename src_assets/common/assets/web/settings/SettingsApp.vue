<script setup>
import { computed, inject, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import AppShell from '../ui/AppShell.vue'
import { api, restartHost } from '../ui/api'
import SettingRow from './SettingRow.vue'
import Recommendations from './Recommendations.vue'
import EncoderTest from '../ui/EncoderTest.vue'
import HelpLink from '../ui/HelpLink.vue'
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
// Phones: the category list is its own screen, and a category opens over it.
const NARROW = '(max-width: 900px)'
const isNarrow = () => window.matchMedia(NARROW).matches
const narrowList = ref(!location.hash)
// The encoder the host picked ("nvenc", ...), so the sidebar shows only its tuning page.
const activeEncoder = ref('')
const ENCODER_CATEGORY = { nvenc: 'nv', quicksync: 'qsv', amdvce: 'amd', vaapi: 'vaapi', videotoolbox: 'vt', software: 'sw' }
const showOtherEncoders = ref(false)

const RESTART_KEY = 'norishine.restartNeeded'
const ADVANCED_KEY = 'norishine.showAdvanced'
const EXPERT_KEY = 'norishine.showExpert'
const readFlag = (store, key) => { try { return store.getItem(key) === '1' } catch { return false } }
const writeFlag = (store, key, on) => { try { on ? store.setItem(key, '1') : store.removeItem(key) } catch { /* ignore */ } }

const showAdvanced = ref(readFlag(localStorage, ADVANCED_KEY))
const expertOpen = ref(readFlag(localStorage, EXPERT_KEY))
watch(expertOpen, (on) => writeFlag(localStorage, EXPERT_KEY, on))
const restartNeeded = ref(readFlag(sessionStorage, RESTART_KEY))
watch(showAdvanced, (on) => writeFlag(localStorage, ADVANCED_KEY, on))
watch(restartNeeded, (on) => writeFlag(sessionStorage, RESTART_KEY, on))

const settings = computed(() => schema.value ? schema.value.settings.filter((s) => s.available) : [])
const byKey = computed(() => Object.fromEntries((schema.value?.settings || []).map((s) => [s.key, s])))
const categoryTitle = computed(() => Object.fromEntries((schema.value?.categories || []).map((c) => [c.id, c.title])))
const expertIds = computed(() => new Set((schema.value?.categories || []).filter((c) => c.expert).map((c) => c.id)))
// Expert categories are tucked into their own sidebar group, so they show all their settings.
const visibleTier = (s) => showAdvanced.value || s.tier === 'basic' || expertIds.value.has(s.category)

const categories = computed(() => {
  const list = (schema.value?.categories || [])
    .map((c) => {
      const all = settings.value.filter((s) => s.category === c.id)
      const shown = all.filter(visibleTier)
      // A category with only advanced settings still shows them, rather than vanishing from the list.
      return { ...c, settings: shown.length ? shown : all, onlyAdvanced: !shown.length && all.length > 0 }
    })
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

// The current category's settings under their sub-headings; settings without one come first.
const currentSections = computed(() => {
  const c = currentCategory.value
  if (!c) {
    return []
  }
  const defs = (schema.value?.sections || []).filter((s) => s.category === c.id)
  const out = [{ id: '', title: '', settings: c.settings.filter((s) => !s.section || !defs.some((d) => d.id === s.section)) }]
  for (const d of defs) {
    out.push({
      ...d,
      settings: c.settings.filter((s) => s.section === d.id),
      inactive: !isDependencyMet(d, byKey.value, stored.value, draft),
    })
  }
  return out.filter((s) => s.settings.length > 0)
})
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
    api('./api/v1/status').then((s) => { activeEncoder.value = s.encoder || '' }).catch(() => { activeEncoder.value = '' })
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

// The encoder test's pick was applied: it can be undone with the recommendations.
const recommendationsRef = ref(null)
function formatSetting(key, v) {
  const s = byKey.value[key]
  return s ? formatValue(s, v, t) : String(v)
}
async function onEncoderTestApplied(restartRequired) {
  await onRecommendationChange(restartRequired)
  recommendationsRef.value?.load()
}

// A recommendation was applied or undone: take the new values as the baseline.
async function onRecommendationChange(restartRequired) {
  if (restartRequired) {
    restartNeeded.value = true
  }
  try {
    const values = await api('./api/v1/config')
    stored.value = values.values || {}
    invalid.value = values.invalid || {}
    unknown.value = values.unknown || {}
  } catch {
    // The next load shows it.
  }
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
  if (expertIds.value.has(id)) {
    expertOpen.value = true
  }
  query.value = ''
  // On a phone, opening a category is a step forward, so the back gesture returns to the list.
  if (isNarrow() && narrowList.value) {
    history.pushState({ category: true }, '', '#' + id)
  } else {
    history.replaceState(null, '', '#' + id)
  }
  narrowList.value = false
  window.scrollTo({ top: 0 })
}

function backToList() {
  if (history.state && history.state.category) {
    history.back()
  } else {
    history.replaceState(null, '', location.pathname + location.search)
    narrowList.value = true
    window.scrollTo({ top: 0 })
  }
}

function onPopState() {
  if (location.hash) {
    openFromHash()
  } else {
    narrowList.value = true
    window.scrollTo({ top: 0 })
  }
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
  narrowList.value = false
  if (expertIds.value.has(s.category)) {
    expertOpen.value = true
    if (s.category !== detectedEncoderCategory.value && isEncoderCategory(s.category)) {
      showOtherEncoders.value = true
    }
  }
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
    if (expertIds.value.has(id)) {
      expertOpen.value = true
      if (isEncoderCategory(id) && id !== detectedEncoderCategory.value) {
        showOtherEncoders.value = true
      }
    }
    category.value = id
    narrowList.value = false
  }
}

function onKey(e) {
  if (e.key === '/' && document.activeElement?.tagName !== 'INPUT' && document.activeElement?.tagName !== 'TEXTAREA') {
    e.preventDefault()
    if (!searchInput.value?.offsetParent) {
      narrowList.value = true
    }
    nextTick(() => searchInput.value?.focus())
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
  window.addEventListener('popstate', onPopState)
})
onBeforeUnmount(() => {
  window.removeEventListener('keydown', onKey)
  window.removeEventListener('beforeunload', onBeforeUnload)
  window.removeEventListener('hashchange', openFromHash)
  window.removeEventListener('popstate', onPopState)
})

// Sidebar: everyday categories first, then a collapsible Expert group. Of the encoder pages,
// only the one for the encoder this host uses is listed; the others are one click away.
const isEncoderCategory = (id) => (schema.value?.categories || []).some((c) => c.id === id && c.group === 'encoders')
const detectedEncoderCategory = computed(() => {
  const id = ENCODER_CATEGORY[activeEncoder.value]
  return id && categories.value.some((c) => c.id === id) ? id : ''
})
const navItems = computed(() => categories.value.filter((c) => !c.expert))
const expertItems = computed(() => categories.value
  .filter((c) => c.expert)
  .filter((c) => !c.group || !detectedEncoderCategory.value || showOtherEncoders.value || c.id === detectedEncoderCategory.value)
  .map((c) => (c.group && c.id === detectedEncoderCategory.value && !showOtherEncoders.value
    ? { ...c, title: c.group_title, tag: c.title }
    : c)))
const otherEncoderCount = computed(() => detectedEncoderCategory.value
  ? categories.value.filter((c) => c.group && c.id !== detectedEncoderCategory.value).length
  : 0)
const expertHasNews = computed(() => categories.value.some((c) => c.expert &&
  (categoriesWithErrors.value.has(c.id) || categoriesWithChanges.value.has(c.id))))
const pageTitle = computed(() => {
  if (searching.value) {
    return t('settings.search_results')
  }
  const c = currentCategory.value
  if (!c) {
    return t('settings.title')
  }
  return c.group && c.id === detectedEncoderCategory.value ? `${c.group_title}: ${c.title}` : c.title
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
  showKey: showAdvanced.value,
  platform: schema.value?.platform || '',
  remapGroup: remapGroup.value,
  highlighted: highlightKey.value === s.key,
  displays: displays.value,
})
</script>

<template>
  <AppShell page="config">
    <div class="page" :class="{ list: narrowList, searching }">
      <aside class="sidebar" v-if="schema">
        <div class="search">
          <i class="fas fa-magnifying-glass" aria-hidden="true"></i>
          <input ref="searchInput" v-model="query" type="search" class="field" :placeholder="$t('settings.search')"
                 :aria-label="$t('settings.search')" aria-keyshortcuts="/">
          <kbd v-if="!query" class="key-hint" :title="$t('settings.search_shortcut')">/</kbd>
        </div>
        <nav :aria-label="$t('settings.title')">
          <div class="nav-heading">{{ $t('settings.group_everyday') }}</div>
          <a v-for="item in navItems" :key="item.id" :href="'#' + item.id" class="nav-item"
             :class="{ active: !searching && currentCategory && item.id === currentCategory.id }"
             :aria-current="!searching && currentCategory && item.id === currentCategory.id ? 'page' : null"
             @click.prevent="selectCategory(item.id)">
            <span>{{ item.title }}</span>
            <span v-if="categoriesWithErrors.has(item.id)" class="dot dot-error" :title="$t('settings.not_saved')"></span>
            <span v-else-if="categoriesWithChanges.has(item.id)" class="dot" :title="$t('settings.unsaved', { n: 1 }, 1)"></span>
            <i class="fas fa-chevron-right nav-arrow" aria-hidden="true"></i>
          </a>
          <template v-if="expertItems.length">
            <button type="button" class="nav-heading nav-toggle" :aria-expanded="expertOpen ? 'true' : 'false'"
                    aria-controls="expert-categories" @click="expertOpen = !expertOpen">
              <span>{{ $t('settings.group_expert') }}</span>
              <span v-if="!expertOpen && expertHasNews" class="dot" :title="$t('settings.unsaved', { n: 1 }, 1)"></span>
              <i class="fas fa-chevron-down chevron" :class="{ open: expertOpen }" aria-hidden="true"></i>
            </button>
            <div v-show="expertOpen" id="expert-categories">
              <a v-for="item in expertItems" :key="item.id" :href="'#' + item.id" class="nav-item expert"
                 :class="{ active: !searching && currentCategory && item.id === currentCategory.id }"
                 :aria-current="!searching && currentCategory && item.id === currentCategory.id ? 'page' : null"
                 @click.prevent="selectCategory(item.id)">
                <span>{{ item.title }}</span>
                <span v-if="item.tag" class="tag">{{ item.tag }}</span>
                <span v-if="categoriesWithErrors.has(item.id)" class="dot dot-error" :title="$t('settings.not_saved')"></span>
                <span v-else-if="categoriesWithChanges.has(item.id)" class="dot" :title="$t('settings.unsaved', { n: 1 }, 1)"></span>
                <i class="fas fa-chevron-right nav-arrow" aria-hidden="true"></i>
              </a>
              <button v-if="otherEncoderCount" type="button" class="link-button small other-encoders" @click="showOtherEncoders = !showOtherEncoders">
                {{ showOtherEncoders ? $t('settings.hide_other_encoders') : $t('settings.other_encoders', { n: otherEncoderCount }, otherEncoderCount) }}
              </button>
            </div>
          </template>
        </nav>
        <button v-if="hiddenCategoryCount > 0" type="button" class="link-button small sidebar-note"
                @click="showAdvanced = true">
          {{ $t('settings.more_categories', { n: hiddenCategoryCount }, hiddenCategoryCount) }}
        </button>
        <label class="advanced-toggle">
          <span>{{ $t('settings.show_advanced') }}</span>
          <span class="switch">
            <input type="checkbox" role="switch" v-model="showAdvanced">
            <span class="track"></span>
          </span>
        </label>
      </aside>

      <main class="content">
        <div class="content-head">
          <!-- Phones: back to the category list. -->
          <button v-if="schema && !searching" type="button" class="back-button" @click="backToList">
            <i class="fas fa-chevron-left" aria-hidden="true"></i> {{ $t('settings.title') }}
          </button>
          <h1>{{ pageTitle }} <HelpLink :page="'SETTINGS.md' + (!searching && currentCategory ? '#' + currentCategory.id : '')" :label="$t('help.settings')" /></h1>
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
          <Recommendations v-if="!searching" ref="recommendationsRef" :by-key="byKey" @changed="onRecommendationChange" />
          <section v-if="!searching" class="section">
            <h2 class="card-title">
              {{ $t('optimise.section') }}
              <HelpLink page="Norishine-Guide#encoder-test" :label="$t('optimise.help')" />
            </h2>
            <div class="card">
              <EncoderTest :format="formatSetting" @changed="onEncoderTestApplied" />
            </div>
          </section>

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
            <div class="card">
              <div v-for="(text, key) in unknown" :key="key" class="unknown-row">
                <code>{{ key }}</code><code class="muted">{{ text }}</code>
              </div>
            </div>
          </section>

          <template v-else-if="currentCategory">
            <section v-for="sec in currentSections" :key="sec.id || 'main'" class="section" :class="{ inactive: sec.inactive }">
              <h2 v-if="sec.title" class="section-title">
                {{ sec.title }}
                <span v-if="sec.inactive && sec.note" class="tag" :title="sec.note">{{ $t('settings.not_used') }}</span>
              </h2>
              <div class="card">
                <SettingRow v-for="s in sec.settings" :key="s.key" v-bind="rowProps(s, false)"
                            @update="update(s, $event)" @reset="reset(s)" />
              </div>
            </section>
          </template>
        </template>
      </main>
    </div>

    <!-- Save bar -->
    <div class="savebar" v-if="changeCount > 0 || saving" role="region" :aria-label="$t('settings.save')">
      <div class="savebar-inner">
        <span class="grow">
          <strong>{{ $t('settings.unsaved', { n: changeCount }, changeCount) }}</strong>
        </span>
        <button type="button" class="btn btn-quiet" :disabled="saving" @click="discard">{{ $t('settings.discard') }}</button>
        <button v-if="needsRestart" type="button" class="btn" :disabled="saving" @click="save(false, false)">
          {{ $t('settings.save_only') }}
        </button>
        <button type="button" class="btn btn-primary" :disabled="saving" :title="$t('settings.save_shortcut')" aria-keyshortcuts="Control+S" @click="save(false, needsRestart)">
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
  display: grid; grid-template-columns: 250px minmax(0, 1fr); gap: 32px;
}
/* Norilight's settings sidebar: a panel with search at the top. */
.sidebar {
  position: sticky; top: 80px; align-self: start; max-height: calc(100vh - 100px); overflow-y: auto;
  background: var(--sidebar); border-radius: var(--radius); padding: 8px;
}
.sidebar .search { margin: 0 0 6px; }
.key-hint {
  position: absolute; right: 9px; top: 50%; transform: translateY(-50%); pointer-events: none;
  font: 12px var(--mono); color: var(--text-tertiary); border: 1px solid var(--border); border-radius: 4px; padding: 0 5px;
}
.nav-item {
  position: relative; display: flex; align-items: center; gap: 8px; min-height: 36px;
  padding: 6px 10px; border-radius: 6px; color: var(--text-primary); text-decoration: none; font-size: 14px;
}
.nav-item > span:first-child { flex: 1; }
.nav-item:hover { background: var(--nav-hover); }
.nav-item.active { background: var(--nav-selected); font-weight: 600; }
.nav-item.active::before { content: ""; position: absolute; left: 0; top: 9px; bottom: 9px; width: 3px; border-radius: 3px; background: var(--accent); }
.nav-item.expert:not(.active) { color: var(--text-secondary); }
.nav-heading {
  display: flex; align-items: center; gap: 8px; width: 100%; margin: 10px 0 2px; padding: 4px 10px;
  font-size: 12px; font-weight: 600; color: var(--text-secondary);
}
.nav-toggle { border: 0; background: none; font-family: inherit; cursor: pointer; border-radius: 6px; text-align: left; }
.nav-toggle > span:first-child { flex: 1; }
.nav-toggle:hover { background: var(--nav-hover); color: var(--text-primary); }
.chevron { font-size: 10px; transition: transform 0.12s; }
.chevron:not(.open) { transform: rotate(-90deg); }
.tag { font-size: 11.5px; font-weight: 500; padding: 1px 8px; border-radius: 999px; background: var(--nav-selected); color: var(--text-secondary); }
.nav-item.active .tag { background: var(--background); }
.other-encoders { display: block; margin: 4px 10px 2px; font-size: 13px; }
.dot { width: 8px; height: 8px; border-radius: 50%; background: var(--accent); flex: none; }
.dot-error { background: var(--error); }
.sidebar-note { margin: 10px 10px 0; font-size: 13px; }
.sidebar .advanced-toggle {
  justify-content: space-between; margin-top: 8px; padding: 10px 10px 4px; border-top: 1px solid var(--divider);
}

.content { max-width: var(--content-max); min-width: 0; }
.content-head { display: flex; flex-direction: column; gap: 2px; margin-bottom: 18px; }
.crumb { font-size: 14px; color: var(--text-secondary); }
.content-head .category-desc { margin: 6px 0 0; }
.nav-arrow { display: none; }
.back-button {
  display: none; align-self: flex-start; align-items: center; gap: 8px; min-height: 44px; margin: -6px 0 2px -10px;
  padding: 0 10px; border: 0; border-radius: 8px; background: none; color: var(--accent);
  font: inherit; font-size: 15px; font-weight: 500; cursor: pointer;
}
.back-button:hover { background: var(--nav-hover); }
.section-title { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; font-size: 14px; font-weight: 600; color: var(--text-secondary); margin: 0 0 8px; }
.section.inactive .card { opacity: 0.62; }
.section.inactive .card:focus-within, .section.inactive .card:hover { opacity: 1; }
.search { position: relative; }
.search i { position: absolute; left: 11px; top: 50%; transform: translateY(-50%); color: var(--text-tertiary); font-size: 13px; }
.search .field { padding-left: 32px; }

.link-button { border: 0; background: none; padding: 2px 0; color: var(--accent); font: inherit; cursor: pointer; font-weight: 500; }
.link-button:hover { text-decoration: underline; }
.sidebar-note { text-align: left; margin-top: 12px; padding: 0 12px; }
.advanced-toggle { display: flex; align-items: center; gap: 10px; cursor: pointer; color: var(--text-secondary); font-size: 14px; }

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

/* A floating bar, so it reads as a temporary prompt rather than part of the page. */
.savebar {
  position: fixed; left: 24px; right: 24px; bottom: 16px; z-index: 30; max-width: 1192px; margin: 0 auto;
  background: var(--popup); border: 1px solid var(--stroke-card); border-radius: 12px; box-shadow: var(--shadow);
  animation: bar-in 0.18s var(--ease-out);
}
@keyframes bar-in { from { opacity: 0; transform: translateY(12px); } }
.savebar-inner { padding: 12px 16px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
.savebar .grow { flex: 1 1 220px; }

.confirm-list { margin: 0; padding-left: 20px; font-weight: 600; }

/* Phones: the categories are a screen of their own; a category opens with a back button. */
@media (max-width: 900px) {
  .page { grid-template-columns: minmax(0, 1fr); gap: 0; padding-top: 16px; }
  .sidebar { display: none; }
  .back-button { display: inline-flex; }
  .page.list .sidebar { display: block; position: static; max-height: none; overflow: visible; padding: 10px; }
  .page.list:not(.searching) .content { display: none; }
  .page.list.searching .sidebar { margin-bottom: 18px; }
  .page.list.searching .sidebar nav,
  .page.list.searching .sidebar .advanced-toggle,
  .page.list.searching .sidebar .sidebar-note { display: none; }
  .page.list .nav-item { min-height: 48px; font-size: 16px; padding: 8px 12px; }
  .page.list .nav-item.active { background: none; font-weight: 400; }
  .page.list .nav-item.active::before { display: none; }
  .page.list .nav-arrow { display: block; font-size: 12px; color: var(--text-tertiary); }
  .page.list .nav-heading { font-size: 13px; margin-top: 14px; }
  .page.list .sidebar .advanced-toggle { min-height: 48px; padding: 10px 12px 4px; }
  .page.list .search .field { min-height: 44px; font-size: 16px; }
  .key-hint { display: none; }
}
@media (max-width: 600px) {
  .savebar { left: 0; right: 0; bottom: 0; border-radius: 12px 12px 0 0; padding-bottom: env(safe-area-inset-bottom); }
}
@media (prefers-reduced-motion: reduce) { .savebar { animation: none; } .chevron { transition: none; } }
</style>

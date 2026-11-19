<script setup>
// Encoder test (roadmap N6): POST /api/v1/optimise encodes a moving test picture with each
// preset of the host's encoder, GET shows the progress and the pick, and
// POST /api/v1/optimise/apply saves it; Undo is the recommendations' rollback.
// Used on the Settings page and in the setup wizard's encoder step.
import { computed, inject, onBeforeUnmount, onMounted, ref } from 'vue'
import { api } from './api'

const props = defineProps({
  // Formats a setting's value for display: (key, value) => text. Raw values without it.
  format: { type: Function, default: null },
})
const emit = defineEmits(['changed'])

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const state = ref(null)
const error = ref('')
const message = ref('')
const busy = ref(false)
let timer = null

const running = computed(() => state.value?.state === 'running')
const done = computed(() => state.value?.state === 'done' && state.value?.pick != null)
const results = computed(() => state.value?.results || [])
const same = computed(() => done.value && JSON.stringify(state.value.pick) === JSON.stringify(state.value.current))

function show(v) {
  if (v == null) {
    return '—'
  }
  return props.format ? props.format(state.value?.setting, v) : String(v)
}

async function load() {
  try {
    state.value = await api('./api/v1/optimise')
  } catch {
    state.value = null
  }
  if (running.value) {
    timer = setTimeout(load, 1000)
  } else if (state.value?.state === 'failed') {
    error.value = state.value.error || ''
  }
}

async function start() {
  error.value = ''
  message.value = ''
  try {
    state.value = await api('./api/v1/optimise', { method: 'POST', body: {} })
    timer = setTimeout(load, 1000)
  } catch (e) {
    if (e.status !== 401) {
      error.value = e.message
    }
  }
}

async function apply() {
  busy.value = true
  error.value = ''
  try {
    const r = await api('./api/v1/optimise/apply', { method: 'POST', body: {} })
    message.value = t('optimise.applied')
    emit('changed', !!r.restart_required)
  } catch (e) {
    if (e.status !== 401) {
      error.value = e.message
    }
  } finally {
    busy.value = false
  }
}

onMounted(load)
onBeforeUnmount(() => clearTimeout(timer))
</script>

<template>
  <div class="encoder-test">
    <div class="row">
      <div class="grow">
        <div class="name">{{ $t('optimise.title') }}</div>
        <div v-if="state && state.encoder" class="small muted">{{ state.codec }} · {{ state.width }}×{{ state.height }} · {{ state.fps }} fps</div>
      </div>
      <button type="button" class="btn btn-small" :disabled="running || busy" @click="start">
        <i class="fas" :class="running ? 'fa-spinner fa-spin' : 'fa-gauge-high'" aria-hidden="true"></i>
        {{ running ? $t('optimise.running', { progress: state.progress || 0 }) : $t('optimise.start') }}
      </button>
    </div>
    <table v-if="results.length" class="results small">
      <thead>
        <tr><th>{{ $t('optimise.preset') }}</th><th>{{ $t('optimise.p95') }}</th></tr>
      </thead>
      <tbody>
        <tr v-for="r in results" :key="JSON.stringify(r.value)" :class="{ picked: done && JSON.stringify(r.value) === JSON.stringify(state.pick) }">
          <td>{{ show(r.value) }}</td>
          <td>{{ r.error ? '—' : r.p95_ms + ' ms' }}</td>
        </tr>
      </tbody>
    </table>
    <div v-if="done" class="row">
      <div class="grow values">
        <span>{{ show(state.current) }}</span>
        <i class="fas fa-arrow-right muted" aria-hidden="true"></i>
        <strong>{{ show(state.pick) }}</strong>
      </div>
      <span v-if="same" class="small muted">{{ $t('optimise.already') }}</span>
      <button v-else type="button" class="btn btn-primary btn-small" :disabled="busy" @click="apply">{{ $t('optimise.apply') }}</button>
    </div>
    <div v-if="message" class="small" role="status">{{ message }}</div>
    <div v-if="error" class="small error" role="alert">{{ $t('optimise.failed', { error }) }}</div>
  </div>
</template>

<style scoped>
.encoder-test { display: flex; flex-direction: column; gap: 10px; }
.row { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; }
.name { font-weight: 500; }
.grow { flex: 1 1 200px; }
.values { display: inline-flex; align-items: center; gap: 8px; }
.results { border-collapse: collapse; width: 100%; max-width: 360px; }
.results th { text-align: left; font-weight: 500; color: var(--text-secondary); padding: 2px 8px 2px 0; }
.results td { padding: 2px 8px 2px 0; }
.results tr.picked td { font-weight: 600; color: var(--accent); }
.error { color: var(--error); }
</style>

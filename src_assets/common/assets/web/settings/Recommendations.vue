<script setup>
// Recommended encoder settings from what earlier streams measured (GET /api/v1/recommendations),
// with Apply and Undo. Shown only when there is something to apply or undo.
// How it decides: docs/AUTOTUNE.md, "Host settings".
import { computed, inject, onMounted, ref } from 'vue'
import { api } from '../ui/api'
import HelpLink from '../ui/HelpLink.vue'
import { formatValue } from './model'

const props = defineProps({
  byKey: { type: Object, required: true },
})
const emit = defineEmits(['changed'])

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const data = ref(null)
const busy = ref(false)
const message = ref(null)  // {kind: 'ok' | 'error', text}

const recommendations = computed(() => data.value?.recommendations || [])
const applied = computed(() => data.value?.applied || null)
const visible = computed(() => recommendations.value.length > 0 || !!applied.value || !!message.value)

function title(key) {
  return props.byKey[key]?.title || key
}
function value(key, v) {
  const s = props.byKey[key]
  return s ? formatValue(s, v, t) : String(v)
}

async function load() {
  try {
    data.value = await api('./api/v1/recommendations')
  } catch {
    // Older host or no access: no section.
    data.value = null
  }
}

async function change(path, body) {
  busy.value = true
  message.value = null
  try {
    const result = await api(path, { method: 'POST', body })
    const skipped = result.skipped || []
    message.value = skipped.length
      ? { kind: 'ok', text: t('recommend.skipped', { keys: skipped.map(title).join(', ') }) }
      : { kind: 'ok', text: path.endsWith('rollback') ? t('recommend.undone') : t('recommend.applied') }
    emit('changed', !!result.restart_required)
  } catch (e) {
    if (e.status !== 401) {
      const errors = e.body && e.body.errors ? Object.values(e.body.errors).join(' ') : e.message
      message.value = { kind: 'error', text: t('recommend.failed', { error: errors }) }
    }
  } finally {
    busy.value = false
    await load()
  }
}

const apply = (keys) => change('./api/v1/recommendations/apply', keys ? { keys } : {})
const undo = () => change('./api/v1/recommendations/rollback', {})

onMounted(load)
defineExpose({ load })
</script>

<template>
  <section v-if="visible" class="section recommend">
    <h2 class="card-title">
      {{ $t('recommend.title') }}
      <HelpLink page="AUTOTUNE.md#host-settings-norishine" :label="$t('recommend.help')" />
    </h2>
    <div class="card">
      <div v-for="r in recommendations" :key="r.key" class="rec-row">
        <div class="grow">
          <div class="rec-name">{{ title(r.key) }}</div>
          <div class="small muted">{{ $t('recommend.reason_' + r.reason) }}</div>
        </div>
        <div class="rec-values">
          <span>{{ value(r.key, r.current) }}</span>
          <i class="fas fa-arrow-right muted" aria-hidden="true"></i>
          <strong>{{ value(r.key, r.recommended) }}</strong>
        </div>
        <button type="button" class="btn btn-small" :disabled="busy" @click="apply([r.key])">{{ $t('recommend.apply') }}</button>
      </div>
      <div v-if="recommendations.length > 1" class="rec-row rec-actions">
        <span class="grow"></span>
        <button type="button" class="btn btn-primary btn-small" :disabled="busy" @click="apply()">{{ $t('recommend.apply_all') }}</button>
      </div>
      <div v-if="applied" class="rec-row">
        <div class="grow">
          <div class="rec-name">{{ $t('recommend.applied_title') }}</div>
          <div v-for="s in applied.settings" :key="s.key" class="small muted">
            {{ title(s.key) }}: {{ value(s.key, s.previous) }} → {{ value(s.key, s.value) }}
          </div>
        </div>
        <button type="button" class="btn btn-small" :disabled="busy" @click="undo">
          <i class="fas fa-rotate-left" aria-hidden="true"></i> {{ $t('recommend.undo') }}
        </button>
      </div>
      <div v-if="message" class="rec-row small" :class="{ 'rec-error': message.kind === 'error' }" role="status">
        {{ message.text }}
      </div>
    </div>
  </section>
</template>

<style scoped>
.rec-row { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; padding: 12px 0; }
.rec-row + .rec-row { border-top: 1px solid var(--stroke-divider, var(--stroke-card)); }
.rec-name { font-weight: 500; }
.rec-values { display: inline-flex; align-items: center; gap: 8px; }
.rec-error { color: var(--error); }
.grow { flex: 1 1 200px; }
</style>

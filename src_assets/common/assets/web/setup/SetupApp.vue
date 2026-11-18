<script setup>
import { computed, inject, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { api, restartHost } from '../ui/api'
import HelpLink from '../ui/HelpLink.vue'
import { P, PRESETS, presetOf, setLevel } from '../clients/permissions'

// First start, after the account is made: import, name, display, encoder, first device.
// Explanations are in docs/GUIDE.md#first-start; this page has labels only.
const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const loaded = ref(false)
const loadError = ref('')
const otherHost = ref(null)  // {name, installed}
const steps = computed(() => [...(otherHost.value ? ['import'] : []), 'name', 'display', 'encoder', 'pair', 'done'])
const index = ref(0)
const step = computed(() => steps.value[index.value])
const busy = ref(false)
const error = ref('')
let restartNeeded = false

// Import
const imported = ref(false)
async function runImport() {
  busy.value = true
  error.value = ''
  try {
    await api('./api/v1/setup/import', { method: 'POST', body: {}, timeout: 150000 })
    imported.value = true
    restartNeeded = true
    await loadSetup()
    next()
  } catch (e) {
    error.value = t('setup.import_failed', { error: e.message })
  } finally {
    busy.value = false
  }
}

// Name and display
const pcName = ref('')
const hostname = ref('')
const virtualDisplay = ref(true)
async function saveSettings(set) {
  busy.value = true
  error.value = ''
  try {
    const r = await api('./api/v1/config', { method: 'PATCH', body: { set } })
    restartNeeded ||= !!r.restart_required
    return true
  } catch (e) {
    const fields = e.body?.errors ? Object.values(e.body.errors).join(' ') : e.message
    error.value = t('setup.save_failed', { error: fields })
    return false
  } finally {
    busy.value = false
  }
}
const saveName = async () => (await saveSettings({ sunshine_name: hostname.value.trim() })) && next()
const saveDisplay = async () => (await saveSettings({ headless_mode: virtualDisplay.value })) && next()

// Encoder check
const encoder = ref(null)  // {status, encoder, h264, hevc, av1, hdr}
async function checkEncoder() {
  busy.value = true
  error.value = ''
  encoder.value = null
  try {
    encoder.value = await api('./api/v1/setup/encoder', { method: 'POST', body: {}, timeout: 60000 })
  } catch (e) {
    error.value = t('setup.encoder_failed', { error: e.message })
  } finally {
    busy.value = false
  }
}
const ENCODERS = { nvenc: 'NVIDIA NVENC', amdvce: 'AMD AMF', quicksync: 'Intel Quick Sync', software: 'Software (CPU)', vaapi: 'VA-API', videotoolbox: 'VideoToolbox' }
const encoderName = computed(() => ENCODERS[encoder.value?.encoder] || encoder.value?.encoder || '')
const codecs = computed(() => ['h264', 'hevc', 'av1', 'hdr'].map((k) => ({ key: k, ok: !!encoder.value?.[k] })))

// First device
const pending = ref([])
const selected = ref('')
const pin = ref('')
const paired = ref(null)  // the new device: {uuid, name, perm}
const preset = ref('play')
const manage = ref('none')
let pollTimer = null
async function loadPending() {
  try {
    const r = await api('./api/pin')
    pending.value = r.pairings || []
    if (!pending.value.some((p) => p.id === selected.value)) {
      selected.value = pending.value.length === 1 ? pending.value[0].id : ''
    }
  } catch { /* keep the last list */ }
}
watch(step, (s) => {
  clearInterval(pollTimer)
  error.value = ''
  if (s === 'pair' && !paired.value) {
    loadPending()
    pollTimer = setInterval(loadPending, 2000)
  }
  if (s === 'encoder' && !encoder.value) {
    checkEncoder()
  }
})
onBeforeUnmount(() => clearInterval(pollTimer))

async function pair() {
  const request = pending.value.find((p) => p.id === selected.value)
  if (!request || !/^\d{4}$/.test(pin.value)) {
    return
  }
  busy.value = true
  error.value = ''
  try {
    const r = await api('./api/pin', { method: 'POST', body: { pairing_id: request.id, pin: pin.value, name: '' } })
    if (r.status !== true) {
      error.value = t('pairing.pin_wrong')
      return
    }
    clearInterval(pollTimer)
    pin.value = ''
    // The client finishes pairing a moment after the PIN is accepted.
    for (let attempt = 0; attempt < 10 && !paired.value; attempt++) {
      const devices = (await api('./api/v1/devices')).devices || []
      const match = devices.filter((d) => d.name === request.name)
      if (match.length === 1) {
        paired.value = match[0]
        preset.value = presetOf(match[0].perm) || 'play'
      } else {
        await new Promise((resolve) => setTimeout(resolve, 500))
      }
    }
    if (!paired.value) {
      error.value = t('setup.device_not_found')
    }
  } catch (e) {
    error.value = t('pairing.pin_wrong') + ' ' + e.message
  } finally {
    busy.value = false
  }
}
async function savePermissions() {
  busy.value = true
  error.value = ''
  try {
    let perm = PRESETS.find((p) => p.id === preset.value).apply(paired.value.perm)
    const manageBits = { none: 0, read: P.manage_read, write: P.manage_read | P.manage_write }[manage.value]
    perm = setLevel(perm, P.manage, manageBits)
    await api('./api/v1/devices/' + encodeURIComponent(paired.value.uuid), { method: 'PATCH', body: { perm } })
    next()
  } catch (e) {
    error.value = t('setup.save_failed', { error: e.message })
  } finally {
    busy.value = false
  }
}

// Finish
const finishing = ref(false)
async function finish() {
  finishing.value = true
  error.value = ''
  try {
    await api('./api/v1/setup/done', { method: 'POST', body: {} })
    if (restartNeeded && !await restartHost()) {
      throw new Error(t('settings.restart_timeout'))
    }
    location.href = './'
  } catch (e) {
    error.value = e.message
    finishing.value = false
  }
}

function next() {
  error.value = ''
  index.value = Math.min(index.value + 1, steps.value.length - 1)
}
function back() {
  error.value = ''
  index.value = Math.max(index.value - 1, 0)
}

async function loadSetup() {
  const r = await api('./api/v1/setup')
  pcName.value = r.pc_name || ''
  hostname.value = r.hostname || ''
  virtualDisplay.value = !!r.headless_mode || !loaded.value
  if (!loaded.value) {
    otherHost.value = r.other_host
  }
}
onMounted(async () => {
  try {
    await loadSetup()
    loaded.value = true
  } catch (e) {
    loadError.value = e.message
  }
})
</script>

<template>
  <main class="setup">
    <div class="setup-card">
      <div class="setup-head">
        <img src="/images/logo-norishine-45.png" width="36" height="36" alt="">
        <h1>{{ $t('setup.title') }} <HelpLink page="GUIDE.md#first-start" :label="$t('help.page')" /></h1>
        <button type="button" class="btn btn-quiet btn-small skip" :disabled="finishing" @click="finish">{{ $t('setup.skip') }}</button>
      </div>

      <p v-if="loadError" class="error-text" role="alert">{{ loadError }}</p>
      <template v-else-if="loaded">
        <ol class="stepper" :aria-label="$t('setup.title')">
          <li v-for="(s, i) in steps" :key="s" :class="{ current: i === index, past: i < index }" :aria-current="i === index ? 'step' : null">
            <span class="dot">{{ i + 1 }}</span><span class="step-name">{{ $t('setup.step_' + s) }}</span>
          </li>
        </ol>

        <section v-if="step === 'import'" class="step">
          <h2>{{ $t('setup.import_title', { name: otherHost.name }) }}</h2>
          <p v-if="imported" class="notice notice-ok">{{ $t('setup.imported') }}</p>
          <div class="buttons">
            <button type="button" class="btn" :disabled="busy" @click="next">{{ $t('setup.import_skip') }}</button>
            <button type="button" class="btn btn-primary" :disabled="busy" @click="runImport">
              <i v-if="busy" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('setup.import') }}
            </button>
          </div>
        </section>

        <section v-else-if="step === 'name'" class="step">
          <h2><label for="hostname">{{ $t('setup.name_title') }}</label></h2>
          <input id="hostname" v-model="hostname" class="field" :placeholder="pcName" maxlength="64" autofocus @keydown.enter="saveName">
          <div class="buttons">
            <button v-if="index > 0" type="button" class="btn" :disabled="busy" @click="back">{{ $t('setup.back') }}</button>
            <button type="button" class="btn btn-primary" :disabled="busy" @click="saveName">{{ $t('setup.next') }}</button>
          </div>
        </section>

        <section v-else-if="step === 'display'" class="step">
          <h2 id="display-title">{{ $t('setup.display_title') }}</h2>
          <div class="choices" role="radiogroup" aria-labelledby="display-title">
            <label class="choice" :class="{ on: virtualDisplay }">
              <input v-model="virtualDisplay" type="radio" name="display" :value="true">
              <i class="fas fa-clone" aria-hidden="true"></i> {{ $t('setup.display_virtual') }}
            </label>
            <label class="choice" :class="{ on: !virtualDisplay }">
              <input v-model="virtualDisplay" type="radio" name="display" :value="false">
              <i class="fas fa-display" aria-hidden="true"></i> {{ $t('setup.display_monitor') }}
            </label>
          </div>
          <div class="buttons">
            <button type="button" class="btn" :disabled="busy" @click="back">{{ $t('setup.back') }}</button>
            <button type="button" class="btn btn-primary" :disabled="busy" @click="saveDisplay">{{ $t('setup.next') }}</button>
          </div>
        </section>

        <section v-else-if="step === 'encoder'" class="step">
          <h2>{{ $t('setup.encoder_title') }}</h2>
          <p v-if="busy" class="muted"><i class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('setup.encoder_checking') }}</p>
          <template v-else-if="encoder">
            <p v-if="encoder.status" class="encoder-name"><i class="fas fa-circle-check ok" aria-hidden="true"></i> {{ encoderName }}</p>
            <p v-else class="notice notice-error">{{ $t('setup.encoder_none') }}</p>
            <div class="chips">
              <span v-for="c in codecs" :key="c.key" class="badge" :class="c.ok ? 'badge-ok' : ''">
                <i class="fas" :class="c.ok ? 'fa-check' : 'fa-xmark'" aria-hidden="true"></i> {{ $t('setup.codec_' + c.key) }}
              </span>
            </div>
          </template>
          <div class="buttons">
            <button type="button" class="btn" :disabled="busy" @click="back">{{ $t('setup.back') }}</button>
            <button type="button" class="btn" :disabled="busy" @click="checkEncoder">{{ $t('setup.check_again') }}</button>
            <button type="button" class="btn btn-primary" :disabled="busy" @click="next">{{ $t('setup.next') }}</button>
          </div>
        </section>

        <section v-else-if="step === 'pair'" class="step">
          <h2>{{ $t('setup.pair_title') }}</h2>
          <template v-if="!paired">
            <p v-if="!pending.length" class="muted waiting"><i class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('setup.pair_waiting') }}</p>
            <form v-else class="pair-form" @submit.prevent="pair">
              <label class="field-label" for="request">{{ $t('setup.pair_device') }}</label>
              <select id="request" v-model="selected" class="field">
                <option v-if="pending.length > 1" value="" disabled>{{ $t('pairing.choose_device') }}</option>
                <option v-for="p in pending" :key="p.id" :value="p.id">{{ p.name || p.id }}</option>
              </select>
              <label class="field-label" for="pin">{{ $t('setup.pin') }}</label>
              <input id="pin" v-model="pin" class="field pin" inputmode="numeric" maxlength="4" autocomplete="off" pattern="\d{4}" required>
              <button type="submit" class="btn btn-primary" :disabled="busy || !selected || pin.length !== 4">{{ $t('setup.pair') }}</button>
            </form>
          </template>
          <template v-else>
            <p class="notice notice-ok">{{ $t('setup.paired', { name: paired.name }) }}</p>
            <h3 id="preset-title">{{ $t('devices.quick') }}</h3>
            <div class="choices" role="radiogroup" aria-labelledby="preset-title">
              <label v-for="p in PRESETS" :key="p.id" class="choice" :class="{ on: preset === p.id }">
                <input v-model="preset" type="radio" name="preset" :value="p.id"> {{ $t(p.label) }}
              </label>
            </div>
            <h3 id="manage-title">{{ $t('setup.host_management') }}</h3>
            <div class="choices" role="radiogroup" aria-labelledby="manage-title">
              <label v-for="m in ['none', 'read', 'write']" :key="m" class="choice" :class="{ on: manage === m }">
                <input v-model="manage" type="radio" name="manage" :value="m"> {{ $t('devices.manage_' + m) }}
              </label>
            </div>
          </template>
          <div class="buttons">
            <button type="button" class="btn" :disabled="busy" @click="back">{{ $t('setup.back') }}</button>
            <button v-if="!paired" type="button" class="btn" :disabled="busy" @click="next">{{ $t('setup.pair_later') }}</button>
            <button v-else type="button" class="btn btn-primary" :disabled="busy" @click="savePermissions">{{ $t('setup.next') }}</button>
          </div>
        </section>

        <section v-else-if="step === 'done'" class="step">
          <h2>{{ $t('setup.done_title') }}</h2>
          <div class="buttons">
            <button type="button" class="btn" :disabled="finishing" @click="back">{{ $t('setup.back') }}</button>
            <button type="button" class="btn btn-primary" :disabled="finishing" @click="finish">
              <i v-if="finishing" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('setup.finish') }}
            </button>
          </div>
        </section>

        <p v-if="error" class="error-text" role="alert"><i class="fas fa-circle-exclamation" aria-hidden="true"></i> {{ error }}</p>
      </template>
    </div>
  </main>
</template>

<style scoped>
.setup {
  min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 24px 16px;
  background:
    radial-gradient(1200px 500px at 50% -10%, color-mix(in srgb, var(--blush-middle) 28%, transparent), transparent),
    var(--background);
}
.setup-card {
  width: min(560px, 100%); background: var(--card); border-radius: 12px; padding: 26px; box-shadow: var(--shadow);
  display: flex; flex-direction: column; gap: 16px;
}
.setup-head { display: flex; align-items: center; gap: 12px; }
.setup-head h1 { font-size: 22px; flex: 1; }
.skip { flex: none; }
.stepper { display: flex; gap: 6px; list-style: none; margin: 0; padding: 0; flex-wrap: wrap; }
.stepper li { display: flex; align-items: center; gap: 6px; font-size: 13px; color: var(--text-tertiary); }
.stepper li + li::before { content: ""; width: 14px; height: 1px; background: var(--divider); }
.stepper .dot {
  width: 22px; height: 22px; border-radius: 50%; display: inline-flex; align-items: center; justify-content: center;
  border: 1px solid var(--border); font-size: 12px;
}
.stepper .current { color: var(--text-primary); font-weight: 600; }
.stepper .current .dot { background: var(--accent-strong); border-color: var(--accent-strong); color: var(--on-accent); }
.stepper .past .dot { border-color: var(--accent); color: var(--accent); }
@media (max-width: 520px) { .stepper li:not(.current) .step-name { display: none; } }
.step { display: flex; flex-direction: column; gap: 12px; }
.step h2 { font-size: 18px; margin: 0; }
.step h3 { font-size: 14px; margin: 6px 0 0; color: var(--text-secondary); }
.buttons { display: flex; gap: 8px; justify-content: flex-end; flex-wrap: wrap; margin-top: 6px; }
.choices { display: flex; flex-direction: column; gap: 8px; }
.choice {
  display: flex; align-items: center; gap: 10px; padding: 12px 14px; border: 1px solid var(--border);
  border-radius: 8px; cursor: pointer;
}
.choice.on { border-color: var(--accent); box-shadow: inset 0 0 0 1px var(--accent); }
.choice input { accent-color: var(--accent); }
.encoder-name { font-size: 16px; font-weight: 600; margin: 0; }
.ok { color: var(--ok, var(--accent)); }
.pair-form { display: flex; flex-direction: column; gap: 6px; }
.pair-form .btn { align-self: flex-end; margin-top: 8px; }
.pin { width: 120px; font-size: 20px; letter-spacing: 6px; text-align: center; }
.field-label { font-size: 13.5px; font-weight: 600; color: var(--text-secondary); }
.waiting { margin: 0; }
</style>

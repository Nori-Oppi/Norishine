<script setup>
import { computed, inject, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import AppShell from '../ui/AppShell.vue'
import { api } from '../ui/api'
import { notify } from '../ui/dialogs'

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const method = ref(location.hash === '#OTP' ? 'otp' : 'pin')
watch(method, (m) => history.replaceState(null, '', m === 'otp' ? '#OTP' : '#PIN'))

// PIN pairing: Moonlight and other GameStream clients show a PIN; enter it here.
const pending = ref([])
const pendingError = ref(false)
const selected = ref('')
const pin = ref('')
// The PIN as four boxes; `pin` stays the single value that is sent.
const digits = ref(['', '', '', ''])
const digitBoxes = ref([])
const pairButton = ref(null)
watch(digits, (d) => { pin.value = d.join('') }, { deep: true })

function onDigit(i, e) {
  const value = e.target.value.replace(/\D/g, '')
  if (value.length > 1) {
    // Typed over a filled box, or pasted: spread the digits from here on.
    fillFrom(i, value)
    return
  }
  digits.value[i] = value
  if (value && i < 3) {
    digitBoxes.value[i + 1]?.focus()
  } else if (value && i === 3) {
    focusPair()
  }
}
// The Pair button only enables once the fourth digit is in, so focus it after that update.
async function focusPair() {
  await nextTick()
  pairButton.value?.focus()
}
function fillFrom(i, value) {
  for (const ch of value.slice(0, 4 - i)) {
    digits.value[i++] = ch
  }
  if (i > 3) {
    focusPair()
  } else {
    digitBoxes.value[i]?.focus()
  }
}
function onDigitKey(i, e) {
  if (e.key === 'Backspace' && !digits.value[i] && i > 0) {
    e.preventDefault()
    digits.value[i - 1] = ''
    digitBoxes.value[i - 1]?.focus()
  } else if (e.key === 'ArrowLeft' && i > 0) {
    digitBoxes.value[i - 1]?.focus()
  } else if (e.key === 'ArrowRight' && i < 3) {
    digitBoxes.value[i + 1]?.focus()
  }
}
function onDigitPaste(i, e) {
  const value = (e.clipboardData?.getData('text') || '').replace(/\D/g, '')
  if (value) {
    e.preventDefault()
    fillFrom(i, value)
  }
}
const pinComplete = computed(() => /^\d{4}$/.test(pin.value))
const pinName = ref('')
const pinBusy = ref(false)
const pinResult = ref(null)  // {ok, text, name}
let pollTimer = null

async function loadPending() {
  try {
    const r = await api('./api/pin')
    pending.value = r.pairings || []
    pendingError.value = false
    // The PIN must reach the device the user picked, never another one.
    if (!pending.value.some((p) => p.id === selected.value)) {
      selected.value = pending.value.length === 1 ? pending.value[0].id : ''
    }
  } catch (e) {
    if (e.status !== 401) {
      pendingError.value = true
      pending.value = []
      selected.value = ''
    }
  }
}

async function sendPin() {
  pinResult.value = null
  if (!selected.value) {
    pinResult.value = { ok: false, text: t('pairing.choose_device') }
    return
  }
  pinBusy.value = true
  const request = pending.value.find((p) => p.id === selected.value)
  const name = pinName.value.trim() || request?.name || ''
  try {
    const r = await api('./api/pin', { method: 'POST', body: { pairing_id: selected.value, pin: pin.value, name: pinName.value } })
    if (r.status === true) {
      pinResult.value = { ok: true, text: t('pairing.paired'), name }
      digits.value = ['', '', '', '']
      pinName.value = ''
    } else {
      pinResult.value = { ok: false, text: t('pairing.pin_wrong') }
    }
  } catch {
    pinResult.value = { ok: false, text: t('pairing.pin_wrong') }
  } finally {
    pinBusy.value = false
    loadPending()
  }
}

// The Devices page opens the editor for "#<uuid>". The client finishes pairing a moment after
// the PIN is accepted, so look the new device up (by name) when asked, waiting briefly for it.
const opening = ref(false)
async function openNewDevice() {
  opening.value = true
  let href = './clients'
  for (let attempt = 0; attempt < 6; attempt++) {
    try {
      const r = await api('./api/v1/devices')
      const matches = (r.devices || []).filter((d) => d.name === pinResult.value?.name)
      if (matches.length === 1) {
        href = './clients#' + encodeURIComponent(matches[0].uuid)
        break
      }
    } catch {
      break
    }
    await new Promise((resolve) => setTimeout(resolve, 500))
  }
  location.href = href
}

async function cancelPending(id) {
  if (!id) {
    return
  }
  try {
    const r = await api('./api/pin', { method: 'DELETE', body: { pairing_id: id } })
    notify(r.status ? t('pairing.cancelled') : t('pairing.cancel_failed'), r.status ? 'ok' : 'error')
  } catch {
    notify(t('pairing.cancel_failed'), 'error')
  }
  loadPending()
}

// One-time PIN pairing (Artemis): the host makes a PIN the client can use with a passphrase.
const passphrase = ref('')
const otpName = ref('')
const otp = ref('')
const otpBusy = ref(false)
const otpExpired = ref(false)
const hostAddr = ref('')
const hostPort = ref('')
const hostName = ref('')
const editingHost = ref(false)
const qrBox = ref(null)
let expiryTimer = null

const deepLink = computed(() => encodeURI(`art://${hostAddr.value}:${hostPort.value}?pin=${otp.value}&passphrase=${passphrase.value}&name=${hostName.value}`))
const localPage = ['localhost', '127.0.0.1', '[::1]'].includes(location.hostname)

async function requestOtp() {
  otpBusy.value = true
  otpExpired.value = false
  try {
    const r = await api('./api/otp', { method: 'POST', body: { passphrase: passphrase.value, deviceName: otpName.value } })
    if (!r.status) {
      notify(r.message || t('pairing.otp_failed'), 'error')
      return
    }
    otp.value = r.otp
    hostName.value = r.name
    if (!hostAddr.value) {
      hostAddr.value = localPage ? r.ip : location.hostname
      hostPort.value = String(parseInt(location.port || '47990', 10) - 1)
    }
    await drawQr()
    clearTimeout(expiryTimer)
    expiryTimer = setTimeout(() => {
      otp.value = ''
      otpExpired.value = true
      drawQr()
    }, 3 * 60 * 1000)
  } catch (e) {
    notify(t('pairing.otp_failed') + ' ' + e.message, 'error')
  } finally {
    otpBusy.value = false
  }
}

let qr = null
async function drawQr() {
  await nextTick()
  if (!qrBox.value || typeof window.QRCode === 'undefined') {
    return
  }
  if (!qr) {
    qr = new window.QRCode(qrBox.value, { width: 180, height: 180 })
  }
  qr.clear()
  if (otp.value && hostAddr.value) {
    qr.makeCode(deepLink.value)
  }
}
watch([hostAddr, hostPort], () => drawQr())

onMounted(() => {
  loadPending()
  pollTimer = setInterval(() => {
    if (method.value === 'pin') {
      loadPending()
    }
  }, 2000)
})
onBeforeUnmount(() => {
  clearInterval(pollTimer)
  clearTimeout(expiryTimer)
})
</script>

<template>
  <AppShell page="pin">
    <main class="page-main narrow">
      <div class="page-head">
        <div>
          <h1>{{ $t('pairing.title') }}</h1>
          <p class="muted">{{ $t('pairing.intro') }}</p>
        </div>
      </div>

      <div class="tabs" role="tablist">
        <button type="button" role="tab" :aria-selected="method === 'pin'" :class="{ active: method === 'pin' }" @click="method = 'pin'">
          {{ $t('pairing.pin_tab') }}
        </button>
        <button type="button" role="tab" :aria-selected="method === 'otp'" :class="{ active: method === 'otp' }" @click="method = 'otp'">
          {{ $t('pairing.otp_tab') }}
        </button>
      </div>

      <template v-if="method === 'pin'">
        <div v-if="pinResult && pinResult.ok" class="notice notice-ok section" role="status">
          <span class="grow">{{ pinResult.text }}</span>
          <a class="btn btn-small btn-primary" href="./clients" :aria-busy="opening ? 'true' : null" @click.prevent="openNewDevice">
            <i v-if="opening" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('pairing.set_permissions') }}
          </a>
        </div>

        <!-- Nothing asking yet: say what to do on the device, and that this page is listening. -->
        <div v-if="!pending.length" class="card-pad listen" role="status">
          <span class="ring" :class="{ error: pendingError }" aria-hidden="true"><i class="fas" :class="pendingError ? 'fa-triangle-exclamation' : 'fa-link'"></i></span>
          <div>
            <h2>{{ pendingError ? $t('pairing.load_failed') : $t('pairing.waiting_title') }}</h2>
            <p class="muted">{{ $t('pairing.pin_steps') }}</p>
            <p class="small muted">{{ $t('pairing.waiting_help') }}</p>
          </div>
        </div>

        <form v-else class="card-pad form-grid" @submit.prevent="sendPin">
          <fieldset class="requests">
            <legend class="form-label">{{ pending.length > 1 ? $t('pairing.choose_device') : $t('pairing.device') }}</legend>
            <label v-for="p in pending" :key="p.id" class="request" :class="{ active: selected === p.id }">
              <input v-model="selected" type="radio" name="pairing-request" :value="p.id">
              <span class="avatar" aria-hidden="true"><i class="fas fa-display"></i></span>
              <span class="grow">
                <span class="row-title">{{ p.name || $t('pairing.unknown_device') }}</span>
                <span class="row-sub mono">{{ p.address || $t('pairing.unknown_address') }}</span>
              </span>
              <button type="button" class="btn btn-icon" :title="$t('pairing.cancel_request')" @click.prevent="cancelPending(p.id)">
                <i class="fas fa-xmark" aria-hidden="true"></i><span class="visually-hidden">{{ $t('pairing.cancel_request') }}</span>
              </button>
            </label>
          </fieldset>
          <div class="pin-row">
            <fieldset class="pin-field">
              <legend class="form-label">{{ $t('pairing.pin_label') }}</legend>
              <div class="pins">
                <input v-for="(d, i) in digits" :key="i" :ref="(el) => { digitBoxes[i] = el }" :value="d" class="field pin-box"
                       inputmode="numeric" autocomplete="one-time-code" maxlength="2" :aria-label="$t('pairing.digit', { n: i + 1 })"
                       @input="onDigit(i, $event)" @keydown="onDigitKey(i, $event)" @paste="onDigitPaste(i, $event)" @focus="$event.target.select()">
              </div>
            </fieldset>
            <div class="form-field name-field">
              <label for="pin-name">{{ $t('pairing.name') }}</label>
              <input id="pin-name" v-model="pinName" class="field" maxlength="128" :placeholder="$t('pairing.name_placeholder')">
            </div>
            <button ref="pairButton" type="submit" class="btn btn-primary" :disabled="!selected || !pinComplete || pinBusy">
              <i v-if="pinBusy" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('pairing.pair') }}
            </button>
          </div>
          <div v-if="pinResult && !pinResult.ok" class="notice notice-error" role="status">
            <span class="grow">{{ pinResult.text }}</span>
          </div>
        </form>
      </template>

      <form v-else class="card-pad form-grid" @submit.prevent="requestOtp">
        <p class="muted step">{{ $t('pairing.otp_steps') }}</p>
        <div class="two">
          <div class="form-field">
            <label for="passphrase">{{ $t('pairing.passphrase') }}</label>
            <input id="passphrase" v-model="passphrase" class="field" pattern="[0-9a-zA-Z]{4,}" required autocomplete="off">
            <p class="form-help">{{ $t('pairing.passphrase_help') }}</p>
          </div>
          <div class="form-field">
            <label for="otp-name">{{ $t('pairing.name') }}</label>
            <input id="otp-name" v-model="otpName" class="field" :placeholder="$t('pairing.name_placeholder')">
          </div>
        </div>
        <div>
          <button type="submit" class="btn btn-primary" :disabled="otpBusy">{{ $t('pairing.make_pin') }}</button>
        </div>

        <div v-show="otp || otpExpired" class="otp-result">
          <div ref="qrBox" class="qr" v-show="otp && hostAddr"></div>
          <div class="otp-text">
            <div class="otp-pin" :class="{ expired: otpExpired }">{{ otp || $t('pairing.expired') }}</div>
            <p class="muted small">{{ otpExpired ? $t('pairing.expired_help') : $t('pairing.otp_help') }}</p>
            <a v-if="otp && hostAddr" class="small mono" :href="deepLink">art://{{ hostAddr }}:{{ hostPort }}</a>
            <button v-if="otp" type="button" class="link-button small" @click="editingHost = !editingHost">{{ $t('pairing.edit_address') }}</button>
            <div v-if="editingHost" class="two">
              <input v-model="hostAddr" class="field" :aria-label="$t('pairing.address')" :placeholder="$t('pairing.address')">
              <input v-model="hostPort" class="field" :aria-label="$t('pairing.port')" :placeholder="$t('pairing.port')" inputmode="numeric">
            </div>
          </div>
        </div>
        <p class="form-help">{{ $t('pairing.otp_clients') }}</p>
      </form>

      <p class="notice notice-warning warn">{{ $t('pairing.warning') }}</p>
    </main>
  </AppShell>
</template>

<style scoped>
.page-main.narrow { max-width: 720px; }
/* Norilight-style tabs: an underline marks the current one. */
.tabs { display: flex; gap: 4px; border-bottom: 1px solid var(--divider); margin-bottom: 18px; }
.tabs button {
  position: relative; border: 0; background: none; color: var(--text-secondary); font: inherit;
  padding: 9px 14px; cursor: pointer; border-radius: 6px 6px 0 0;
}
.tabs button:hover { color: var(--text-primary); background: var(--nav-hover); }
.tabs button.active { color: var(--text-heading); font-weight: 600; }
.tabs button.active::after {
  content: ""; position: absolute; left: 14px; right: 14px; bottom: -1px; height: 3px; border-radius: 3px 3px 0 0; background: var(--accent);
}
.listen { display: flex; gap: 22px; align-items: center; padding: 24px 20px; }
.listen h2 { margin-bottom: 4px; }
.listen p { margin: 4px 0 0; }
.ring {
  width: 64px; height: 64px; flex: none; border-radius: 50%; display: flex; align-items: center; justify-content: center;
  color: var(--accent); font-size: 24px; background: color-mix(in srgb, var(--accent) 12%, transparent);
  animation: pulse 2.2s ease-out infinite;
}
.ring.error { color: var(--warning); background: var(--warning-bg); animation: none; }
@keyframes pulse {
  0% { box-shadow: 0 0 0 0 color-mix(in srgb, var(--accent) 30%, transparent); }
  80%, 100% { box-shadow: 0 0 0 18px transparent; }
}
.requests, .pin-field { border: 0; margin: 0; padding: 0; min-width: 0; }
.requests legend, .pin-field legend { padding: 0; margin-bottom: 6px; }
.requests { display: flex; flex-direction: column; gap: 8px; }
.request {
  display: flex; align-items: center; gap: 14px; padding: 10px 10px 10px 14px; border-radius: 8px; cursor: pointer;
  border: 1px solid var(--border); background: var(--card);
}
.request input { position: absolute; opacity: 0; pointer-events: none; }
.request.active { border-color: var(--accent); background: color-mix(in srgb, var(--accent) 11%, var(--card)); }
.request:focus-within { outline: 2px solid var(--accent); outline-offset: 2px; }
.request .grow { flex: 1; display: flex; flex-direction: column; min-width: 0; }
.avatar {
  width: 40px; height: 40px; border-radius: 50%; flex: none; background: var(--nav-selected); color: var(--accent);
  display: inline-flex; align-items: center; justify-content: center; font-size: 16px;
}
.pin-row { display: flex; gap: 20px; align-items: flex-end; flex-wrap: wrap; }
.pins { display: flex; gap: 8px; }
.pin-box {
  width: 48px; height: 56px; padding: 0; text-align: center; font-family: var(--mono); font-size: 24px; color: var(--text-heading);
}
.name-field { flex: 1 1 200px; }
@media (prefers-reduced-motion: reduce) { .ring { animation: none; } }
.step { margin: 0; }
.two { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.otp-result { display: flex; gap: 20px; align-items: center; flex-wrap: wrap; }
.qr { background: #fff; padding: 10px; border-radius: 8px; line-height: 0; }
.otp-text { display: flex; flex-direction: column; gap: 6px; align-items: flex-start; }
.otp-pin { font-size: 40px; font-weight: 700; letter-spacing: 0.15em; font-family: var(--mono); color: var(--text-heading); }
.otp-pin.expired { color: var(--text-disabled); font-size: 24px; letter-spacing: 0; }
.link-button { border: 0; background: none; padding: 0; color: var(--accent); font: inherit; cursor: pointer; }
.warn { margin-top: 18px; }
@media (max-width: 560px) { .two { grid-template-columns: 1fr; } }
</style>

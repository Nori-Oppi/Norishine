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
const pinName = ref('')
const pinBusy = ref(false)
const pinResult = ref(null)  // {ok, text}
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
  try {
    const r = await api('./api/pin', { method: 'POST', body: { pairing_id: selected.value, pin: pin.value, name: pinName.value } })
    if (r.status === true) {
      pinResult.value = { ok: true, text: t('pairing.paired') }
      pin.value = ''
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

async function cancelPending() {
  if (!selected.value) {
    return
  }
  try {
    const r = await api('./api/pin', { method: 'DELETE', body: { pairing_id: selected.value } })
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

      <div class="segmented" role="tablist">
        <button type="button" role="tab" :aria-selected="method === 'pin'" :class="{ active: method === 'pin' }" @click="method = 'pin'">
          {{ $t('pairing.pin_tab') }}
        </button>
        <button type="button" role="tab" :aria-selected="method === 'otp'" :class="{ active: method === 'otp' }" @click="method = 'otp'">
          {{ $t('pairing.otp_tab') }}
        </button>
      </div>

      <form v-if="method === 'pin'" class="card-pad form-grid" @submit.prevent="sendPin">
        <p class="muted step">{{ $t('pairing.pin_steps') }}</p>
        <div class="form-field">
          <label for="pairing-device">{{ $t('pairing.device') }}</label>
          <div class="device-row">
            <select id="pairing-device" v-model="selected" class="field" :disabled="!pending.length">
              <option v-if="!pending.length" value="">{{ pendingError ? $t('pairing.load_failed') : $t('pairing.waiting') }}</option>
              <option v-else-if="pending.length > 1" value="" disabled>{{ $t('pairing.choose_device') }}</option>
              <option v-for="p in pending" :key="p.id" :value="p.id">
                {{ p.name || $t('pairing.unknown_device') }} ({{ p.address || $t('pairing.unknown_address') }})
              </option>
            </select>
            <button type="button" class="btn btn-icon" :disabled="!selected" :title="$t('pairing.cancel_request')" @click="cancelPending">
              <i class="fas fa-xmark" aria-hidden="true"></i><span class="visually-hidden">{{ $t('pairing.cancel_request') }}</span>
            </button>
          </div>
          <p v-if="!pending.length && !pendingError" class="form-help"><i class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('pairing.waiting_help') }}</p>
        </div>
        <div class="two">
          <div class="form-field">
            <label for="pin">{{ $t('pairing.pin') }}</label>
            <input id="pin" v-model="pin" class="field pin-input" inputmode="numeric" pattern="\d{4}" maxlength="4" required autocomplete="off">
          </div>
          <div class="form-field">
            <label for="pin-name">{{ $t('pairing.name') }}</label>
            <input id="pin-name" v-model="pinName" class="field" maxlength="128" :placeholder="$t('pairing.name_placeholder')">
          </div>
        </div>
        <div>
          <button type="submit" class="btn btn-primary" :disabled="!selected || pinBusy">
            <i v-if="pinBusy" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('pairing.pair') }}
          </button>
        </div>
        <div v-if="pinResult" class="notice" :class="pinResult.ok ? 'notice-ok' : 'notice-error'" role="status">
          <span class="grow">{{ pinResult.text }}</span>
          <a v-if="pinResult.ok" class="btn btn-small" href="./clients">{{ $t('pairing.set_permissions') }}</a>
        </div>
      </form>

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
.segmented { display: inline-flex; background: var(--card); border-radius: 8px; padding: 3px; margin-bottom: 14px; }
.segmented button {
  border: 0; background: none; color: var(--text-secondary); font: inherit; font-weight: 500;
  padding: 7px 16px; border-radius: 6px; cursor: pointer;
}
.segmented button.active { background: var(--accent-strong); color: var(--on-accent); }
.step { margin: 0; }
.two { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.device-row { display: flex; gap: 8px; }
.pin-input { font-size: 22px; letter-spacing: 0.3em; font-family: var(--mono); max-width: 160px; }
.otp-result { display: flex; gap: 20px; align-items: center; flex-wrap: wrap; }
.qr { background: #fff; padding: 10px; border-radius: 8px; line-height: 0; }
.otp-text { display: flex; flex-direction: column; gap: 6px; align-items: flex-start; }
.otp-pin { font-size: 40px; font-weight: 700; letter-spacing: 0.15em; font-family: var(--mono); color: var(--text-heading); }
.otp-pin.expired { color: var(--text-disabled); font-size: 24px; letter-spacing: 0; }
.link-button { border: 0; background: none; padding: 0; color: var(--accent); font: inherit; cursor: pointer; }
.warn { margin-top: 18px; }
@media (max-width: 560px) { .two { grid-template-columns: 1fr; } }
</style>

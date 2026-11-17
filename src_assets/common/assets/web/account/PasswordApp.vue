<script setup>
import { inject, ref } from 'vue'
import AppShell from '../ui/AppShell.vue'

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const currentUsername = ref('')
const currentPassword = ref('')
const newUsername = ref('')
const newPassword = ref('')
const confirm = ref('')
const error = ref('')
const done = ref(false)
const busy = ref(false)

async function save() {
  error.value = ''
  if (newPassword.value !== confirm.value) {
    error.value = t('account.mismatch')
    return
  }
  if (newPassword.value.length < 8) {
    error.value = t('account.too_short')
    return
  }
  busy.value = true
  try {
    const r = await fetch('./api/password', {
      method: 'POST',
      credentials: 'include',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        currentUsername: currentUsername.value,
        currentPassword: currentPassword.value,
        newUsername: newUsername.value,
        newPassword: newPassword.value,
        confirmNewPassword: confirm.value,
      }),
    })
    let body = {}
    try { body = await r.json() } catch { /* not JSON */ }
    if (!r.ok || body.status !== true) {
      error.value = body.error === 'Invalid Current Credentials' ? t('account.wrong_current') : (body.error || t('account.failed', { status: r.status }))
      return
    }
    done.value = true
    // Every session ends with a password change; sign in again.
    setTimeout(() => { location.href = './login' }, 2500)
  } catch {
    error.value = t('account.unreachable')
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <AppShell page="password">
    <main class="page-main narrow">
      <div class="page-head">
        <div>
          <h1>{{ $t('account.change_title') }}</h1>
        </div>
      </div>
      <form class="card-pad form-grid" @submit.prevent="save">
        <div class="two">
          <div class="form-field">
            <label for="cur-user">{{ $t('account.current_username') }}</label>
            <input id="cur-user" v-model="currentUsername" class="field" autocomplete="username" required>
          </div>
          <div class="form-field">
            <label for="cur-pass">{{ $t('account.current_password') }}</label>
            <input id="cur-pass" v-model="currentPassword" class="field" type="password" autocomplete="current-password" required>
          </div>
        </div>
        <div class="form-field">
          <label for="new-user">{{ $t('account.new_username') }}</label>
          <input id="new-user" v-model="newUsername" class="field" autocomplete="off" :placeholder="$t('account.new_username_placeholder')">
        </div>
        <div class="two">
          <div class="form-field">
            <label for="new-pass">{{ $t('account.new_password') }}</label>
            <input id="new-pass" v-model="newPassword" class="field" type="password" autocomplete="new-password" required>
          </div>
          <div class="form-field">
            <label for="confirm-pass">{{ $t('account.confirm') }}</label>
            <input id="confirm-pass" v-model="confirm" class="field" type="password" autocomplete="new-password" required>
          </div>
        </div>
        <p v-if="error" class="error-text" role="alert"><i class="fas fa-circle-exclamation" aria-hidden="true"></i> {{ error }}</p>
        <p v-if="done" class="notice notice-ok" role="status">{{ $t('account.changed') }}</p>
        <div><button type="submit" class="btn btn-primary" :disabled="busy || done">{{ $t('account.change') }}</button></div>
      </form>
      <p class="small muted note"><a href="https://github.com/Nori-Oppi/Norishine/blob/master/docs/GUIDE.md#forgot-the-password" target="_blank" rel="noopener">{{ $t('account.forgot_link') }}</a></p>
    </main>
  </AppShell>
</template>

<style scoped>
.page-main.narrow { max-width: 720px; }
.two { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.note { margin-top: 14px; }
@media (max-width: 560px) { .two { grid-template-columns: 1fr; } }
</style>

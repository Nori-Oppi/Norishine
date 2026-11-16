<script setup>
import { computed, inject, onMounted, ref } from 'vue'

// Sign-in page, and the first-run page that creates the web UI account.
const props = defineProps({
  mode: { type: String, default: 'login' },  // "login" or "welcome"
})

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const username = ref(props.mode === 'welcome' ? 'admin' : '')
const password = ref('')
const confirm = ref('')
const busy = ref(false)
const error = ref('')
const welcome = computed(() => props.mode === 'welcome')

onMounted(() => {
  // Older versions could keep the password in this browser; remove it.
  try { localStorage.removeItem('login') } catch { /* ignore */ }
})

// Only go back to a page on this host.
function destination() {
  const url = new URL(location.href)
  const redir = url.searchParams.get('redir')
  if (redir) {
    try {
      const target = new URL(redir, location.href)
      if (target.origin === location.origin && !target.pathname.startsWith('/login')) {
        // The server's redirect keeps the page's #fragment on the login URL, not in redir.
        return target.pathname + target.search + (target.hash || url.hash)
      }
    } catch { /* fall through */ }
  }
  return './' + url.hash
}

async function signIn() {
  const r = await fetch('./api/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username: username.value, password: password.value }),
  })
  if (r.ok) {
    location.href = destination()
    return
  }
  let message = ''
  try { message = (await r.json()).error } catch { /* not JSON */ }
  if (r.status === 401) {
    error.value = t('account.wrong')
  } else if (r.status === 429) {
    error.value = message || t('account.locked')
  } else {
    error.value = message || t('account.failed', { status: r.status })
  }
}

async function submit() {
  error.value = ''
  if (welcome.value) {
    if (password.value !== confirm.value) {
      error.value = t('account.mismatch')
      return
    }
    if (password.value.length < 8) {
      error.value = t('account.too_short')
      return
    }
  }
  busy.value = true
  try {
    if (welcome.value) {
      const r = await fetch('./api/password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ newUsername: username.value, newPassword: password.value, confirmNewPassword: confirm.value }),
      })
      if (!r.ok) {
        let message = ''
        try { message = (await r.json()).error } catch { /* not JSON */ }
        error.value = message || t('account.failed', { status: r.status })
        return
      }
    }
    await signIn()
  } catch {
    error.value = t('account.unreachable')
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <main class="auth">
    <form class="auth-card" @submit.prevent="submit">
      <div class="auth-head">
        <img src="/images/logo-norishine-45.png" width="40" height="40" alt="">
        <h1>{{ welcome ? $t('account.welcome_title') : $t('account.title') }}</h1>
      </div>
      <p v-if="welcome" class="muted">{{ $t('account.welcome_text') }}</p>

      <label class="field-label" for="username">{{ $t('_common.username') }}</label>
      <input id="username" v-model="username" class="field" autocomplete="username" required autofocus>

      <label class="field-label" for="password">{{ $t('_common.password') }}</label>
      <input id="password" v-model="password" class="field" type="password"
             :autocomplete="welcome ? 'new-password' : 'current-password'" required>

      <template v-if="welcome">
        <label class="field-label" for="confirm">{{ $t('account.confirm') }}</label>
        <input id="confirm" v-model="confirm" class="field" type="password" autocomplete="new-password" required>
      </template>

      <p v-if="error" class="error-text auth-error" role="alert">
        <i class="fas fa-circle-exclamation" aria-hidden="true"></i> {{ error }}
      </p>

      <button type="submit" class="btn btn-primary auth-submit" :disabled="busy">
        <i v-if="busy" class="fas fa-spinner fa-spin" aria-hidden="true"></i>
        {{ welcome ? $t('account.create') : $t('account.sign_in') }}
      </button>
    </form>
  </main>
</template>

<style scoped>
.auth {
  min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 24px 16px;
  background:
    radial-gradient(1200px 500px at 50% -10%, color-mix(in srgb, var(--blush-middle) 28%, transparent), transparent),
    var(--background);
}
.auth-card {
  width: min(400px, 100%); background: var(--card); border-radius: 12px; padding: 28px 26px;
  display: flex; flex-direction: column; gap: 6px; box-shadow: var(--shadow);
}
.auth-head { display: flex; align-items: center; gap: 12px; margin-bottom: 10px; }
.auth-head h1 { font-size: 22px; }
.auth-card p { margin: 0 0 8px; }
.field-label { font-size: 13.5px; font-weight: 600; color: var(--text-secondary); margin-top: 8px; }
.auth-error { margin: 10px 0 0; }
.auth-submit { margin-top: 16px; min-height: 40px; }
</style>

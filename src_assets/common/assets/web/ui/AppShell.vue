<script setup>
import { nextTick, ref, watch } from 'vue'
import { storedTheme, setTheme } from './theme'
import { answerConfirm, dialogState, dismiss } from './dialogs'

defineProps({
  // The current page, to highlight it in the navigation.
  page: { type: String, required: true },
})

const links = [
  { href: './', page: 'home', icon: 'fa-house', label: 'navbar.home' },
  { href: './pin', page: 'pin', icon: 'fa-link', label: 'navbar.pin' },
  { href: './clients', page: 'clients', icon: 'fa-mobile-screen', label: 'navbar.clients' },
  { href: './apps', page: 'apps', icon: 'fa-table-cells-large', label: 'navbar.applications' },
  { href: './config', page: 'config', icon: 'fa-sliders', label: 'navbar.configuration' },
  { href: './troubleshooting', page: 'troubleshooting', icon: 'fa-stethoscope', label: 'navbar.troubleshoot' },
]

const themes = ['auto', 'light', 'dark']
const theme = ref(storedTheme())
const themeIcon = { auto: 'fa-circle-half-stroke', light: 'fa-sun', dark: 'fa-moon' }

function nextTheme() {
  theme.value = themes[(themes.indexOf(theme.value) + 1) % themes.length]
  setTheme(theme.value)
}

async function signOut() {
  try {
    await fetch('./api/logout', { method: 'POST', credentials: 'include' })
  } finally {
    location.href = './login'
  }
}

// Focus the safe choice when a confirmation opens.
const cancelButton = ref(null)
watch(() => dialogState.confirm, async (c) => {
  if (c) {
    await nextTick()
    cancelButton.value?.focus()
  }
})
function onKey(e) {
  if (e.key === 'Escape' && dialogState.confirm) {
    answerConfirm(false)
  }
}
window.addEventListener('keydown', onKey)
</script>

<template>
  <div class="shell">
    <header class="topbar">
      <div class="topbar-inner">
        <a class="brand" href="./">
          <img src="/images/logo-norishine-45.png" width="30" height="30" alt="">
          <span>Norishine</span>
        </a>
        <nav class="nav" aria-label="Main">
          <a v-for="link in links" :key="link.page" :href="link.href" class="nav-link"
             :class="{ active: link.page === page }" :aria-current="link.page === page ? 'page' : null">
            <i class="fas fa-fw" :class="link.icon" aria-hidden="true"></i>
            <span>{{ $t(link.label) }}</span>
          </a>
        </nav>
        <a href="./password" class="icon-button" :class="{ active: page === 'password' }" :title="$t('navbar.password')">
          <i class="fas fa-fw fa-user-shield" aria-hidden="true"></i>
          <span class="visually-hidden">{{ $t('navbar.password') }}</span>
        </a>
        <button type="button" class="icon-button" @click="nextTheme"
                :title="$t('navbar.toggle_theme') + ': ' + $t('navbar.theme_' + theme)">
          <i class="fas fa-fw" :class="themeIcon[theme]" aria-hidden="true"></i>
          <span class="visually-hidden">{{ $t('navbar.toggle_theme') }}: {{ $t('navbar.theme_' + theme) }}</span>
        </button>
        <button type="button" class="icon-button" @click="signOut" :title="$t('navbar.logout')">
          <i class="fas fa-fw fa-right-from-bracket" aria-hidden="true"></i>
          <span class="visually-hidden">{{ $t('navbar.logout') }}</span>
        </button>
      </div>
    </header>
    <slot></slot>

    <div class="toasts" aria-live="polite">
      <div v-for="toast in dialogState.toasts" :key="toast.id" class="notice toast"
           :class="{ 'notice-ok': toast.kind === 'ok', 'notice-error': toast.kind === 'error', 'notice-warning': toast.kind === 'warning' }"
           :role="toast.kind === 'error' ? 'alert' : 'status'">
        <span class="grow">{{ toast.text }}</span>
        <button type="button" class="btn btn-icon" :title="$t('_common.dismiss')" @click="dismiss(toast.id)">
          <i class="fas fa-xmark" aria-hidden="true"></i><span class="visually-hidden">{{ $t('_common.dismiss') }}</span>
        </button>
      </div>
    </div>

    <div v-if="dialogState.confirm" class="dialog-backdrop" @click.self="answerConfirm(false)">
      <div class="dialog" role="alertdialog" aria-modal="true" aria-labelledby="confirm-title" aria-describedby="confirm-body">
        <h2 id="confirm-title">{{ dialogState.confirm.title }}</h2>
        <p v-if="dialogState.confirm.body" id="confirm-body" class="muted confirm-body">{{ dialogState.confirm.body }}</p>
        <div class="actions">
          <button ref="cancelButton" type="button" class="btn" @click="answerConfirm(false)">{{ $t('settings.cancel') }}</button>
          <button type="button" class="btn" :class="dialogState.confirm.danger ? 'btn-danger-solid' : 'btn-primary'"
                  @click="answerConfirm(true)">{{ dialogState.confirm.confirm }}</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.topbar {
  background: linear-gradient(90deg, var(--blush-start), var(--blush-middle) 55%, var(--blush-end));
  color: #fff;
  position: sticky; top: 0; z-index: 20;
}
.topbar-inner {
  max-width: 1240px; margin: 0 auto; padding: 0 16px;
  display: flex; align-items: center; gap: 8px; min-height: 56px;
}
.brand {
  display: flex; align-items: center; gap: 10px; color: #fff; text-decoration: none;
  font-weight: 600; font-size: 17px; flex: none; margin-right: 10px;
}
.nav { display: flex; gap: 2px; flex: 1; overflow-x: auto; scrollbar-width: none; }
.nav::-webkit-scrollbar { display: none; }
.nav-link {
  display: flex; align-items: center; gap: 7px; padding: 7px 11px; border-radius: 6px;
  color: rgba(255, 255, 255, 0.86); text-decoration: none; white-space: nowrap; font-size: 14.5px;
}
.nav-link:hover { background: rgba(255, 255, 255, 0.12); color: #fff; }
.nav-link.active { background: rgba(255, 255, 255, 0.2); color: #fff; font-weight: 600; }
.icon-button {
  flex: none; width: 36px; height: 36px; border-radius: 6px; border: 0; cursor: pointer;
  background: rgba(255, 255, 255, 0.12); color: #fff;
  display: inline-flex; align-items: center; justify-content: center; text-decoration: none;
}
.icon-button:hover, .icon-button.active { background: rgba(255, 255, 255, 0.26); }
.toasts {
  position: fixed; right: 16px; bottom: 16px; z-index: 40; display: flex; flex-direction: column; gap: 8px;
  width: min(420px, calc(100vw - 32px));
}
.toast { box-shadow: var(--shadow); }
.confirm-body { white-space: pre-line; }
@media (max-width: 860px) {
  .brand span { display: none; }
  .nav-link span { display: none; }
  .nav-link.active span { display: inline; }
}
</style>

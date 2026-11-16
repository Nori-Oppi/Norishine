<script setup>
import { ref } from 'vue'
import { storedTheme, setTheme } from './theme'

defineProps({
  // Path of the current page, to highlight it in the navigation.
  page: { type: String, required: true },
})

const links = [
  { href: './', page: 'home', icon: 'fa-house', label: 'navbar.home' },
  { href: './pin', page: 'pin', icon: 'fa-link', label: 'navbar.pin' },
  { href: './apps', page: 'apps', icon: 'fa-table-cells-large', label: 'navbar.applications' },
  { href: './config', page: 'config', icon: 'fa-sliders', label: 'navbar.configuration' },
  { href: './password', page: 'password', icon: 'fa-user-shield', label: 'navbar.password' },
  { href: './troubleshooting', page: 'troubleshooting', icon: 'fa-stethoscope', label: 'navbar.troubleshoot' },
]

const themes = ['auto', 'light', 'dark']
const theme = ref(storedTheme())
const themeIcon = { auto: 'fa-circle-half-stroke', light: 'fa-sun', dark: 'fa-moon' }

async function signOut() {
  try {
    await fetch('./api/logout', { method: 'POST', credentials: 'include' })
  } finally {
    location.href = './login'
  }
}

function nextTheme() {
  theme.value = themes[(themes.indexOf(theme.value) + 1) % themes.length]
  setTheme(theme.value)
}
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
        <button type="button" class="theme-button" @click="nextTheme"
                :title="$t('navbar.toggle_theme') + ': ' + $t('navbar.theme_' + theme)">
          <i class="fas fa-fw" :class="themeIcon[theme]" aria-hidden="true"></i>
          <span class="visually-hidden">{{ $t('navbar.toggle_theme') }}: {{ $t('navbar.theme_' + theme) }}</span>
        </button>
        <button type="button" class="theme-button" @click="signOut" :title="$t('navbar.logout')">
          <i class="fas fa-fw fa-right-from-bracket" aria-hidden="true"></i>
          <span class="visually-hidden">{{ $t('navbar.logout') }}</span>
        </button>
      </div>
    </header>
    <slot></slot>
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
  display: flex; align-items: center; gap: 18px; min-height: 56px;
}
.brand {
  display: flex; align-items: center; gap: 10px; color: #fff; text-decoration: none;
  font-weight: 600; font-size: 17px; flex: none;
}
.nav { display: flex; gap: 2px; flex: 1; overflow-x: auto; scrollbar-width: none; }
.nav::-webkit-scrollbar { display: none; }
.nav-link {
  display: flex; align-items: center; gap: 7px; padding: 7px 11px; border-radius: 6px;
  color: rgba(255, 255, 255, 0.86); text-decoration: none; white-space: nowrap; font-size: 14.5px;
}
.nav-link:hover { background: rgba(255, 255, 255, 0.12); color: #fff; }
.nav-link.active { background: rgba(255, 255, 255, 0.2); color: #fff; font-weight: 600; }
.theme-button {
  flex: none; width: 36px; height: 36px; border-radius: 6px; border: 0; cursor: pointer;
  background: rgba(255, 255, 255, 0.12); color: #fff;
}
.theme-button:hover { background: rgba(255, 255, 255, 0.22); }
@media (max-width: 760px) {
  .brand span { display: none; }
  .nav-link span { display: none; }
  .nav-link.active span { display: inline; }
}
</style>

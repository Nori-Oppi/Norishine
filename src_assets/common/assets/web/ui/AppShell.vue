<script setup>
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue'
import { storedTheme, setTheme } from './theme'
import { answerConfirm, dialogState, dismiss } from './dialogs'

const props = defineProps({
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
function chooseTheme(name) {
  theme.value = name
  setTheme(name)
}

// The current page's name, shown in the top bar on phones.
const currentLabel = computed(() => links.find((l) => l.page === props.page)?.label || 'navbar.password')

// Account menu (wide screens) and navigation drawer (phones). One is open at a time.
const openMenu = ref('')  // '', 'account' or 'drawer'
const accountButton = ref(null)
const drawerButton = ref(null)
function toggle(name) {
  openMenu.value = openMenu.value === name ? '' : name
}
function closeMenus(returnFocus = false) {
  const was = openMenu.value
  openMenu.value = ''
  if (returnFocus) {
    (was === 'drawer' ? drawerButton : accountButton).value?.focus()
  }
}
function onDocumentClick(e) {
  if (openMenu.value && !e.target.closest('.menu-area')) {
    closeMenus()
  }
}
document.addEventListener('click', onDocumentClick)
onBeforeUnmount(() => document.removeEventListener('click', onDocumentClick))

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
  } else if (e.key === 'Escape' && openMenu.value) {
    closeMenus(true)
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
          <span class="brand-name">Norishine</span>
        </a>
        <span class="page-name" aria-hidden="true">{{ $t(currentLabel) }}</span>
        <nav class="nav" aria-label="Main">
          <a v-for="link in links" :key="link.page" :href="link.href" class="nav-link"
             :class="{ active: link.page === page }" :aria-current="link.page === page ? 'page' : null"
             :title="$t(link.label)">
            <i class="fas fa-fw" :class="link.icon" aria-hidden="true"></i>
            <span class="nav-label">{{ $t(link.label) }}</span>
          </a>
        </nav>

        <div class="menu-area account-area">
          <button ref="accountButton" type="button" class="icon-button" :class="{ active: page === 'password' }"
                  aria-haspopup="menu" :aria-expanded="openMenu === 'account'" aria-controls="account-menu"
                  :title="$t('navbar.password')" @click="toggle('account')">
            <i class="fas fa-fw fa-circle-user" aria-hidden="true"></i>
            <span class="visually-hidden">{{ $t('navbar.password') }}</span>
          </button>
          <div v-if="openMenu === 'account'" id="account-menu" class="menu" role="menu">
            <a href="./password" class="menu-item" role="menuitem">
              <i class="fas fa-fw fa-key" aria-hidden="true"></i> {{ $t('navbar.change_password') }}
            </a>
            <div class="menu-label" id="theme-label">{{ $t('navbar.toggle_theme') }}</div>
            <div class="theme-choice" role="radiogroup" aria-labelledby="theme-label">
              <label v-for="name in themes" :key="name">
                <input type="radio" name="theme" :value="name" :checked="theme === name" @change="chooseTheme(name)">
                <span>{{ $t('navbar.theme_' + name) }}</span>
              </label>
            </div>
            <button type="button" class="menu-item" role="menuitem" @click="signOut">
              <i class="fas fa-fw fa-right-from-bracket" aria-hidden="true"></i> {{ $t('navbar.logout') }}
            </button>
          </div>
        </div>

        <div class="menu-area drawer-area">
          <button ref="drawerButton" type="button" class="icon-button" aria-haspopup="true"
                  :aria-expanded="openMenu === 'drawer'" aria-controls="nav-drawer" @click="toggle('drawer')">
            <i class="fas fa-fw" :class="openMenu === 'drawer' ? 'fa-xmark' : 'fa-bars'" aria-hidden="true"></i>
            <span class="visually-hidden">{{ $t('navbar.menu') }}</span>
          </button>
          <div v-if="openMenu === 'drawer'" id="nav-drawer" class="drawer">
            <nav :aria-label="$t('navbar.menu')">
              <a v-for="link in links" :key="link.page" :href="link.href" class="drawer-item"
                 :class="{ active: link.page === page }" :aria-current="link.page === page ? 'page' : null">
                <i class="fas fa-fw" :class="link.icon" aria-hidden="true"></i> {{ $t(link.label) }}
              </a>
            </nav>
            <hr>
            <a href="./password" class="drawer-item" :class="{ active: page === 'password' }">
              <i class="fas fa-fw fa-key" aria-hidden="true"></i> {{ $t('navbar.change_password') }}
            </a>
            <div class="theme-choice drawer-theme" role="radiogroup" :aria-label="$t('navbar.toggle_theme')">
              <label v-for="name in themes" :key="name">
                <input type="radio" name="theme-drawer" :value="name" :checked="theme === name" @change="chooseTheme(name)">
                <span>{{ $t('navbar.theme_' + name) }}</span>
              </label>
            </div>
            <button type="button" class="drawer-item" @click="signOut">
              <i class="fas fa-fw fa-right-from-bracket" aria-hidden="true"></i> {{ $t('navbar.logout') }}
            </button>
          </div>
        </div>
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
/* A 12% shade over the brand gradient keeps white text above 4.5:1 everywhere on it. */
.topbar {
  background: linear-gradient(rgba(0, 0, 0, 0.12), rgba(0, 0, 0, 0.12)),
              linear-gradient(90deg, var(--blush-start), var(--blush-middle) 55%, var(--blush-end));
  color: #fff;
  position: sticky; top: 0; z-index: 20;
}
.topbar :focus-visible { outline: 2px solid #fff; outline-offset: 2px; }
.topbar-inner {
  max-width: 1240px; margin: 0 auto; padding: 0 16px;
  display: flex; align-items: center; gap: 8px; min-height: 56px;
}
.brand {
  display: flex; align-items: center; gap: 10px; color: #fff; text-decoration: none;
  font-weight: 600; font-size: 17px; flex: none; margin-right: 10px;
}
.page-name { display: none; font-weight: 600; font-size: 17px; flex: 1; }
.nav { display: flex; gap: 2px; flex: 1; }
.nav-link {
  position: relative; display: flex; align-items: center; gap: 7px; padding: 7px 11px; border-radius: 6px;
  color: #fff; text-decoration: none; white-space: nowrap; font-size: 14.5px;
}
.nav-link:hover { background: rgba(255, 255, 255, 0.12); }
/* Norilight's tab indicator: an underline, not a filled pill. */
.nav-link.active { font-weight: 600; }
.nav-link.active::after {
  content: ""; position: absolute; left: 11px; right: 11px; bottom: -10px;
  height: 3px; border-radius: 3px 3px 0 0; background: #fff;
}
.icon-button {
  flex: none; width: 40px; height: 40px; border-radius: 6px; border: 0; cursor: pointer;
  background: transparent; color: #fff; font-size: 18px;
  display: inline-flex; align-items: center; justify-content: center; text-decoration: none;
}
.icon-button:hover, .icon-button.active, .icon-button[aria-expanded="true"] { background: rgba(255, 255, 255, 0.16); }
.menu-area { position: relative; flex: none; }
.drawer-area { display: none; }

.menu {
  position: absolute; right: 0; top: calc(100% + 8px); min-width: 230px; z-index: 30;
  background: var(--popup); color: var(--text-primary); border: 1px solid var(--stroke-card);
  border-radius: 10px; box-shadow: var(--shadow); padding: 6px;
  animation: menu-in 0.12s var(--ease-out);
}
@keyframes menu-in { from { opacity: 0; transform: translateY(-4px); } }
.menu-item, .drawer-item {
  display: flex; align-items: center; gap: 10px; width: 100%; padding: 9px 10px; border: 0; border-radius: 6px;
  background: none; color: var(--text-primary); font: inherit; font-size: 14px; text-align: left;
  text-decoration: none; cursor: pointer;
}
.menu-item:hover, .drawer-item:hover { background: var(--nav-hover); }
.menu :focus-visible, .drawer :focus-visible { outline-color: var(--accent); }
.menu-label { padding: 8px 10px 4px; font-size: 12px; font-weight: 600; color: var(--text-secondary); }
.theme-choice { display: flex; gap: 4px; padding: 0 6px 6px; }
.theme-choice label { flex: 1; position: relative; }
.theme-choice input { position: absolute; opacity: 0; inset: 0; margin: 0; cursor: pointer; }
.theme-choice span {
  display: block; text-align: center; padding: 6px 8px; border-radius: 6px; font-size: 13px;
  border: 1px solid var(--border); color: var(--text-primary);
}
.theme-choice input:checked + span { background: var(--accent-strong); border-color: var(--accent-strong); color: var(--on-accent); }
.theme-choice input:focus-visible + span { outline: 2px solid var(--accent); outline-offset: 2px; }

.drawer {
  position: fixed; left: 0; right: 0; top: 56px; z-index: 30; max-height: calc(100vh - 56px); overflow-y: auto;
  background: var(--popup); color: var(--text-primary); box-shadow: var(--shadow);
  border-radius: 0 0 14px 14px; padding: 8px 12px 14px;
  animation: menu-in 0.14s var(--ease-out);
}
.drawer-item { min-height: 48px; font-size: 15px; }
.drawer-item.active { background: var(--nav-selected); font-weight: 600; box-shadow: inset 3px 0 0 var(--accent); }
.drawer hr { border: 0; border-top: 1px solid var(--divider); margin: 8px 0; }
.drawer-theme { padding: 6px 0 8px; }

.toasts {
  position: fixed; right: 16px; bottom: 16px; z-index: 40; display: flex; flex-direction: column; gap: 8px;
  width: min(420px, calc(100vw - 32px));
}
.toast { box-shadow: var(--shadow); background: var(--popup); }
.confirm-body { white-space: pre-line; }

/* Medium widths: icons only, each with its name as a tooltip and accessible name. */
@media (max-width: 1000px) {
  .nav-label { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0, 0, 0, 0); white-space: nowrap; }
  .nav-link { padding: 8px 12px; font-size: 16px; }
  .nav-link.active::after { left: 8px; right: 8px; }
}
/* Phones: the page's name and a menu, never a sideways-scrolling strip. */
@media (max-width: 720px) {
  .brand-name, .nav, .account-area { display: none; }
  .page-name { display: block; }
  .drawer-area { display: block; }
}
</style>

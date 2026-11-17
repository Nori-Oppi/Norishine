<script setup>
// A "⋯" button with a small menu of less common row actions (move, remove...).
// items: [{ label, icon, danger, disabled, separator, action }]
import { nextTick, onBeforeUnmount, ref } from 'vue'

const props = defineProps({
  items: { type: Array, required: true },
  label: { type: String, required: true },
})

const open = ref(false)
const root = ref(null)
const button = ref(null)
const menu = ref(null)

function enabledItems() {
  return menu.value ? [...menu.value.querySelectorAll('[role="menuitem"]:not([disabled])')] : []
}

async function show() {
  open.value = true
  document.addEventListener('pointerdown', onOutside, true)
  await nextTick()
  const first = enabledItems()[0]
  if (first) {
    first.focus()
  }
}

function hide(focusButton = true) {
  open.value = false
  document.removeEventListener('pointerdown', onOutside, true)
  if (focusButton && button.value) {
    button.value.focus()
  }
}

function onOutside(e) {
  if (root.value && !root.value.contains(e.target)) {
    hide(false)
  }
}

function onKey(e) {
  const list = enabledItems()
  const at = list.indexOf(document.activeElement)
  if (e.key === 'Escape') {
    e.preventDefault()
    hide()
  } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault()
    const step = e.key === 'ArrowDown' ? 1 : -1
    list[(at + step + list.length) % list.length]?.focus()
  } else if (e.key === 'Home' || e.key === 'End') {
    e.preventDefault()
    list[e.key === 'Home' ? 0 : list.length - 1]?.focus()
  } else if (e.key === 'Tab') {
    hide(false)
  }
}

function choose(item) {
  hide()
  item.action()
}

onBeforeUnmount(() => document.removeEventListener('pointerdown', onOutside, true))
</script>

<template>
  <span ref="root" class="row-menu">
    <button ref="button" type="button" class="btn btn-icon more" :class="{ open }" :title="label"
            aria-haspopup="menu" :aria-expanded="open ? 'true' : 'false'" @click="open ? hide() : show()">
      <i class="fas fa-ellipsis" aria-hidden="true"></i><span class="visually-hidden">{{ label }}</span>
    </button>
    <div v-if="open" ref="menu" class="row-menu-list" role="menu" :aria-label="label" @keydown="onKey">
      <template v-for="(item, i) in items" :key="i">
        <div v-if="item.separator" class="sep" role="separator"></div>
        <button v-else type="button" role="menuitem" class="row-menu-item" :class="{ danger: item.danger }"
                :disabled="item.disabled" @click="choose(item)">
          <i v-if="item.icon" class="fas" :class="item.icon" aria-hidden="true"></i>{{ item.label }}
        </button>
      </template>
    </div>
  </span>
</template>

<style scoped>
.row-menu { position: relative; display: inline-flex; }
.more.open { background: var(--nav-hover); color: var(--text-primary); }
.row-menu-list {
  position: absolute; right: 0; top: calc(100% + 4px); z-index: 15; min-width: 200px;
  background: var(--popup); border: 1px solid var(--stroke-card); border-radius: 10px;
  box-shadow: var(--shadow); padding: 6px; animation: menu-in 0.12s var(--ease-out);
}
@keyframes menu-in { from { opacity: 0; transform: translateY(-4px); } }
.row-menu-item {
  display: flex; align-items: center; gap: 10px; width: 100%; padding: 9px 10px; border: 0; border-radius: 6px;
  background: none; color: var(--text-primary); font: inherit; font-size: 14px; text-align: left; cursor: pointer;
}
.row-menu-item i { width: 16px; text-align: center; color: var(--text-secondary); }
.row-menu-item:hover:not(:disabled), .row-menu-item:focus-visible { background: var(--nav-hover); outline: none; }
.row-menu-item:disabled { color: var(--text-disabled); cursor: default; }
.row-menu-item:disabled i { color: var(--text-disabled); }
.row-menu-item.danger, .row-menu-item.danger i { color: var(--error); }
.sep { height: 1px; margin: 4px 0; background: var(--divider); }
@media (pointer: coarse) { .row-menu-item { min-height: 44px; } }
@media (prefers-reduced-motion: reduce) { .row-menu-list { animation: none; } }
</style>

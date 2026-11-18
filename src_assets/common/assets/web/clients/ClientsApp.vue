<script setup>
import { computed, inject, nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import AppShell from '../ui/AppShell.vue'
import { api } from '../ui/api'
import { confirmAction, notify } from '../ui/dialogs'
import { clone } from '../settings/model'
import CommandList from '../settings/CommandList.vue'
import RowMenu from '../ui/RowMenu.vue'
import HelpLink from '../ui/HelpLink.vue'
import { P, ACCESS_LEVELS, MANAGE_LEVELS, INPUTS, EXTRAS, PRESETS, accessLevel, manageLevel, presetOf, setLevel, summary } from './permissions'

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const clients = ref([])
const platform = ref('')
const loading = ref(true)
const loadError = ref('')
const editing = ref(null)  // uuid
const form = ref(null)
const saving = ref(false)
let timer = null

const CMD_FIELDS = [{ name: 'cmd', label: 'devices.command' }]

async function load() {
  try {
    const r = await api('./api/clients/list')
    platform.value = r.platform || ''
    clients.value = (r.named_certs || []).map((c) => ({ ...c, perm: parseInt(c.perm, 10) || 0 }))
    loadError.value = ''
  } catch (e) {
    if (e.status !== 401) {
      loadError.value = e.message
    }
  } finally {
    loading.value = false
  }
}

// "#<uuid>" (from the Pairing page) opens that device's editor once the list is in.
async function openFromHash() {
  const uuid = decodeURIComponent(location.hash.slice(1))
  if (!uuid) {
    return
  }
  const c = clients.value.find((x) => x.uuid === uuid)
  if (c) {
    edit(c)
    history.replaceState(null, '', location.pathname)
    await nextTick()
    document.getElementById('device-' + uuid)?.scrollIntoView({ block: 'start', behavior: 'smooth' })
  }
}

// A click anywhere on a row (not on its buttons) opens the editor.
function rowClick(e, c) {
  if (editing.value !== c.uuid && !e.target.closest('button, a, input, label, [role="menu"]')) {
    edit(c)
  }
}

onMounted(async () => {
  await load()
  openFromHash()
  // Keep the connected state current, but not while a device is being edited.
  timer = setInterval(() => {
    if (!editing.value) {
      load()
    }
  }, 5000)
})
onBeforeUnmount(() => clearInterval(timer))

function edit(c) {
  editing.value = c.uuid
  form.value = {
    uuid: c.uuid,
    name: c.name,
    perm: c.perm,
    display_mode: c.display_mode || '',
    enable_legacy_ordering: c.enable_legacy_ordering !== false,
    allow_client_commands: c.allow_client_commands !== false,
    always_use_virtual_display: !!c.always_use_virtual_display,
    do: clone(c.do || []),
    undo: clone(c.undo || []),
  }
  // Open the detailed permissions straight away when they don't match a quick choice.
  fineTune.value = !presetOf(c.perm)
}
const fineTune = ref(false)
const permSummary = computed(() => form.value ? summary(form.value.perm).map((s) => t(s.key)).join(' · ') : '')
function cancel() {
  editing.value = null
  form.value = null
}

// One-line summaries for the collapsed Display and Commands sections.
const displaySummary = computed(() => {
  const f = form.value
  const parts = []
  if (f.always_use_virtual_display && platform.value === 'windows') {
    parts.push(t('devices.sum_virtual_display'))
  }
  parts.push(f.display_mode.trim() ? f.display_mode.trim() : t('devices.sum_device_mode'))
  return parts.join(' · ')
})
const commandsSummary = computed(() => {
  const f = form.value
  if (!f.allow_client_commands) {
    return t('devices.sum_commands_off')
  }
  const n = [...f.do, ...f.undo].filter((c) => (c.cmd || '').trim()).length
  return n ? t('devices.sum_commands', { n }, n) : t('devices.sum_commands_none')
})
function deviceMenu(c) {
  return [{ label: t('devices.unpair'), icon: 'fa-link-slash', danger: true, action: () => unpair(c) }]
}

const modeInvalid = computed(() => form.value && form.value.display_mode.trim() !== '' &&
  !/^\d+x\d+x\d+(\.\d+)?$/.test(form.value.display_mode.trim()))

function toggle(bit, on) {
  form.value.perm = on ? (form.value.perm | bit) : (form.value.perm & ~bit)
}

async function save() {
  if (modeInvalid.value) {
    return
  }
  saving.value = true
  const f = form.value
  const cmds = (list) => list.filter((c) => (c.cmd || '').trim() !== '').map((c) => ({ cmd: c.cmd.trim(), elevated: c.elevated === true || c.elevated === 'true' }))
  try {
    const r = await api('./api/clients/update', {
      method: 'POST',
      body: {
        uuid: f.uuid,
        name: f.name.trim(),
        display_mode: f.display_mode.trim(),
        perm: f.perm >>> 0,
        enable_legacy_ordering: f.enable_legacy_ordering,
        allow_client_commands: f.allow_client_commands,
        always_use_virtual_display: f.always_use_virtual_display,
        do: cmds(f.do),
        undo: cmds(f.undo),
      },
    })
    if (!r.status) {
      throw new Error(t('devices.not_found'))
    }
    notify(t('devices.saved', { name: f.name || t('devices.unnamed') }))
    cancel()
  } catch (e) {
    notify(t('devices.save_failed', { error: e.message }), 'error')
  } finally {
    saving.value = false
    load()
  }
}

async function unpair(c) {
  if (!await confirmAction({ title: t('devices.unpair_title', { name: c.name || t('devices.unnamed') }), body: t('devices.unpair_body'), confirm: t('devices.unpair'), danger: true })) {
    return
  }
  try {
    await api('./api/clients/unpair', { method: 'POST', body: { uuid: c.uuid } })
    notify(t('devices.unpaired', { name: c.name || t('devices.unnamed') }))
  } catch (e) {
    notify(t('devices.unpair_failed', { error: e.message }), 'error')
  }
  if (editing.value === c.uuid) {
    cancel()
  }
  load()
}

async function unpairAll() {
  if (!await confirmAction({ title: t('devices.unpair_all_title'), body: t('devices.unpair_all_body'), confirm: t('devices.unpair_all'), danger: true })) {
    return
  }
  try {
    await api('./api/clients/unpair-all', { method: 'POST', body: {} })
    notify(t('devices.unpaired_all'))
  } catch (e) {
    notify(t('devices.unpair_failed', { error: e.message }), 'error')
  }
  cancel()
  load()
}

async function disconnect(c) {
  try {
    await api('./api/clients/disconnect', { method: 'POST', body: { uuid: c.uuid } })
    notify(t('home.disconnected', { name: c.name || t('devices.unnamed') }))
  } catch (e) {
    notify(t('home.disconnect_failed', { error: e.message }), 'error')
  }
  setTimeout(load, 800)
}
</script>

<template>
  <AppShell page="clients">
    <main class="page-main">
      <div class="page-head">
        <div>
          <h1>{{ $t('devices.title') }} <HelpLink page="Norishine-Guide#devices-and-permissions" :label="$t('help.page')" /></h1>
        </div>
        <a class="btn btn-primary" href="./pin"><i class="fas fa-plus" aria-hidden="true"></i> {{ $t('devices.pair_new') }}</a>
      </div>

      <div v-if="loadError" class="notice notice-error section"><span class="grow">{{ $t('devices.load_failed', { error: loadError }) }}</span></div>

      <div class="card-pad">
        <div v-if="loading" class="empty-note"><i class="fas fa-spinner fa-spin" aria-hidden="true"></i></div>
        <div v-else-if="!clients.length" class="empty-note">
          {{ $t('devices.none') }} <a href="./pin">{{ $t('devices.pair_new') }}</a>
        </div>

        <div v-for="c in clients" :key="c.uuid" :id="'device-' + c.uuid" class="device" :class="{ open: editing === c.uuid }">
          <div class="list-row" :class="{ clickable: editing !== c.uuid }" @click="rowClick($event, c)">
            <span class="avatar" aria-hidden="true"><i class="fas fa-display"></i></span>
            <div class="grow">
              <div class="row-title">{{ c.name || $t('devices.unnamed') }}</div>
              <div class="row-sub summary">
                <template v-for="(s, i) in summary(c.perm)" :key="s.key"><span v-if="i" aria-hidden="true"> · </span><span :class="{ careful: s.careful }">{{ $t(s.key) }}</span></template>
              </div>
            </div>
            <span v-if="c.connected" class="badge badge-ok">{{ $t('devices.connected') }}</span>
            <button v-if="c.connected" type="button" class="btn btn-small btn-quiet" @click="disconnect(c)">
              <i class="fas fa-link-slash" aria-hidden="true"></i> {{ $t('home.disconnect') }}
            </button>
            <button v-if="editing !== c.uuid" type="button" class="btn btn-small btn-quiet" @click="edit(c)">
              <i class="fas fa-pen" aria-hidden="true"></i> {{ $t('devices.edit') }}
            </button>
            <RowMenu v-if="editing !== c.uuid" :items="deviceMenu(c)" :label="$t('devices.more', { name: c.name || $t('devices.unnamed') })" />
          </div>

          <form v-if="editing === c.uuid && form" class="editor" @submit.prevent="save">
            <div class="form-field">
              <label :for="'name-' + c.uuid">{{ $t('devices.name') }}</label>
              <input :id="'name-' + c.uuid" v-model="form.name" class="field" maxlength="128">
            </div>

            <fieldset class="group">
              <legend>{{ $t('devices.quick') }}</legend>
              <div class="presets" role="radiogroup">
                <label v-for="preset in PRESETS" :key="preset.id" class="level" :class="{ active: presetOf(form.perm) === preset.id }" :title="$t(preset.help)">
                  <input type="radio" :name="'preset-' + c.uuid" :checked="presetOf(form.perm) === preset.id"
                         @change="form.perm = preset.apply(form.perm)">
                  <span class="row-title">{{ $t(preset.label) }}</span>
                </label>
              </div>
            </fieldset>

            <details class="more-group" :open="fineTune" @toggle="fineTune = $event.target.open">
              <summary><span class="sum-title">{{ $t('devices.fine_tune') }}</span><span class="sum-value">{{ permSummary }}</span></summary>
              <div class="more-body fine">
            <fieldset class="group">
              <legend>{{ $t('devices.perm_streaming') }}</legend>
              <div class="levels" role="radiogroup">
                <label v-for="level in ACCESS_LEVELS" :key="level.id" class="level" :class="{ active: accessLevel(form.perm) === level.id }" :title="$t(level.help)">
                  <input type="radio" :name="'access-' + c.uuid" :checked="accessLevel(form.perm) === level.id"
                         @change="form.perm = setLevel(form.perm, P.access, level.bits)">
                  <span class="row-title">{{ $t(level.label) }}</span>
                </label>
              </div>
            </fieldset>

            <fieldset class="group">
              <legend>{{ $t('devices.perm_input') }}</legend>
              <div class="checks">
                <label v-for="p in INPUTS" :key="p.bit" class="check">
                  <input type="checkbox" :checked="(form.perm & p.bit) !== 0" @change="toggle(p.bit, $event.target.checked)">
                  {{ $t(p.label) }}
                </label>
              </div>
            </fieldset>

            <fieldset class="group">
              <legend>{{ $t('devices.perm_extras') }}</legend>
              <div class="checks column">
                <label v-for="p in EXTRAS" :key="p.bit" class="check" :title="$t(p.help)">
                  <input type="checkbox" :checked="(form.perm & p.bit) !== 0" @change="toggle(p.bit, $event.target.checked)">
                  <span>{{ $t(p.label) }}</span>
                </label>
              </div>
            </fieldset>
              </div>
            </details>

            <fieldset class="group">
              <legend>{{ $t('devices.perm_manage') }}</legend>
              <div class="levels three" role="radiogroup">
                <label v-for="level in MANAGE_LEVELS" :key="level.id" class="level" :class="{ active: manageLevel(form.perm) === level.id }" :title="$t(level.help)">
                  <input type="radio" :name="'manage-' + c.uuid" :checked="manageLevel(form.perm) === level.id"
                         @change="form.perm = setLevel(form.perm, P.manage, level.bits)">
                  <span class="row-title">{{ $t(level.label) }}</span>
                  <span v-if="level.id === 'write'" class="row-sub careful">{{ $t('devices.manage_trust') }}</span>
                </label>
              </div>
            </fieldset>

            <details class="more-group">
              <summary><span class="sum-title">{{ $t('devices.display') }}</span><span class="sum-value">{{ displaySummary }}</span></summary>
              <div class="more-body">
              <div class="form-field">
                <label :for="'mode-' + c.uuid">{{ $t('devices.display_mode') }}</label>
                <input :id="'mode-' + c.uuid" v-model="form.display_mode" class="field mono mode" :class="{ invalid: modeInvalid }" placeholder="1920x1080x59.94">
                <p v-if="modeInvalid" class="form-help error-text">{{ $t('devices.display_mode_invalid') }}</p>
              </div>
              <div v-if="platform === 'windows'" class="toggle-row">
                <div class="grow"><div class="row-title" :title="$t('pin.always_use_virtual_display_desc')">{{ $t('pin.always_use_virtual_display') }}</div></div>
                <label class="switch"><input v-model="form.always_use_virtual_display" type="checkbox" role="switch" :aria-label="$t('pin.always_use_virtual_display')"><span class="track"></span></label>
              </div>
              <div class="toggle-row">
                <div class="grow"><div class="row-title" :title="$t('devices.legacy_ordering_help')">{{ $t('pin.enable_legacy_ordering') }}</div></div>
                <label class="switch"><input v-model="form.enable_legacy_ordering" type="checkbox" role="switch" :aria-label="$t('pin.enable_legacy_ordering')"><span class="track"></span></label>
              </div>
              </div>
            </details>

            <details class="more-group">
              <summary><span class="sum-title">{{ $t('devices.commands') }}</span><span class="sum-value">{{ commandsSummary }}</span></summary>
              <div class="more-body">
              <div class="toggle-row">
                <div class="grow"><div class="row-title" :title="$t('devices.allow_commands_help')">{{ $t('devices.allow_commands') }}</div></div>
                <label class="switch"><input v-model="form.allow_client_commands" type="checkbox" role="switch" :aria-label="$t('devices.allow_commands')"><span class="track"></span></label>
              </div>
              <template v-if="form.allow_client_commands">
                <div class="form-field cmds">
                  <span class="form-label">{{ $t('devices.on_connect') }}</span>
                  <CommandList :value="form.do" :fields="CMD_FIELDS" :elevated="platform === 'windows'" @update="form.do = $event" />
                </div>
                <div class="form-field cmds">
                  <span class="form-label">{{ $t('devices.on_disconnect') }}</span>
                  <CommandList :value="form.undo" :fields="CMD_FIELDS" :elevated="platform === 'windows'" @update="form.undo = $event" />
                </div>
              </template>
              </div>
            </details>

            <div class="editor-actions">
              <button type="button" class="btn btn-quiet danger unpair" @click="unpair(c)">
                <i class="fas fa-link-slash" aria-hidden="true"></i> {{ $t('devices.unpair') }}
              </button>
              <button type="button" class="btn" @click="cancel">{{ $t('settings.cancel') }}</button>
              <button type="submit" class="btn btn-primary" :disabled="saving || modeInvalid">
                <i v-if="saving" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('settings.save') }}
              </button>
            </div>
          </form>
        </div>
      </div>
      <div class="foot">
        <button v-if="clients.length > 1" type="button" class="btn btn-small btn-quiet danger" @click="unpairAll">{{ $t('devices.unpair_all') }}</button>
      </div>
    </main>
  </AppShell>
</template>

<style scoped>
.device { border-top: 1px solid var(--row-divider); scroll-margin-top: 72px; }
.more-body.fine { gap: 20px; padding-top: 8px; }
.device:first-of-type { border-top: 0; }
.device .list-row { border-top: 0; min-height: 68px; gap: 14px; }
.avatar {
  width: 40px; height: 40px; border-radius: 50%; flex: none; background: var(--nav-selected); color: var(--accent);
  display: inline-flex; align-items: center; justify-content: center; font-size: 16px;
}
.summary { margin-top: 2px; }
.list-row.clickable { cursor: pointer; }
.list-row.clickable:hover .row-title { color: var(--accent); }
.presets { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; margin-top: 8px; }
.custom-note { margin-top: 8px; }
.summary .careful { color: var(--warning); }
/* The editor sits on the page colour, tied to its row by the accent bar. */
.editor {
  margin: 0 calc(-1 * var(--card-padding)); padding: 18px 20px 0; display: flex; flex-direction: column; gap: 22px;
  background: var(--background); border-left: 2px solid var(--accent); border-top: 1px solid var(--row-divider);
}
.device:last-child .editor { border-radius: 0 0 var(--radius) var(--radius); }
.group { border: 0; margin: 0; padding: 0; }
.group legend { font-weight: 600; color: var(--text-heading); padding: 0; margin-bottom: 10px; font-size: 16px; }
.more-group { border: 1px solid var(--border); border-radius: 8px; background: var(--card); }
.more-group summary {
  display: flex; align-items: center; gap: 12px; padding: 12px 14px; cursor: pointer; list-style: none;
}
.more-group summary::-webkit-details-marker { display: none; }
.more-group summary::before {
  content: ""; width: 7px; height: 7px; flex: none; border-right: 2px solid var(--text-secondary); border-bottom: 2px solid var(--text-secondary);
  transform: rotate(-45deg); transition: transform 0.12s;
}
.more-group[open] summary::before { transform: rotate(45deg); }
.sum-title { font-weight: 600; color: var(--text-heading); }
.sum-value { margin-left: auto; font-size: 13px; color: var(--text-secondary); text-align: right; }
.more-body { padding: 4px 14px 14px; display: flex; flex-direction: column; gap: 10px; }
.danger { color: var(--error); }
.danger:hover:not(:disabled) { background: color-mix(in srgb, var(--error) 10%, transparent); }
.careful { color: var(--warning); }
.foot { display: flex; justify-content: flex-end; align-items: center; gap: 12px; margin-top: 14px; flex-wrap: wrap; }
.foot p { margin: 0; }
.levels { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 8px; }
.levels.three { grid-template-columns: repeat(3, minmax(0, 1fr)); }
.level {
  position: relative; display: flex; flex-direction: column; gap: 2px; padding: 11px 12px 11px 38px; border-radius: 8px; cursor: pointer;
  border: 1px solid var(--border); background: var(--card);
}
.level input { position: absolute; opacity: 0; pointer-events: none; }
/* A radio dot, so the choice isn't shown by colour alone. */
.level::before {
  content: ""; position: absolute; left: 12px; top: 13px; width: 16px; height: 16px; border-radius: 50%;
  border: 2px solid var(--border-strong); box-sizing: border-box;
}
.level.active { border-color: var(--accent); background: color-mix(in srgb, var(--accent) 11%, var(--card)); }
.level.active::before { border-color: var(--accent); background: radial-gradient(var(--accent) 0 3.5px, transparent 4.5px); }
.level:focus-within { outline: 2px solid var(--accent); outline-offset: 2px; }
.checks { display: flex; flex-wrap: wrap; gap: 8px 18px; }
.checks.column { flex-direction: column; }
.check { display: flex; align-items: center; gap: 8px; cursor: pointer; }
.check input { width: 16px; height: 16px; accent-color: var(--accent-strong); }
.mode { max-width: 260px; }
.cmds { margin-top: 8px; }
/* Save and Cancel stay in view while scrolling through a long editor. */
.editor-actions {
  position: sticky; bottom: 0; z-index: 2; display: flex; justify-content: flex-end; gap: 10px;
  margin: 0 -20px; padding: 12px 20px 14px; background: var(--background); border-top: 1px solid var(--divider);
}
.editor-actions .unpair { margin-right: auto; }
@media (max-width: 760px) {
  .levels, .levels.three { grid-template-columns: 1fr 1fr; }
  .presets { grid-template-columns: 1fr; }
}
</style>

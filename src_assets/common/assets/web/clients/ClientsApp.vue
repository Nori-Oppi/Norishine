<script setup>
import { computed, inject, onBeforeUnmount, onMounted, ref } from 'vue'
import AppShell from '../ui/AppShell.vue'
import { api } from '../ui/api'
import { confirmAction, notify } from '../ui/dialogs'
import { clone } from '../settings/model'
import CommandList from '../settings/CommandList.vue'
import { P, ACCESS_LEVELS, MANAGE_LEVELS, INPUTS, EXTRAS, accessLevel, manageLevel, setLevel, summary } from './permissions'

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

onMounted(() => {
  load()
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
}
function cancel() {
  editing.value = null
  form.value = null
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
          <h1>{{ $t('devices.title') }}</h1>
          <p class="muted">{{ $t('devices.intro') }}</p>
        </div>
        <div class="head-buttons">
          <a class="btn" href="./pin"><i class="fas fa-plus" aria-hidden="true"></i> {{ $t('devices.pair_new') }}</a>
          <button v-if="clients.length" type="button" class="btn btn-danger" @click="unpairAll">{{ $t('devices.unpair_all') }}</button>
        </div>
      </div>

      <div v-if="loadError" class="notice notice-error section"><span class="grow">{{ $t('devices.load_failed', { error: loadError }) }}</span></div>

      <div class="card-pad">
        <div v-if="loading" class="empty-note"><i class="fas fa-spinner fa-spin" aria-hidden="true"></i></div>
        <div v-else-if="!clients.length" class="empty-note">
          {{ $t('devices.none') }} <a href="./pin">{{ $t('devices.pair_new') }}</a>
        </div>

        <div v-for="c in clients" :key="c.uuid" class="device" :class="{ open: editing === c.uuid }">
          <div class="list-row">
            <span class="status-dot" :class="{ on: c.connected }" :title="c.connected ? $t('devices.connected') : $t('devices.not_connected')"></span>
            <div class="grow">
              <div class="row-title">
                {{ c.name || $t('devices.unnamed') }}
                <span v-if="c.connected" class="badge badge-accent">{{ $t('devices.connected') }}</span>
              </div>
              <div class="chips summary">
                <span v-for="s in summary(c.perm)" :key="s.key" class="badge" :class="{ 'badge-careful': s.careful }">{{ $t(s.key) }}</span>
              </div>
            </div>
            <button v-if="c.connected" type="button" class="btn btn-small" @click="disconnect(c)">
              <i class="fas fa-link-slash" aria-hidden="true"></i> {{ $t('home.disconnect') }}
            </button>
            <button v-if="editing !== c.uuid" type="button" class="btn btn-small" @click="edit(c)">
              <i class="fas fa-pen" aria-hidden="true"></i> {{ $t('devices.edit') }}
            </button>
            <button type="button" class="btn btn-icon" :title="$t('devices.unpair')" @click="unpair(c)">
              <i class="fas fa-trash-can" aria-hidden="true"></i><span class="visually-hidden">{{ $t('devices.unpair') }}</span>
            </button>
          </div>

          <form v-if="editing === c.uuid && form" class="editor" @submit.prevent="save">
            <div class="form-field">
              <label :for="'name-' + c.uuid">{{ $t('devices.name') }}</label>
              <input :id="'name-' + c.uuid" v-model="form.name" class="field" maxlength="128">
            </div>

            <fieldset class="group">
              <legend>{{ $t('devices.perm_streaming') }}</legend>
              <div class="levels" role="radiogroup">
                <label v-for="level in ACCESS_LEVELS" :key="level.id" class="level" :class="{ active: accessLevel(form.perm) === level.id }">
                  <input type="radio" :name="'access-' + c.uuid" :checked="accessLevel(form.perm) === level.id"
                         @change="form.perm = setLevel(form.perm, P.access, level.bits)">
                  <span class="row-title">{{ $t(level.label) }}</span>
                  <span class="row-sub">{{ $t(level.help) }}</span>
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
                <label v-for="p in EXTRAS" :key="p.bit" class="check">
                  <input type="checkbox" :checked="(form.perm & p.bit) !== 0" @change="toggle(p.bit, $event.target.checked)">
                  <span>{{ $t(p.label) }} <span class="row-sub">· {{ $t(p.help) }}</span></span>
                </label>
              </div>
            </fieldset>

            <fieldset class="group">
              <legend>{{ $t('devices.perm_manage') }}</legend>
              <p class="form-help">{{ $t('devices.manage_help') }}</p>
              <div class="levels three" role="radiogroup">
                <label v-for="level in MANAGE_LEVELS" :key="level.id" class="level" :class="{ active: manageLevel(form.perm) === level.id }">
                  <input type="radio" :name="'manage-' + c.uuid" :checked="manageLevel(form.perm) === level.id"
                         @change="form.perm = setLevel(form.perm, P.manage, level.bits)">
                  <span class="row-title">{{ $t(level.label) }}</span>
                  <span class="row-sub">{{ $t(level.help) }}</span>
                </label>
              </div>
            </fieldset>

            <fieldset class="group">
              <legend>{{ $t('devices.display') }}</legend>
              <div class="form-field">
                <label :for="'mode-' + c.uuid">{{ $t('devices.display_mode') }}</label>
                <input :id="'mode-' + c.uuid" v-model="form.display_mode" class="field mono mode" :class="{ invalid: modeInvalid }" placeholder="1920x1080x59.94">
                <p class="form-help" :class="{ 'error-text': modeInvalid }">{{ modeInvalid ? $t('devices.display_mode_invalid') : $t('devices.display_mode_help') }}</p>
              </div>
              <div v-if="platform === 'windows'" class="toggle-row">
                <div class="grow"><div class="row-title">{{ $t('pin.always_use_virtual_display') }}</div><div class="row-sub">{{ $t('pin.always_use_virtual_display_desc') }}</div></div>
                <label class="switch"><input v-model="form.always_use_virtual_display" type="checkbox" role="switch" :aria-label="$t('pin.always_use_virtual_display')"><span class="track"></span></label>
              </div>
              <div class="toggle-row">
                <div class="grow"><div class="row-title">{{ $t('pin.enable_legacy_ordering') }}</div><div class="row-sub">{{ $t('devices.legacy_ordering_help') }}</div></div>
                <label class="switch"><input v-model="form.enable_legacy_ordering" type="checkbox" role="switch" :aria-label="$t('pin.enable_legacy_ordering')"><span class="track"></span></label>
              </div>
            </fieldset>

            <fieldset class="group">
              <legend>{{ $t('devices.commands') }}</legend>
              <div class="toggle-row">
                <div class="grow"><div class="row-title">{{ $t('devices.allow_commands') }}</div><div class="row-sub">{{ $t('devices.allow_commands_help') }}</div></div>
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
            </fieldset>

            <div class="editor-actions">
              <button type="button" class="btn" @click="cancel">{{ $t('settings.cancel') }}</button>
              <button type="submit" class="btn btn-primary" :disabled="saving || modeInvalid">
                <i v-if="saving" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('settings.save') }}
              </button>
            </div>
          </form>
        </div>
      </div>
      <p class="small muted note">{{ $t('devices.first_device_note') }}</p>
    </main>
  </AppShell>
</template>

<style scoped>
.head-buttons { display: flex; gap: 8px; }
.device { border-top: 1px solid var(--row-divider); }
.device:first-of-type { border-top: 0; }
.device .list-row { border-top: 0; }
.summary { margin-top: 5px; }
.editor { padding: 4px 0 18px; display: flex; flex-direction: column; gap: 16px; }
.group { border: 0; margin: 0; padding: 0; }
.group legend { font-weight: 600; color: var(--text-heading); padding: 0; margin-bottom: 8px; font-size: 15px; }
.levels { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 8px; }
.levels.three { grid-template-columns: repeat(3, minmax(0, 1fr)); }
.level {
  display: flex; flex-direction: column; gap: 2px; padding: 10px 12px; border-radius: 8px; cursor: pointer;
  border: 1px solid var(--border); background: var(--field);
}
.level input { position: absolute; opacity: 0; pointer-events: none; }
.level.active { border-color: var(--accent); box-shadow: inset 0 0 0 1px var(--accent); }
.level:focus-within { outline: 2px solid var(--accent); outline-offset: 2px; }
.checks { display: flex; flex-wrap: wrap; gap: 8px 18px; }
.checks.column { flex-direction: column; }
.check { display: flex; align-items: center; gap: 8px; cursor: pointer; }
.check input { width: 16px; height: 16px; accent-color: var(--accent-strong); }
.mode { max-width: 260px; }
.cmds { margin-top: 8px; }
.editor-actions { display: flex; justify-content: flex-end; gap: 10px; }
.note { margin-top: 14px; }
@media (max-width: 760px) {
  .levels, .levels.three { grid-template-columns: 1fr 1fr; }
}
</style>

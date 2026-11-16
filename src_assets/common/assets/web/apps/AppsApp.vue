<script setup>
import { inject, onMounted, ref } from 'vue'
import AppShell from '../ui/AppShell.vue'
import { api } from '../ui/api'
import { confirmAction, notify } from '../ui/dialogs'
import { clone } from '../settings/model'
import AppEditor from './AppEditor.vue'

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const NEW_APP = {
  name: '', output: '', cmd: '', 'working-dir': '', 'image-path': '',
  'exclude-global-prep-cmd': false, 'exclude-global-state-cmd': false, elevated: false,
  'auto-detach': true, 'wait-all': true, 'exit-timeout': 5,
  'prep-cmd': [], 'state-cmd': [], detached: [],
  'scale-factor': 100, 'use-app-identity': false, 'per-client-app-identity': false,
  'allow-client-commands': true, 'virtual-display': false, 'terminate-on-pause': false, gamepad: '',
}

const apps = ref([])
const current = ref('')
const host = ref({ name: '', uuid: '' })
const platform = ref('')
const loading = ref(true)
const loadError = ref('')
const editing = ref(null)
const saving = ref(false)
const reordered = ref(false)
const busy = ref(false)
const dragIndex = ref(-1)
const dropIndex = ref(-1)

async function load() {
  try {
    const r = await api('./api/apps')
    apps.value = (r.apps || []).filter((a) => a.uuid)
    current.value = r.current_app || ''
    host.value = { name: r.host_name, uuid: r.host_uuid }
    reordered.value = false
    loadError.value = ''
  } catch (e) {
    if (e.status !== 401) {
      loadError.value = e.message
    }
  } finally {
    loading.value = false
  }
}

onMounted(async () => {
  load()
  try {
    platform.value = (await api('./api/v1/status')).platform
  } catch { /* fields for other platforms stay visible */ }
})

function startNew() {
  editing.value = clone(NEW_APP)
  window.scrollTo({ top: 0 })
}
function edit(app) {
  editing.value = Object.assign(clone(NEW_APP), clone(app))
  window.scrollTo({ top: 0 })
}

async function save() {
  const a = editing.value
  a.name = a.name.trim()
  if (!a.name) {
    return
  }
  a['exit-timeout'] = Number.isFinite(parseInt(a['exit-timeout'], 10)) ? parseInt(a['exit-timeout'], 10) : 5
  a['scale-factor'] = parseInt(a['scale-factor'], 10) || 100
  a['image-path'] = String(a['image-path'] || '').trim().replace(/"/g, '')
  a['prep-cmd'] = (a['prep-cmd'] || []).filter((c) => (c.do || '').trim() || (c.undo || '').trim())
  a['state-cmd'] = (a['state-cmd'] || []).filter((c) => (c.do || '').trim() || (c.undo || '').trim())
  a.detached = (a.detached || []).map((c) => String(c).trim()).filter(Boolean)
  delete a.id
  saving.value = true
  try {
    const r = await api('./api/apps', { method: 'POST', body: a })
    if (r.status === false) {
      throw new Error(r.error || '')
    }
    notify(t('apps2.saved', { name: a.name }))
    editing.value = null
    load()
  } catch (e) {
    notify(t('apps2.save_failed', { error: e.message }), 'error')
  } finally {
    saving.value = false
  }
}

async function remove(app) {
  if (!await confirmAction({ title: t('apps2.delete_title', { name: app.name }), body: t('apps2.delete_body'), confirm: t('apps.delete'), danger: true })) {
    return
  }
  try {
    await api('./api/apps/delete', { method: 'POST', body: { uuid: app.uuid } })
    notify(t('apps2.deleted', { name: app.name }))
  } catch (e) {
    notify(t('apps2.delete_failed', { error: e.message }), 'error')
  }
  load()
}

async function launch(app) {
  const body = current.value ? t('apps2.launch_replaces') : t('apps2.launch_body')
  if (!await confirmAction({ title: t('apps2.launch_title', { name: app.name }), body, confirm: t('apps.launch') })) {
    return
  }
  busy.value = true
  try {
    const r = await api('./api/apps/launch', { method: 'POST', body: { uuid: app.uuid } })
    if (r.status === false) {
      throw new Error(r.error || '')
    }
    notify(t('apps2.launched', { name: app.name }))
  } catch (e) {
    notify(t('apps2.launch_failed', { error: e.message }), 'error')
  } finally {
    busy.value = false
    load()
  }
}

async function closeRunning() {
  const app = apps.value.find((a) => a.uuid === current.value)
  if (!await confirmAction({ title: t('home.close_title', { app: app ? app.name : '' }), body: t('home.close_body'), confirm: t('home.close'), danger: true })) {
    return
  }
  busy.value = true
  try {
    await api('./api/apps/close', { method: 'POST', body: {} })
    notify(t('home.closed'))
  } catch (e) {
    notify(t('home.close_failed', { error: e.message }), 'error')
  } finally {
    busy.value = false
    load()
  }
}

function exportLauncher(app) {
  const content = `# Artemis app entry\n# Exported by Norishine\n[host_uuid] ${host.value.uuid}\n[host_name] ${host.value.name}\n[app_uuid] ${app.uuid}\n[app_name] ${app.name}\n`
  const link = document.createElement('a')
  link.href = `data:text/plain;charset=utf-8,${encodeURIComponent(content)}`
  link.download = `${app.name}.art`
  link.click()
}

// Ordering
function move(i, delta) {
  const list = apps.value
  list.splice(i + delta, 0, list.splice(i, 1)[0])
  reordered.value = true
}
function alphabetize() {
  const before = apps.value.map((a) => a.uuid).join()
  apps.value.sort((x, y) => x.name.localeCompare(y.name))
  reordered.value = reordered.value || before !== apps.value.map((a) => a.uuid).join()
  if (!reordered.value) {
    notify(t('apps.already_ordered'))
  }
}
function onDragStart(e, i) {
  dragIndex.value = i
  e.dataTransfer.effectAllowed = 'move'
}
function onDragOver(e, i) {
  if (dragIndex.value < 0) {
    return
  }
  e.preventDefault()
  dropIndex.value = i
}
function onDrop(e, i) {
  e.preventDefault()
  const from = dragIndex.value
  dragIndex.value = dropIndex.value = -1
  if (from < 0 || from === i) {
    return
  }
  const list = apps.value
  const [item] = list.splice(from, 1)
  list.splice(i, 0, item)
  reordered.value = true
}
async function saveOrder() {
  busy.value = true
  try {
    const r = await api('./api/apps/reorder', { method: 'POST', body: { order: apps.value.map((a) => a.uuid) } })
    if (r.status === false) {
      throw new Error(r.error || '')
    }
    notify(t('apps2.order_saved'))
  } catch (e) {
    notify(t('apps2.order_failed', { error: e.message }), 'error')
  } finally {
    busy.value = false
    load()
  }
}
</script>

<template>
  <AppShell page="apps">
    <main class="page-main">
      <AppEditor v-if="editing" :app="editing" :platform="platform" :saving="saving"
                 @save="save" @cancel="editing = null" @export="exportLauncher(editing)" />

      <template v-else>
        <div class="page-head">
          <div>
            <h1>{{ $t('apps2.title') }}</h1>
            <p class="muted">{{ $t('apps2.intro') }}</p>
          </div>
          <button type="button" class="btn btn-primary" @click="startNew">
            <i class="fas fa-plus" aria-hidden="true"></i> {{ $t('apps2.add') }}
          </button>
        </div>

        <div v-if="loadError" class="notice notice-error section"><span class="grow">{{ $t('apps2.load_failed', { error: loadError }) }}</span></div>

        <div v-if="reordered" class="notice section">
          <span class="grow">{{ $t('apps2.order_changed') }}</span>
          <button type="button" class="btn btn-small" :disabled="busy" @click="load">{{ $t('settings.discard') }}</button>
          <button type="button" class="btn btn-small btn-primary" :disabled="busy" @click="saveOrder">{{ $t('apps.save_order') }}</button>
        </div>

        <div class="card-pad">
          <div class="list-tools" v-if="apps.length > 1">
            <span class="small muted">{{ $t('apps2.order_help') }}</span>
            <button type="button" class="btn btn-small" @click="alphabetize"><i class="fas fa-arrow-down-a-z" aria-hidden="true"></i> {{ $t('apps.alphabetize') }}</button>
          </div>
          <div v-if="loading" class="empty-note"><i class="fas fa-spinner fa-spin" aria-hidden="true"></i></div>
          <div v-else-if="!apps.length" class="empty-note">{{ $t('apps2.none') }}</div>
          <div v-for="(app, i) in apps" :key="app.uuid" class="list-row app-row" :class="{ dropping: dropIndex === i }"
               draggable="true" @dragstart="onDragStart($event, i)" @dragover="onDragOver($event, i)"
               @dragleave="dropIndex = -1" @drop="onDrop($event, i)" @dragend="dragIndex = dropIndex = -1">
            <i class="fas fa-grip-vertical grip" aria-hidden="true"></i>
            <span class="status-dot" :class="{ on: app.uuid === current }"></span>
            <div class="grow">
              <div class="row-title">{{ app.name }} <span v-if="app.uuid === current" class="badge badge-accent">{{ $t('apps2.running') }}</span></div>
              <div class="row-sub mono">{{ app.cmd || (app.detached && app.detached[0]) || $t('apps2.no_cmd') }}</div>
            </div>
            <div class="row-buttons">
              <button type="button" class="btn btn-icon" :disabled="i === 0" :title="$t('settings.move_up')" @click="move(i, -1)"><i class="fas fa-arrow-up" aria-hidden="true"></i><span class="visually-hidden">{{ $t('settings.move_up') }}</span></button>
              <button type="button" class="btn btn-icon" :disabled="i === apps.length - 1" :title="$t('settings.move_down')" @click="move(i, 1)"><i class="fas fa-arrow-down" aria-hidden="true"></i><span class="visually-hidden">{{ $t('settings.move_down') }}</span></button>
              <button v-if="app.uuid === current" type="button" class="btn btn-small btn-danger" :disabled="busy" @click="closeRunning">
                <i class="fas fa-stop" aria-hidden="true"></i> {{ $t('home.close') }}
              </button>
              <button v-else type="button" class="btn btn-small" :disabled="busy" @click="launch(app)">
                <i class="fas fa-play" aria-hidden="true"></i> {{ $t('apps.launch') }}
              </button>
              <button type="button" class="btn btn-small" @click="edit(app)"><i class="fas fa-pen" aria-hidden="true"></i> {{ $t('devices.edit') }}</button>
              <button type="button" class="btn btn-icon" :title="$t('apps.delete')" @click="remove(app)"><i class="fas fa-trash-can" aria-hidden="true"></i><span class="visually-hidden">{{ $t('apps.delete') }}</span></button>
            </div>
          </div>
        </div>
        <p class="small muted note">{{ $t('apps2.refresh_note') }}</p>
      </template>
    </main>
  </AppShell>
</template>

<style scoped>
.list-tools { display: flex; justify-content: space-between; align-items: center; gap: 10px; padding: 6px 0 10px; border-bottom: 1px solid var(--row-divider); flex-wrap: wrap; }
.app-row { cursor: default; }
.app-row.dropping { box-shadow: inset 0 2px 0 var(--accent); }
.grip { color: var(--text-disabled); cursor: grab; }
.row-buttons { display: flex; gap: 6px; align-items: center; flex-wrap: wrap; justify-content: flex-end; }
.note { margin-top: 12px; }
@media (max-width: 700px) {
  .app-row { flex-wrap: wrap; }
  .row-buttons { width: 100%; justify-content: flex-start; }
}
</style>

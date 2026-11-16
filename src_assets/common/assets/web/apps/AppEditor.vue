<script setup>
import { computed, inject, ref } from 'vue'
import { api } from '../ui/api'
import { notify } from '../ui/dialogs'
import CommandList from '../settings/CommandList.vue'
import { searchCovers } from './covers'

const props = defineProps({
  app: { type: Object, required: true },  // a copy; edited in place
  platform: { type: String, default: '' },
  saving: Boolean,
})
const emit = defineEmits(['save', 'cancel', 'export'])

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const a = props.app
const windows = computed(() => props.platform === 'windows')
const PREP_FIELDS = [{ name: 'do', label: 'apps2.cmd_do' }, { name: 'undo', label: 'apps2.cmd_undo' }]
const DETACHED_FIELDS = [{ name: '', label: '' }]

const gamepads = computed(() => {
  const list = [
    { value: '', label: 'apps2.gamepad_default' },
    { value: 'disabled', label: '_common.disabled' },
    { value: 'auto', label: '_common.auto' },
  ]
  if (props.platform === 'windows') {
    list.push({ value: 'x360', label: 'config.gamepad_x360' }, { value: 'ds4', label: 'config.gamepad_ds4' })
  } else if (props.platform === 'linux') {
    list.push({ value: 'xone', label: 'config.gamepad_xone' }, { value: 'ds5', label: 'config.gamepad_ds5' }, { value: 'switch', label: 'config.gamepad_switch' })
  }
  return list
})

const toggles = computed(() => [
  { key: 'auto-detach', label: 'apps.auto_detach', help: 'apps.auto_detach_desc' },
  { key: 'wait-all', label: 'apps.wait_all', help: 'apps.wait_all_desc' },
  { key: 'terminate-on-pause', label: 'apps.terminate_on_pause', help: 'apps.terminate_on_pause_desc' },
  { key: 'allow-client-commands', label: 'apps.allow_client_commands', help: 'apps.allow_client_commands_desc' },
  { key: 'exclude-global-prep-cmd', label: 'apps2.use_global_prep', help: 'apps.global_prep_desc', inverted: true },
  { key: 'exclude-global-state-cmd', label: 'apps2.use_global_state', help: 'apps.global_state_desc', inverted: true },
])
const displayToggles = computed(() => [
  { key: 'virtual-display', label: 'apps.virtual_display', help: 'apps.virtual_display_desc' },
  { key: 'use-app-identity', label: 'apps.use_app_identity', help: 'apps.use_app_identity_desc' },
  ...(a['use-app-identity'] ? [{ key: 'per-client-app-identity', label: 'apps.per_client_app_identity', help: 'apps.per_client_app_identity_desc', indented: true }] : []),
])

// Covers
const covers = ref(null)  // null: closed; [] while searching or none found
const coverSearching = ref(false)
const coverBusy = ref(false)
async function findCover() {
  covers.value = []
  coverSearching.value = true
  try {
    covers.value = await searchCovers(a.name)
  } catch (e) {
    notify(t('apps2.cover_search_failed', { error: e.message }), 'error')
    covers.value = null
  } finally {
    coverSearching.value = false
  }
}
async function useCover(cover) {
  coverBusy.value = true
  try {
    const r = await api('./api/covers/upload', { method: 'POST', body: { key: cover.key, url: cover.saveUrl } })
    a['image-path'] = r.path
    covers.value = null
  } catch (e) {
    notify(t('apps2.cover_failed', { error: e.message }), 'error')
  } finally {
    coverBusy.value = false
  }
}

const envVars = ['APP_ID', 'APP_NAME', 'APP_UUID', 'APP_STATUS', 'CLIENT_UUID', 'CLIENT_NAME', 'CLIENT_WIDTH', 'CLIENT_HEIGHT',
  'CLIENT_FPS', 'CLIENT_HDR', 'CLIENT_GCMAP', 'CLIENT_HOST_AUDIO', 'CLIENT_ENABLE_SOPS', 'CLIENT_AUDIO_CONFIGURATION']
const envHelp = {
  APP_ID: 'apps.env_app_id', APP_NAME: 'apps.env_app_name', APP_UUID: 'apps.env_app_uuid', APP_STATUS: 'apps.env_app_status',
  CLIENT_UUID: 'apps.env_client_uuid', CLIENT_NAME: 'apps.env_client_name', CLIENT_WIDTH: 'apps.env_client_width',
  CLIENT_HEIGHT: 'apps.env_client_height', CLIENT_FPS: 'apps.env_client_fps', CLIENT_HDR: 'apps.env_client_hdr',
  CLIENT_GCMAP: 'apps.env_client_gcmap', CLIENT_HOST_AUDIO: 'apps.env_client_host_audio',
  CLIENT_ENABLE_SOPS: 'apps.env_client_enable_sops', CLIENT_AUDIO_CONFIGURATION: 'apps.env_client_audio_config',
}
</script>

<template>
  <form class="editor" @submit.prevent="emit('save')">
    <div class="editor-head">
      <h2>{{ a.name || $t('apps2.untitled') }}</h2>
      <div class="editor-buttons">
        <button v-if="a.uuid" type="button" class="btn btn-small" @click="emit('export')">
          <i class="fas fa-file-export" aria-hidden="true"></i> {{ $t('apps2.export') }}
        </button>
        <button type="button" class="btn" @click="emit('cancel')">{{ $t('settings.cancel') }}</button>
        <button type="submit" class="btn btn-primary" :disabled="saving || !a.name.trim()">
          <i v-if="saving" class="fas fa-spinner fa-spin" aria-hidden="true"></i> {{ $t('settings.save') }}
        </button>
      </div>
    </div>

    <section class="card-pad form-grid">
      <div class="form-field">
        <label for="app-name">{{ $t('apps.app_name') }}</label>
        <input id="app-name" v-model="a.name" class="field" required>
        <p class="form-help">{{ $t('apps2.name_help') }}</p>
      </div>
      <div class="form-field">
        <label for="app-cmd">{{ $t('apps.cmd') }}</label>
        <input id="app-cmd" v-model="a.cmd" class="field mono" spellcheck="false">
        <p class="form-help">{{ $t('apps2.cmd_help') }}</p>
      </div>
      <div class="two">
        <div class="form-field">
          <label for="app-dir">{{ $t('apps.working_dir') }}</label>
          <input id="app-dir" v-model="a['working-dir']" class="field mono" spellcheck="false">
        </div>
        <div class="form-field">
          <label for="app-output">{{ $t('apps2.output') }}</label>
          <input id="app-output" v-model="a.output" class="field mono" spellcheck="false">
        </div>
      </div>
      <p class="form-help">{{ $t('apps2.dir_output_help') }}</p>

      <div class="form-field">
        <label for="app-image">{{ $t('apps.image') }}</label>
        <div class="image-row">
          <input id="app-image" v-model="a['image-path']" class="field mono" spellcheck="false">
          <button type="button" class="btn" :disabled="!a.name.trim() || coverSearching" @click="findCover">
            <i class="fas" :class="coverSearching ? 'fa-spinner fa-spin' : 'fa-magnifying-glass'" aria-hidden="true"></i> {{ $t('apps.find_cover') }}
          </button>
        </div>
        <p class="form-help">{{ $t('apps2.image_help') }}</p>
        <div v-if="covers !== null" class="covers">
          <p v-if="!coverSearching && !covers.length" class="muted small">{{ $t('apps2.no_covers') }}</p>
          <button v-for="c in covers" :key="c.key" type="button" class="cover" :disabled="coverBusy" :title="c.name" @click="useCover(c)">
            <img :src="c.url" :alt="c.name" loading="lazy">
            <span class="small">{{ c.name }}</span>
          </button>
        </div>
      </div>

      <div class="two">
        <div v-if="platform !== 'macos'" class="form-field">
          <label for="app-gamepad">{{ $t('config.gamepad') }}</label>
          <select id="app-gamepad" v-model="a.gamepad" class="field">
            <option v-for="g in gamepads" :key="g.value" :value="g.value">{{ $t(g.label) }}</option>
          </select>
        </div>
        <div class="form-field">
          <label for="app-timeout">{{ $t('apps2.exit_timeout') }}</label>
          <input id="app-timeout" v-model.number="a['exit-timeout']" class="field" type="number" min="0" step="1">
        </div>
      </div>
      <div v-if="windows" class="toggle-row">
        <div class="grow"><div class="row-title">{{ $t('apps2.elevated') }}</div><div class="row-sub">{{ $t('apps.run_as_desc') }}</div></div>
        <label class="switch"><input v-model="a.elevated" type="checkbox" role="switch" :aria-label="$t('apps2.elevated')"><span class="track"></span></label>
      </div>
    </section>

    <section class="section">
      <h3 class="sub">{{ $t('apps2.behaviour') }}</h3>
      <div class="card-pad">
        <div v-for="tg in toggles" :key="tg.key" class="toggle-row">
          <div class="grow"><div class="row-title">{{ $t(tg.label) }}</div><div class="row-sub">{{ $t(tg.help) }}</div></div>
          <label class="switch">
            <input type="checkbox" role="switch" :aria-label="$t(tg.label)" :checked="tg.inverted ? !a[tg.key] : !!a[tg.key]"
                   @change="a[tg.key] = tg.inverted ? !$event.target.checked : $event.target.checked">
            <span class="track"></span>
          </label>
        </div>
      </div>
    </section>

    <section class="section">
      <h3 class="sub">{{ $t('apps2.display') }}</h3>
      <div class="card-pad">
        <div v-for="tg in displayToggles" :key="tg.key" class="toggle-row" :class="{ indented: tg.indented }">
          <div class="grow"><div class="row-title">{{ $t(tg.label) }}</div><div class="row-sub">{{ $t(tg.help) }}</div></div>
          <label class="switch"><input v-model="a[tg.key]" type="checkbox" role="switch" :aria-label="$t(tg.label)"><span class="track"></span></label>
        </div>
        <div v-if="windows" class="toggle-row">
          <div class="grow">
            <label class="row-title" for="app-scale">{{ $t('apps.resolution_scale_factor') }}: {{ a['scale-factor'] }}%</label>
            <div class="row-sub">{{ $t('apps.resolution_scale_factor_desc') }}</div>
            <input id="app-scale" v-model.number="a['scale-factor']" type="range" min="20" max="200" step="1" class="range">
          </div>
        </div>
      </div>
    </section>

    <section class="section">
      <h3 class="sub">{{ $t('apps.cmd_prep_name') }}</h3>
      <p class="form-help">{{ $t('apps2.prep_help') }}</p>
      <div class="card-pad"><CommandList :value="a['prep-cmd']" :fields="PREP_FIELDS" :elevated="windows" @update="a['prep-cmd'] = $event" /></div>
    </section>
    <section class="section">
      <h3 class="sub">{{ $t('apps.cmd_state_name') }}</h3>
      <p class="form-help">{{ $t('apps2.state_help') }}</p>
      <div class="card-pad"><CommandList :value="a['state-cmd']" :fields="PREP_FIELDS" :elevated="windows" @update="a['state-cmd'] = $event" /></div>
    </section>
    <section class="section">
      <h3 class="sub">{{ $t('apps.detached_cmds') }}</h3>
      <p class="form-help">{{ $t('apps2.detached_help') }}</p>
      <div class="card-pad"><CommandList :value="a.detached" :fields="DETACHED_FIELDS" add-label="apps.detached_cmds_add" @update="a.detached = $event" /></div>
    </section>

    <details class="section env">
      <summary>{{ $t('apps.env_vars_about') }}</summary>
      <p class="form-help">{{ $t('apps2.env_help') }}</p>
      <table class="env-table">
        <tr v-for="v in envVars" :key="v"><td class="mono">APOLLO_{{ v }}</td><td>{{ $t(envHelp[v]) }}</td></tr>
      </table>
      <p class="form-help">{{ $t('apps.env_sunshine_compatibility') }}</p>
    </details>
  </form>
</template>

<style scoped>
.editor { display: flex; flex-direction: column; gap: 16px; }
.editor-head {
  display: flex; align-items: center; justify-content: space-between; gap: 12px; flex-wrap: wrap;
  position: sticky; top: 56px; z-index: 5; background: var(--background); padding: 10px 0;
}
.editor-buttons { display: flex; gap: 8px; flex-wrap: wrap; }
.two { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.image-row { display: flex; gap: 8px; }
.covers { display: grid; grid-template-columns: repeat(auto-fill, minmax(110px, 1fr)); gap: 10px; margin-top: 8px; }
.cover { border: 1px solid var(--border); background: var(--field); border-radius: 6px; padding: 6px; cursor: pointer; color: inherit; display: flex; flex-direction: column; gap: 4px; text-align: left; }
.cover img { width: 100%; aspect-ratio: 3 / 4; object-fit: cover; border-radius: 4px; }
.cover:hover { border-color: var(--accent); }
.sub { font-size: 15px; margin-bottom: 6px; }
.section { margin-bottom: 0; }
.indented { margin-left: var(--indent); }
.range { width: 100%; margin-top: 8px; accent-color: var(--accent-strong); }
.env summary { cursor: pointer; font-weight: 600; color: var(--text-secondary); }
.env-table { border-collapse: collapse; font-size: 13.5px; margin: 8px 0; }
.env-table td { padding: 5px 10px 5px 0; border-bottom: 1px solid var(--row-divider); vertical-align: top; }
@media (max-width: 600px) { .two { grid-template-columns: 1fr; } }
</style>

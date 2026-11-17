<script setup>
import { computed, inject } from 'vue'
import { isEnumInteger, LIST_TYPES } from './model'
import CommandList from './CommandList.vue'
import KeybindingsEditor from './KeybindingsEditor.vue'
import ModeRemappingEditor from './ModeRemappingEditor.vue'

const props = defineProps({
  setting: { type: Object, required: true },
  value: { required: true },
  modified: Boolean,  // Differs from what is saved
  canReset: Boolean,  // Differs from the default
  defaultText: { type: String, default: '' },
  error: { type: String, default: '' },
  invalidEntry: { type: Object, default: null },  // {value, error} stored in the file
  dependencyText: { type: String, default: '' },  // Set when the setting has no effect now
  indented: Boolean,
  showTier: Boolean,  // Mark advanced settings (in search results)
  showKey: Boolean,  // Show the name in the config file (advanced settings on)
  platform: { type: String, default: '' },
  remapGroup: { type: String, default: 'mixed' },
  highlighted: Boolean,
  displays: { type: Array, default: null },  // GET /api/v1/displays, for the monitor picker
})
const emit = defineEmits(['update', 'reset'])

const PREP_FIELDS = [{ name: 'do', label: 'settings.cmd_do' }, { name: 'undo', label: 'settings.cmd_undo' }]
const SERVER_FIELDS = [{ name: 'name', label: 'settings.cmd_name' }, { name: 'cmd', label: 'settings.cmd_cmd' }]

const i18n = inject('i18n')
const t = (...args) => i18n.t(...args)

const s = computed(() => props.setting)

// The explanation lives in the docs and on hover, so rows stay one line.
const tooltip = computed(() => [
  props.setting.help,
  props.dependencyText,
  props.showKey ? 'sunshine.conf: ' + props.setting.key : '',
].filter(Boolean).join('\n\n'))
const id = computed(() => 'setting-' + s.value.key)
const wide = computed(() => LIST_TYPES.includes(s.value.type))
const enumInteger = computed(() => isEnumInteger(s.value))
// The default entry of a list is marked in the list itself, so the names stay short.
const markDefault = (value, title) => String(value) === String(s.value.default) ? t('settings.default_choice', { title }) : title
const choices = computed(() => {
  if (s.value.type === 'choice') {
    return s.value.choices.map((v) => {
      const title = s.value.value_titles?.[v] ?? (v === '' ? t('settings.automatic') : v)
      return { value: v, title: markDefault(v, title) }
    })
  }
  const list = []
  for (let n = s.value.min; n <= s.value.max; n++) {
    list.push({ value: n, title: markDefault(n, s.value.value_titles[String(n)]) })
  }
  return list
})
const textValue = computed(() => props.value === null || props.value === undefined ? '' : String(props.value))

// Monitor picker for output_name, from the host's display list.
const displayPicker = computed(() => s.value.key === 'output_name' && Array.isArray(props.displays) && props.displays.length > 0)
const displayChoices = computed(() => {
  const list = [{ value: '', title: t('settings.display_main') }]
  for (const d of props.displays || []) {
    const name = d.friendly_name || d.name
    const mode = d.active ? ` (${d.width}×${d.height}, ${Math.round(d.refresh_rate)} Hz)` : ` (${t('settings.display_off')})`
    list.push({ value: d.id, title: name + mode })
  }
  if (textValue.value !== '' && !list.some((c) => c.value === textValue.value)) {
    list.push({ value: textValue.value, title: t('settings.display_missing', { id: textValue.value }) })
  }
  return list
})

// Numbers: a unit after the field, bitrate in Mbps, and a checkbox for a special value
// such as 0 = "No limit" or -1 = "Off".
const UNIT_TEXT = { ms: 'ms', percent: '%', fps: 'fps', per_second: '/s', kbps: 'Mbps' }
const scale = computed(() => s.value.unit === 'kbps' ? 1000 : 1)
const unitText = computed(() => UNIT_TEXT[s.value.unit] || '')
const special = computed(() => {
  if (enumInteger.value || !(s.value.type === 'integer' || s.value.type === 'number')) {
    return null
  }
  const entries = Object.entries(s.value.value_titles || {})
  return entries.length === 1 ? { value: Number(entries[0][0]), title: entries[0][1] } : null
})
const isSpecial = computed(() => special.value !== null && textValue.value !== '' && Number(textValue.value) === special.value.value)
// A sensible value to start from when the special value is unticked.
const START = { max_bitrate: 50000, back_button_timeout: 2000, minimum_fps_target: 20, dd_wa_hdr_toggle_delay: 500 }
function toggleSpecial(on) {
  if (on) {
    emit('update', special.value.value)
  } else {
    const fallback = Number(s.value.default) !== special.value.value ? s.value.default : (START[s.value.key] ?? special.value.value + 1)
    emit('update', fallback)
  }
}
const shownNumber = computed(() => {
  if (isSpecial.value || textValue.value === '') {
    return ''
  }
  const n = Number(textValue.value)
  return Number.isFinite(n) ? String(n / scale.value) : textValue.value
})
function numberInput(text) {
  // Accept a decimal comma as well as a point.
  const clean = text.trim().replace(',', '.')
  const n = Number(clean)
  if (clean !== '' && Number.isFinite(n) && scale.value !== 1) {
    emit('update', Math.round(n * scale.value))
  } else {
    emit('update', clean)
  }
}
</script>

<template>
  <div class="row" :class="{ modified, indented, wide, inactive: !!dependencyText, highlighted, 'has-error': !!error }"
       :id="s.key">
    <div class="row-main">
      <div class="row-text">
        <label class="row-title" :for="wide ? null : id" :id="id + '-label'" :title="tooltip">
          {{ s.title }}
          <span v-if="s.tier === 'dangerous'" class="badge badge-careful">{{ $t('settings.careful') }}</span>
          <span v-else-if="showTier && s.tier === 'advanced'" class="badge">{{ $t('settings.advanced') }}</span>
        </label>
        <span v-if="s.help" class="visually-hidden" :id="id + '-help'">{{ s.help }}</span>
      </div>

      <div v-if="!wide" class="row-control">
        <label v-if="s.type === 'boolean'" class="switch">
          <input type="checkbox" role="switch" :id="id" :checked="!!value" :aria-describedby="s.help ? id + '-help' : null"
                 @change="emit('update', $event.target.checked)">
          <span class="track"></span>
        </label>

        <select v-else-if="displayPicker" class="field" :id="id" :aria-describedby="s.help ? id + '-help' : null"
                @change="emit('update', $event.target.value)">
          <option v-for="c in displayChoices" :key="c.value" :value="c.value" :selected="c.value === textValue">{{ c.title }}</option>
        </select>

        <select v-else-if="s.type === 'choice' || enumInteger" class="field" :id="id"
                :aria-describedby="s.help ? id + '-help' : null"
                @change="emit('update', s.type === 'choice' ? $event.target.value : Number($event.target.value))">
          <option v-for="c in choices" :key="c.value" :value="c.value" :selected="String(c.value) === textValue">
            {{ c.title ?? $t('settings.automatic') }}
          </option>
          <!-- A stored value this host doesn't offer (for example a gamepad type from another platform). -->
          <option v-if="!choices.some((c) => String(c.value) === textValue)" :value="textValue" selected>{{ textValue }}</option>
        </select>

        <div v-else-if="s.type === 'integer' || s.type === 'number'" class="number-control">
          <label v-if="special" class="special">
            <input type="checkbox" :checked="isSpecial" @change="toggleSpecial($event.target.checked)">
            {{ special.title }}
          </label>
          <input v-if="!isSpecial" type="text" :inputmode="s.type === 'number' || scale !== 1 ? 'decimal' : 'numeric'"
                 class="field number" :id="id" :class="{ invalid: !!error }" :value="shownNumber"
                 :aria-describedby="s.help ? id + '-help' : null" @input="numberInput($event.target.value)">
          <span v-if="unitText && !isSpecial" class="unit">{{ unitText }}</span>
        </div>

        <input v-else type="text" class="field" :class="{ mono: s.type === 'path', invalid: !!error }" :id="id"
               :value="textValue" :placeholder="$t('settings.automatic')" spellcheck="false"
               :aria-describedby="s.help ? id + '-help' : null"
               @input="emit('update', $event.target.value)">
      </div>
    </div>

    <div v-if="wide" class="row-editor" role="group" :aria-labelledby="id + '-label'">
      <CommandList v-if="s.type === 'prep_commands' || s.type === 'server_commands'" :value="value"
                   :fields="s.type === 'prep_commands' ? PREP_FIELDS : SERVER_FIELDS"
                   :elevated="platform === 'windows'" @update="emit('update', $event)" />
      <KeybindingsEditor v-else-if="s.type === 'keybindings'" :value="value" @update="emit('update', $event)" />
      <ModeRemappingEditor v-else-if="s.type === 'mode_remapping'" :value="value" :active-group="remapGroup"
                           @update="emit('update', $event)" />
    </div>

    <p v-if="invalidEntry && !modified" class="row-note small warning-text">
      <i class="fas fa-triangle-exclamation" aria-hidden="true"></i>
      {{ $t('settings.stored_invalid', { value: invalidEntry.value, error: invalidEntry.error }) }}
    </p>
    <p v-if="error" class="row-error small" role="alert">
      <i class="fas fa-circle-exclamation" aria-hidden="true"></i> {{ error }}
    </p>
    <div v-if="canReset || (invalidEntry && !modified)" class="row-reset small">
      <button type="button" class="link-button" :title="$t('settings.default_is', { value: defaultText })" @click="emit('reset')">
        <i class="fas fa-rotate-left" aria-hidden="true"></i> {{ $t('settings.reset') }}
      </button>
    </div>
  </div>
</template>

<style scoped>
.row {
  position: relative; padding: 14px 0; min-height: var(--row-min-height);
  border-top: 1px solid var(--row-divider);
  scroll-margin-top: 140px;
}
.row:first-child { border-top: 0; }
.row.indented { margin-left: var(--indent); }
.row.modified::before {
  content: ""; position: absolute; left: calc(-1 * var(--card-padding)); top: 10px; bottom: 10px;
  width: 3px; border-radius: 2px; background: var(--accent);
}
.row.indented.modified::before { left: calc(-1 * var(--card-padding) - var(--indent)); }
.row.has-error::before { background: var(--error); content: ""; position: absolute; left: calc(-1 * var(--card-padding)); top: 10px; bottom: 10px; width: 3px; border-radius: 2px; }
.row.highlighted { animation: flash 1.6s ease-out; }
@keyframes flash {
  0%, 30% { background: color-mix(in srgb, var(--accent) 16%, transparent); }
  100% { background: transparent; }
}
.row-main { display: flex; gap: 16px; align-items: flex-start; justify-content: space-between; }
.row-text { flex: 1 1 auto; min-width: 0; }
.row-title { display: block; font-weight: 400; color: var(--text-heading); font-size: 16px; }
.row-title .badge { margin-left: 6px; }
.row-help { margin: 3px 0 0; color: var(--text-secondary); font-size: 14px; white-space: pre-line; max-width: 62ch; }
.row.inactive .row-title, .row.inactive .row-help { color: var(--text-disabled); }
.row-note { margin: 6px 0 0; color: var(--text-tertiary); }
.row-key { margin: 4px 0 0; font-size: 12px; color: var(--text-tertiary); }
.row-key code { font-size: 12px; user-select: all; }
.warning-text { color: var(--warning); }
.row-control { flex: 0 0 auto; width: min(360px, 45%); display: flex; justify-content: flex-end; padding-top: 2px; }
.row-control .field.number { width: 110px; text-align: right; }
.number-control { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; justify-content: flex-end; }
.number-control .special { display: flex; align-items: center; gap: 6px; color: var(--text-primary); cursor: pointer; }
.number-control .unit { color: var(--text-secondary); min-width: 2.5em; }
.row-editor { margin-top: 12px; }
.row-error { margin: 8px 0 0; color: var(--error); font-weight: 500; }
.row-reset { margin-top: 6px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
.link-button {
  border: 0; background: none; padding: 2px 0; color: var(--accent); font: inherit; cursor: pointer; font-weight: 500;
}
.link-button:hover { text-decoration: underline; }
@media (max-width: 640px) {
  .row-main { flex-direction: column; gap: 10px; }
  .row-control { width: 100%; justify-content: flex-start; }
  .row.indented { margin-left: 14px; }
}
</style>

<script setup>
import { computed } from 'vue'
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
  platform: { type: String, default: '' },
  remapGroup: { type: String, default: 'mixed' },
  highlighted: Boolean,
})
const emit = defineEmits(['update', 'reset'])

const PREP_FIELDS = [{ name: 'do', label: 'settings.cmd_do' }, { name: 'undo', label: 'settings.cmd_undo' }]
const SERVER_FIELDS = [{ name: 'name', label: 'settings.cmd_name' }, { name: 'cmd', label: 'settings.cmd_cmd' }]

const s = computed(() => props.setting)
const id = computed(() => 'setting-' + s.value.key)
const wide = computed(() => LIST_TYPES.includes(s.value.type))
const enumInteger = computed(() => isEnumInteger(s.value))
const choices = computed(() => {
  if (s.value.type === 'choice') {
    return s.value.choices.map((v) => ({ value: v, title: s.value.value_titles?.[v] ?? (v === '' ? null : v) }))
  }
  const list = []
  for (let n = s.value.min; n <= s.value.max; n++) {
    list.push({ value: n, title: s.value.value_titles[String(n)] })
  }
  return list
})
const numberStep = computed(() => s.value.type === 'number' ? 'any' : '1')
const textValue = computed(() => props.value === null || props.value === undefined ? '' : String(props.value))
</script>

<template>
  <div class="row" :class="{ modified, indented, wide, inactive: !!dependencyText, highlighted, 'has-error': !!error }"
       :id="s.key">
    <div class="row-main">
      <div class="row-text">
        <label class="row-title" :for="wide ? null : id" :id="id + '-label'">
          {{ s.title }}
          <span v-if="s.tier === 'dangerous'" class="badge badge-careful">{{ $t('settings.careful') }}</span>
          <span v-else-if="showTier && s.tier === 'advanced'" class="badge">{{ $t('settings.advanced') }}</span>
        </label>
        <p v-if="s.help" class="row-help" :id="id + '-help'">{{ s.help }}</p>
        <p v-if="dependencyText" class="row-note small">
          <i class="fas fa-circle-info" aria-hidden="true"></i> {{ dependencyText }}
        </p>
      </div>

      <div v-if="!wide" class="row-control">
        <label v-if="s.type === 'boolean'" class="switch">
          <input type="checkbox" role="switch" :id="id" :checked="!!value" :aria-describedby="s.help ? id + '-help' : null"
                 @change="emit('update', $event.target.checked)">
          <span class="track"></span>
        </label>

        <select v-else-if="s.type === 'choice' || enumInteger" class="field" :id="id"
                :aria-describedby="s.help ? id + '-help' : null"
                @change="emit('update', s.type === 'choice' ? $event.target.value : Number($event.target.value))">
          <option v-for="c in choices" :key="c.value" :value="c.value" :selected="String(c.value) === textValue">
            {{ c.title ?? $t('settings.automatic') }}
          </option>
          <!-- A stored value this host doesn't offer (for example a gamepad type from another platform). -->
          <option v-if="!choices.some((c) => String(c.value) === textValue)" :value="textValue" selected>{{ textValue }}</option>
        </select>

        <input v-else-if="s.type === 'integer' || s.type === 'number'" type="number" class="field number" :id="id"
               :class="{ invalid: !!error }" :value="textValue" :min="s.min" :max="s.max" :step="numberStep"
               :placeholder="String(s.default)" :aria-describedby="s.help ? id + '-help' : null"
               @input="emit('update', $event.target.value)">

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
      <span class="muted">{{ $t('settings.default_is', { value: defaultText }) }}</span>
      <button type="button" class="link-button" @click="emit('reset')">
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
.row-title { display: block; font-weight: 600; color: var(--text-primary); font-size: 15px; }
.row-title .badge { margin-left: 6px; }
.row-help { margin: 3px 0 0; color: var(--text-secondary); font-size: 13.5px; white-space: pre-line; max-width: 62ch; }
.row.inactive .row-title, .row.inactive .row-help { color: var(--text-disabled); }
.row-note { margin: 6px 0 0; color: var(--text-tertiary); }
.warning-text { color: var(--warning); }
.row-control { flex: 0 0 auto; width: 280px; display: flex; justify-content: flex-end; padding-top: 2px; }
.row-control .field.number { width: 140px; }
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

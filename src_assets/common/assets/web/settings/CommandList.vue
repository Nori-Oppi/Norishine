<script setup>
import { computed } from 'vue'
import { clone } from './model'

// Editor for prep_commands ({do, undo, elevated}) and server_commands ({name, cmd, elevated}).
const props = defineProps({
  value: { type: Array, default: () => [] },
  kind: { type: String, required: true },  // "prep_commands" or "server_commands"
  windows: Boolean,  // "Run as administrator" only exists on Windows
  disabled: Boolean,
  idPrefix: { type: String, required: true },
})
const emit = defineEmits(['update'])

const fields = computed(() => props.kind === 'prep_commands'
  ? [{ name: 'do', label: 'settings.cmd_do' }, { name: 'undo', label: 'settings.cmd_undo' }]
  : [{ name: 'name', label: 'settings.cmd_name' }, { name: 'cmd', label: 'settings.cmd_cmd' }])

const rows = computed(() => Array.isArray(props.value) ? props.value : [])

function change(mutate) {
  const next = clone(rows.value)
  mutate(next)
  emit('update', next)
}

function add() {
  change((list) => list.push(props.kind === 'prep_commands' ? { do: '', undo: '', elevated: false } : { name: '', cmd: '', elevated: false }))
}
function move(i, delta) {
  change((list) => list.splice(i + delta, 0, list.splice(i, 1)[0]))
}
</script>

<template>
  <div class="list-editor">
    <div v-for="(row, i) in rows" :key="i" class="entry">
      <div class="entry-fields">
        <label v-for="f in fields" :key="f.name" class="entry-field">
          <span class="small muted">{{ $t(f.label) }}</span>
          <input type="text" class="field mono" :value="row[f.name]" :disabled="disabled" spellcheck="false"
                 @input="change((list) => list[i][f.name] = $event.target.value)">
        </label>
      </div>
      <div class="entry-side">
        <label v-if="windows" class="elevated small">
          <input type="checkbox" :checked="!!row.elevated" :disabled="disabled"
                 @change="change((list) => list[i].elevated = $event.target.checked)">
          {{ $t('settings.cmd_elevated') }}
        </label>
        <div class="entry-buttons">
          <button type="button" class="btn btn-icon" :disabled="disabled || i === 0" :title="$t('settings.move_up')"
                  @click="move(i, -1)"><i class="fas fa-arrow-up" aria-hidden="true"></i><span class="visually-hidden">{{ $t('settings.move_up') }}</span></button>
          <button type="button" class="btn btn-icon" :disabled="disabled || i === rows.length - 1" :title="$t('settings.move_down')"
                  @click="move(i, 1)"><i class="fas fa-arrow-down" aria-hidden="true"></i><span class="visually-hidden">{{ $t('settings.move_down') }}</span></button>
          <button type="button" class="btn btn-icon" :disabled="disabled" :title="$t('settings.remove')"
                  @click="change((list) => list.splice(i, 1))"><i class="fas fa-trash-can" aria-hidden="true"></i><span class="visually-hidden">{{ $t('settings.remove') }}</span></button>
        </div>
      </div>
    </div>
    <p v-if="rows.length === 0" class="small muted empty">{{ $t('settings.no_entries') }}</p>
    <button type="button" class="btn btn-small" :disabled="disabled" @click="add">
      <i class="fas fa-plus" aria-hidden="true"></i> {{ $t('settings.add_command') }}
    </button>
  </div>
</template>

<style scoped>
.list-editor { display: flex; flex-direction: column; gap: 8px; align-items: flex-start; }
.entry {
  width: 100%; display: flex; gap: 12px; align-items: flex-end; flex-wrap: wrap;
  padding: 10px 12px; border: 1px solid var(--row-divider); border-radius: 6px;
}
.entry-fields { flex: 1 1 360px; display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.entry-field { display: flex; flex-direction: column; gap: 3px; min-width: 0; }
.entry-side { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
.elevated { display: flex; align-items: center; gap: 6px; color: var(--text-secondary); white-space: nowrap; }
.entry-buttons { display: flex; }
.empty { margin: 0; }
@media (max-width: 560px) { .entry-fields { grid-template-columns: 1fr; } }
</style>

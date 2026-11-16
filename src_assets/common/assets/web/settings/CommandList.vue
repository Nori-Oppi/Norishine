<script setup>
import { computed } from 'vue'
import { clone } from './model'

// Editor for a list of commands: rows of text fields, an optional "Run as administrator"
// (Windows only), reordering and removal. Used for settings, apps and devices.
const props = defineProps({
  value: { type: Array, default: () => [] },
  // Text fields per row: [{name, label}]. A single field named "" edits plain strings.
  fields: { type: Array, required: true },
  elevated: Boolean,  // show "Run as administrator"
  addLabel: { type: String, default: 'settings.add_command' },
  disabled: Boolean,
})
const emit = defineEmits(['update'])

const plain = computed(() => props.fields.length === 1 && props.fields[0].name === '')
const rows = computed(() => Array.isArray(props.value) ? props.value : [])

function change(mutate) {
  const next = clone(rows.value)
  mutate(next)
  emit('update', next)
}
function add() {
  change((list) => {
    if (plain.value) {
      list.push('')
      return
    }
    const item = Object.fromEntries(props.fields.map((f) => [f.name, '']))
    if (props.elevated) {
      item.elevated = false
    }
    list.push(item)
  })
}
function move(i, delta) {
  change((list) => list.splice(i + delta, 0, list.splice(i, 1)[0]))
}
function setField(i, name, value) {
  change((list) => {
    if (plain.value) {
      list[i] = value
    } else {
      list[i][name] = value
    }
  })
}
</script>

<template>
  <div class="list-editor">
    <div v-for="(row, i) in rows" :key="i" class="entry">
      <div class="entry-fields" :style="{ '--cols': fields.length }">
        <label v-for="f in fields" :key="f.name" class="entry-field">
          <span v-if="f.label" class="small muted">{{ $t(f.label) }}</span>
          <input type="text" class="field mono" :value="plain ? row : row[f.name]" :disabled="disabled" spellcheck="false"
                 :aria-label="f.label ? $t(f.label) : null" @input="setField(i, f.name, $event.target.value)">
        </label>
      </div>
      <div class="entry-side">
        <label v-if="elevated && !plain" class="elevated small">
          <input type="checkbox" :checked="row.elevated === true || row.elevated === 'true'" :disabled="disabled"
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
      <i class="fas fa-plus" aria-hidden="true"></i> {{ $t(addLabel) }}
    </button>
  </div>
</template>

<style scoped>
.list-editor { display: flex; flex-direction: column; gap: 8px; align-items: flex-start; }
.entry {
  width: 100%; display: flex; gap: 12px; align-items: flex-end; flex-wrap: wrap;
  padding: 10px 12px; border: 1px solid var(--row-divider); border-radius: 6px;
}
.entry-fields { flex: 1 1 360px; display: grid; grid-template-columns: repeat(var(--cols), minmax(0, 1fr)); gap: 8px; }
.entry-field { display: flex; flex-direction: column; gap: 3px; min-width: 0; }
.entry-side { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
.elevated { display: flex; align-items: center; gap: 6px; color: var(--text-secondary); white-space: nowrap; }
.entry-buttons { display: flex; }
.empty { margin: 0; }
@media (max-width: 560px) { .entry-fields { grid-template-columns: 1fr; } }
</style>

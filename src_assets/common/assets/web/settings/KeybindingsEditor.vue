<script setup>
import { computed } from 'vue'
import { parseKeyCodes, parseKeyCode, hex, KEY_NAMES, clone } from './model'

// Key mappings are stored as "[from,to,from,to,...]"; the editor works on rows of
// {from, to} text so half-typed codes survive until they are fixed.
const props = defineProps({
  value: { type: [String, Array], default: '[]' },
  disabled: Boolean,
})
const emit = defineEmits(['update'])

const rows = computed(() => {
  if (Array.isArray(props.value)) {
    return props.value
  }
  const codes = parseKeyCodes(props.value) || []
  const list = []
  for (let i = 0; i + 1 < codes.length; i += 2) {
    list.push({ from: hex(codes[i]), to: hex(codes[i + 1]) })
  }
  return list
})

function change(mutate) {
  const next = clone(rows.value)
  mutate(next)
  emit('update', next)
}

function name(text) {
  const code = parseKeyCode(text)
  return code === null ? '' : (KEY_NAMES[code] || '')
}
const invalid = (text) => String(text ?? '').trim() !== '' && parseKeyCode(text) === null
</script>

<template>
  <div class="list-editor">
    <p class="small muted hint">{{ $t('settings.key_hint') }}</p>
    <div v-if="rows.length" class="table">
      <div class="head small muted">
        <span>{{ $t('settings.key_from') }}</span><span></span><span>{{ $t('settings.key_to') }}</span><span></span>
      </div>
      <div v-for="(row, i) in rows" :key="i" class="line">
        <div class="cell">
          <input type="text" class="field mono" :class="{ invalid: invalid(row.from) }" :value="row.from" :disabled="disabled"
                 :aria-label="$t('settings.key_from')" spellcheck="false" placeholder="0x10"
                 @input="change((l) => l[i].from = $event.target.value)">
          <span class="small muted keyname">{{ name(row.from) }}</span>
        </div>
        <i class="fas fa-arrow-right muted" aria-hidden="true"></i>
        <div class="cell">
          <input type="text" class="field mono" :class="{ invalid: invalid(row.to) }" :value="row.to" :disabled="disabled"
                 :aria-label="$t('settings.key_to')" spellcheck="false" placeholder="0xA0"
                 @input="change((l) => l[i].to = $event.target.value)">
          <span class="small muted keyname">{{ name(row.to) }}</span>
        </div>
        <button type="button" class="btn btn-icon" :disabled="disabled" :title="$t('settings.remove')"
                @click="change((l) => l.splice(i, 1))"><i class="fas fa-trash-can" aria-hidden="true"></i><span class="visually-hidden">{{ $t('settings.remove') }}</span></button>
      </div>
      <p v-if="rows.some((r) => invalid(r.from) || invalid(r.to))" class="small error-text">{{ $t('settings.key_invalid') }}</p>
    </div>
    <p v-else class="small muted">{{ $t('settings.no_entries') }}</p>
    <button type="button" class="btn btn-small" :disabled="disabled" @click="change((l) => l.push({ from: '', to: '' }))">
      <i class="fas fa-plus" aria-hidden="true"></i> {{ $t('settings.add_key') }}
    </button>
  </div>
</template>

<style scoped>
.list-editor { display: flex; flex-direction: column; gap: 8px; align-items: flex-start; }
.hint { margin: 0; }
.table { display: flex; flex-direction: column; gap: 6px; }
.head, .line { display: grid; grid-template-columns: minmax(0, 220px) 20px minmax(0, 220px) 32px; gap: 10px; align-items: center; }
.cell { display: flex; flex-direction: column; gap: 2px; }
.keyname { min-height: 18px; padding-left: 2px; }
.line > i { margin-bottom: 18px; }
.line > .btn { margin-bottom: 18px; }
</style>

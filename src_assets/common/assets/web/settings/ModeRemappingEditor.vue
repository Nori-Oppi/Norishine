<script setup>
import { computed } from 'vue'
import { REMAP_GROUPS, remapFields, clone } from './model'

// Display mode remapping: three lists, of which the host uses the one that matches the
// resolution and refresh rate options.
const props = defineProps({
  value: { type: Object, default: () => ({}) },
  activeGroup: { type: String, default: 'mixed' },
  disabled: Boolean,
})
const emit = defineEmits(['update'])

const placeholders = {
  requested_resolution: '1920x1080', requested_fps: '60', final_resolution: '2560x1440', final_refresh_rate: '119.95',
}

const lists = computed(() => Object.fromEntries(REMAP_GROUPS.map((g) => [g, Array.isArray(props.value?.[g]) ? props.value[g] : []])))
// The list in use first; the others only when they have entries.
const shown = computed(() => [props.activeGroup, ...REMAP_GROUPS.filter((g) => g !== props.activeGroup && lists.value[g].length)])

function change(mutate) {
  const next = clone(lists.value)
  mutate(next)
  emit('update', next)
}
function add(group) {
  change((l) => l[group].push(Object.fromEntries(remapFields(group).map((f) => [f, '']))))
}
</script>

<template>
  <div class="remap">
    <section v-for="group in shown" :key="group" class="group">
      <h3>
        {{ $t('settings.remap_' + group) }}
        <span v-if="group === activeGroup" class="badge badge-accent">{{ $t('settings.remap_in_use') }}</span>
      </h3>
      <div v-if="lists[group].length" class="rows">
        <div class="line head small muted" :style="{ '--cols': remapFields(group).length }">
          <span v-for="f in remapFields(group)" :key="f">{{ $t('config.dd_mode_remapping_' + f) }}</span><span></span>
        </div>
        <div v-for="(entry, i) in lists[group]" :key="i" class="line" :style="{ '--cols': remapFields(group).length }">
          <input v-for="f in remapFields(group)" :key="f" type="text" class="field mono" :value="entry[f]"
                 :placeholder="placeholders[f]" :aria-label="$t('config.dd_mode_remapping_' + f)" :disabled="disabled"
                 spellcheck="false" @input="change((l) => l[group][i][f] = $event.target.value)">
          <button type="button" class="btn btn-icon" :disabled="disabled" :title="$t('settings.remove')"
                  @click="change((l) => l[group].splice(i, 1))"><i class="fas fa-trash-can" aria-hidden="true"></i><span class="visually-hidden">{{ $t('settings.remove') }}</span></button>
        </div>
      </div>
      <p v-else class="small muted">{{ $t('settings.no_entries') }}</p>
      <button type="button" class="btn btn-small" :disabled="disabled" @click="add(group)">
        <i class="fas fa-plus" aria-hidden="true"></i> {{ $t('settings.remap_add') }}
      </button>
    </section>
  </div>
</template>

<style scoped>
.remap { display: flex; flex-direction: column; gap: 16px; }
.group { display: flex; flex-direction: column; gap: 8px; align-items: flex-start; }
.group h3 { font-size: 13.5px; color: var(--text-secondary); display: flex; gap: 8px; align-items: center; }
.rows { width: 100%; display: flex; flex-direction: column; gap: 6px; }
.line { display: grid; grid-template-columns: repeat(var(--cols), minmax(0, 1fr)) 32px; gap: 8px; align-items: center; }
.group p { margin: 0; }
</style>

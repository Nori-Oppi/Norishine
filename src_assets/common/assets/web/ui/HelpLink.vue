<script setup>
// A small "?" beside a page title, linking to the explanation in the documentation, so the
// page itself can stay short.
import { computed } from 'vue'

const props = defineProps({
  // A wiki page ("Norishine-Guide#apps"), or a file in docs/ ("SETTINGS.md#video").
  page: { type: String, required: true },
  label: { type: String, required: true },
})
const WIKI = 'https://github.com/Nori-Oppi/norilight-docs/wiki/'
const DOCS = 'https://github.com/Nori-Oppi/Norishine/blob/master/docs/'
const href = computed(() => (/^[^#]+\.md(#|$)/.test(props.page) ? DOCS : WIKI) + props.page)
</script>

<template>
  <a class="help-link" :href="href" target="_blank" rel="noopener" :title="label">
    <i class="fas fa-circle-question" aria-hidden="true"></i><span class="visually-hidden">{{ label }}</span>
  </a>
</template>

<style scoped>
.help-link {
  display: inline-flex; align-items: center; justify-content: center; width: 30px; height: 30px; border-radius: 50%;
  color: var(--text-tertiary); font-size: 17px; text-decoration: none; vertical-align: middle; margin-left: 4px;
}
.help-link:hover { color: var(--accent); background: var(--nav-hover); }
</style>

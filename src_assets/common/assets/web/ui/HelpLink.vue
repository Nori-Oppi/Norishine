<script setup>
// A small "?" beside a page title, linking to the explanation in the documentation, so the
// page itself can stay short.
defineProps({
  page: { type: String, required: true },  // file in docs/, e.g. "GUIDE.md#devices"
  label: { type: String, required: true },
})
const DOCS = 'https://github.com/Nori-Oppi/Norishine/blob/master/docs/'
</script>

<template>
  <a class="help-link" :href="DOCS + page" target="_blank" rel="noopener" :title="label">
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

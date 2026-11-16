import '../csrf'
import i18n from '../locale'
import { applyStoredTheme } from './theme'

/**
 * Start a page built on the Norishine theme: load the translations, then mount.
 * Unlike ../init.js this does not load Bootstrap.
 */
export function startPage(app) {
  applyStoredTheme()
  i18n().then((i18n) => {
    app.use(i18n)
    app.provide('i18n', i18n.global)
    app.mount('#app')
  })
}

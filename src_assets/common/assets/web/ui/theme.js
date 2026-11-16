// Light, dark or automatic (follow the system). Shared with the older pages, which read
// the same "theme" entry.
const KEY = 'theme'

export function storedTheme() {
  try {
    const value = localStorage.getItem(KEY)
    return value === 'light' || value === 'dark' ? value : 'auto'
  } catch {
    return 'auto'
  }
}

export function applyTheme(theme) {
  const root = document.documentElement
  if (theme === 'light' || theme === 'dark') {
    root.setAttribute('data-theme', theme)
  } else {
    root.removeAttribute('data-theme')
  }
  // Older Bootstrap pages use data-bs-theme.
  const dark = theme === 'dark' || (theme === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches)
  root.setAttribute('data-bs-theme', dark ? 'dark' : 'light')
}

export function setTheme(theme) {
  try {
    localStorage.setItem(KEY, theme)
  } catch {
    // Private windows may refuse; the choice then lasts for this page only.
  }
  applyTheme(theme)
}

export function applyStoredTheme() {
  applyTheme(storedTheme())
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => applyTheme(storedTheme()))
}

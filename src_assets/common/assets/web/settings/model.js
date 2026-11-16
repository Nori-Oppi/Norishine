// Settings page logic that doesn't depend on Vue: comparing values, working out what to
// send to PATCH /api/v1/config, searching and formatting.

/** Draft value meaning "remove the setting from the config file" (back to the default). */
export const RESET = Symbol('reset')

export const LIST_TYPES = ['prep_commands', 'server_commands', 'keybindings', 'mode_remapping']

export function clone(value) {
  return value === undefined ? undefined : JSON.parse(JSON.stringify(value))
}

const blank = (v) => v === undefined || v === null || String(v).trim() === ''

/** Parse "[0x10,0xA0,17,162]" into numbers, or null when it isn't a list of key codes. */
export function parseKeyCodes(text) {
  if (Array.isArray(text)) {
    return text
  }
  const m = /^\s*\[(.*)\]\s*$/s.exec(String(text ?? ''))
  if (!m) {
    return null
  }
  if (m[1].trim() === '') {
    return []
  }
  const codes = []
  for (const part of m[1].split(',')) {
    const code = parseKeyCode(part)
    if (code === null) {
      return null
    }
    codes.push(code)
  }
  return codes
}

export function parseKeyCode(text) {
  const t = String(text ?? '').trim()
  if (!/^(0x[0-9a-f]+|\d+)$/i.test(t)) {
    return null
  }
  const n = t.toLowerCase().startsWith('0x') ? parseInt(t, 16) : parseInt(t, 10)
  return n >= 0 && n <= 0xffff ? n : null
}

export function hex(code) {
  return '0x' + code.toString(16).toUpperCase().padStart(2, '0')
}

const REMAP_FIELDS = {
  mixed: ['requested_resolution', 'requested_fps', 'final_resolution', 'final_refresh_rate'],
  resolution_only: ['requested_resolution', 'final_resolution'],
  refresh_rate_only: ['requested_fps', 'final_refresh_rate'],
}
export const REMAP_GROUPS = Object.keys(REMAP_FIELDS)
export const remapFields = (group) => REMAP_FIELDS[group]

/**
 * A value in a comparable form. Empty list entries are dropped, numbers typed as text
 * become numbers, and null means "nothing entered" (the default).
 */
export function normalize(setting, value) {
  if (value === RESET || value === undefined || value === null) {
    return null
  }
  switch (setting.type) {
    case 'boolean':
      return !!value
    case 'integer':
    case 'number': {
      if (blank(value)) {
        return null
      }
      const n = Number(String(value).trim())
      return Number.isFinite(n) ? n : String(value)
    }
    case 'choice':
      return String(value)
    case 'string':
    case 'path':
      return String(value).trim()
    case 'prep_commands':
      return (Array.isArray(value) ? value : [])
        .filter((c) => !blank(c.do) || !blank(c.undo))
        .map((c) => ({ do: c.do ?? '', undo: c.undo ?? '', elevated: !!c.elevated }))
    case 'server_commands':
      return (Array.isArray(value) ? value : [])
        .filter((c) => !blank(c.name) || !blank(c.cmd))
        .map((c) => ({ name: c.name ?? '', cmd: c.cmd ?? '', elevated: !!c.elevated }))
    case 'keybindings': {
      if (Array.isArray(value)) {
        // Editor rows: [{from, to}] as typed.
        const rows = value.filter((r) => !blank(r.from) || !blank(r.to))
        const codes = []
        for (const r of rows) {
          const a = parseKeyCode(r.from)
          const b = parseKeyCode(r.to)
          if (a === null || b === null) {
            return '[' + rows.map((x) => `${String(x.from).trim()},${String(x.to).trim()}`).join(',') + ']'
          }
          codes.push(a, b)
        }
        return codes
      }
      return parseKeyCodes(value) ?? String(value)
    }
    case 'mode_remapping': {
      const out = {}
      for (const group of REMAP_GROUPS) {
        const entries = (value && Array.isArray(value[group])) ? value[group] : []
        out[group] = entries
          .filter((e) => REMAP_FIELDS[group].some((f) => !blank(e[f])))
          .map((e) => Object.fromEntries(REMAP_FIELDS[group].map((f) => [f, String(e[f] ?? '').trim()])))
      }
      return out
    }
  }
  return value
}

export function same(setting, a, b) {
  return JSON.stringify(normalize(setting, a)) === JSON.stringify(normalize(setting, b))
}

/** The value to send for a normalized value. */
function wire(setting, normalized) {
  if (setting.type === 'keybindings' && Array.isArray(normalized)) {
    return '[' + normalized.map(hex).join(',') + ']'
  }
  return normalized
}

/**
 * Work out the PATCH body from the draft.
 * @param byKey Schema settings by key.
 * @param stored Typed values in the config file (GET "values").
 * @param invalid Entries in the file that don't validate (GET "invalid").
 * @param draft Key to edited value (or RESET).
 */
export function computeChange(byKey, stored, invalid, draft) {
  const set = {}
  const reset = []
  for (const key of Object.keys(draft)) {
    const setting = byKey[key]
    if (!setting) {
      continue
    }
    const value = draft[key]
    const inFile = key in stored || key in invalid
    const n = normalize(setting, value)
    if (key in stored && value !== RESET && same(setting, n, stored[key])) {
      continue
    }
    if (n === null || same(setting, n, setting.default)) {
      if (inFile) {
        reset.push(key)
      }
      continue
    }
    set[key] = wire(setting, n)
  }
  return { set, reset }
}

/** The value a setting has right now in the page: edited, stored or default. */
export function effective(setting, stored, draft) {
  const key = setting.key
  if (key in draft) {
    return draft[key] === RESET ? setting.default : draft[key]
  }
  return key in stored ? stored[key] : setting.default
}

export function isDependencyMet(setting, byKey, stored, draft) {
  const dep = setting.depends_on
  if (!dep || !byKey[dep.key]) {
    return true
  }
  const other = byKey[dep.key]
  const value = normalize(other, effective(other, stored, draft)) ?? normalize(other, other.default)
  return dep.values.some((v) => JSON.stringify(normalize(other, v)) === JSON.stringify(value))
}

/** Readable text for a value, for "Default: ..." and dependency hints. */
export function formatValue(setting, value, t) {
  if (value === RESET || value === undefined) {
    value = setting.default
  }
  const titles = setting.value_titles || {}
  switch (setting.type) {
    case 'boolean':
      return value ? t('settings.on') : t('settings.off')
    case 'choice':
      return titles[value] ?? (value === '' ? t('settings.automatic') : String(value))
    case 'integer':
    case 'number':
      return titles[String(value)] ?? String(value)
    case 'string':
    case 'path':
      return blank(value) ? t('settings.automatic') : String(value)
    case 'keybindings': {
      const codes = normalize(setting, value)
      return Array.isArray(codes) ? t('settings.items', { n: codes.length / 2 }, codes.length / 2) : String(value)
    }
    case 'prep_commands':
    case 'server_commands': {
      const n = normalize(setting, value).length
      return n === 0 ? t('settings.no_entries') : t('settings.items', { n }, n)
    }
    case 'mode_remapping': {
      const v = normalize(setting, value)
      const n = REMAP_GROUPS.reduce((sum, g) => sum + v[g].length, 0)
      return n === 0 ? t('settings.no_entries') : t('settings.items', { n }, n)
    }
  }
  return String(value)
}

/** Whether every word of the query appears in the setting's texts. */
export function matches(setting, words, categoryTitle) {
  if (words.length === 0) {
    return true
  }
  const haystack = [
    setting.title, setting.help, setting.key, categoryTitle,
    ...Object.values(setting.value_titles || {}),
  ].join(' ').toLowerCase()
  return words.every((w) => haystack.includes(w))
}

export function searchWords(query) {
  return query.toLowerCase().split(/\s+/).filter((w) => w !== '')
}

/** An integer with a readable name for every value in its range is shown as a list. */
export function isEnumInteger(setting) {
  if (setting.type !== 'integer' || !setting.value_titles || setting.max === undefined) {
    return false
  }
  for (let n = setting.min; n <= setting.max; n++) {
    if (!(String(n) in setting.value_titles)) {
      return false
    }
  }
  return true
}

/** Windows virtual-key names, for the key mapping editor. */
export const KEY_NAMES = (() => {
  const names = {
    0x08: 'Backspace', 0x09: 'Tab', 0x0D: 'Enter', 0x10: 'Shift', 0x11: 'Ctrl', 0x12: 'Alt',
    0x13: 'Pause', 0x14: 'Caps Lock', 0x1B: 'Esc', 0x20: 'Space', 0x21: 'Page Up', 0x22: 'Page Down',
    0x23: 'End', 0x24: 'Home', 0x25: 'Left', 0x26: 'Up', 0x27: 'Right', 0x28: 'Down',
    0x2C: 'Print Screen', 0x2D: 'Insert', 0x2E: 'Delete', 0x5B: 'Left Windows', 0x5C: 'Right Windows',
    0x5D: 'Menu', 0x90: 'Num Lock', 0x91: 'Scroll Lock',
    0xA0: 'Left Shift', 0xA1: 'Right Shift', 0xA2: 'Left Ctrl', 0xA3: 'Right Ctrl',
    0xA4: 'Left Alt', 0xA5: 'Right Alt',
  }
  for (let i = 0; i <= 9; i++) {
    names[0x30 + i] = String(i)
    names[0x60 + i] = 'Numpad ' + i
  }
  for (let i = 0; i < 26; i++) {
    names[0x41 + i] = String.fromCharCode(65 + i)
  }
  for (let i = 1; i <= 24; i++) {
    names[0x6F + i] = 'F' + i
  }
  return names
})()

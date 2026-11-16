import { reactive } from 'vue'

// One confirmation dialog and a stack of short notices per page, shown by AppShell.
export const dialogState = reactive({ confirm: null, toasts: [] })

/**
 * Ask before doing something that can't be undone.
 * @returns {Promise<boolean>} true when confirmed.
 */
export function confirmAction({ title, body = '', confirm, danger = false }) {
  return new Promise((resolve) => {
    dialogState.confirm = { title, body, confirm, danger, resolve }
  })
}

export function answerConfirm(yes) {
  const c = dialogState.confirm
  dialogState.confirm = null
  if (c) {
    c.resolve(yes)
  }
}

let nextId = 1
/** Show a short notice; errors stay until dismissed. */
export function notify(text, kind = 'ok') {
  const id = nextId++
  dialogState.toasts.push({ id, text, kind })
  if (kind !== 'error') {
    setTimeout(() => dismiss(id), 5000)
  }
}

export function dismiss(id) {
  const i = dialogState.toasts.findIndex((t) => t.id === id)
  if (i >= 0) {
    dialogState.toasts.splice(i, 1)
  }
}

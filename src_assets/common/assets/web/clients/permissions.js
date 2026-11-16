// Device permission bits (crypto::PERM in src/crypto.h).
export const P = {
  input_controller: 0x00000100,
  input_touch: 0x00000200,
  input_pen: 0x00000400,
  input_mouse: 0x00000800,
  input_kbd: 0x00001000,
  clipboard_set: 0x00010000,
  clipboard_read: 0x00020000,
  server_cmd: 0x00100000,
  list: 0x01000000,
  view: 0x02000000,
  launch: 0x04000000,
  manage_read: 0x10000000,
  manage_write: 0x20000000,
  access: 0x07000000,
  manage: 0x30000000,
  inputs: 0x00001F00,
}

// Streaming access is cumulative: launching includes viewing, viewing includes listing.
export const ACCESS_LEVELS = [
  { id: 'none', bits: 0, label: 'devices.access_none', help: 'devices.access_none_help' },
  { id: 'list', bits: P.list, label: 'devices.access_list', help: 'devices.access_list_help' },
  { id: 'view', bits: P.list | P.view, label: 'devices.access_view', help: 'devices.access_view_help' },
  { id: 'launch', bits: P.list | P.view | P.launch, label: 'devices.access_launch', help: 'devices.access_launch_help' },
]

export const MANAGE_LEVELS = [
  { id: 'none', bits: 0, label: 'devices.manage_none', help: 'devices.manage_none_help' },
  { id: 'read', bits: P.manage_read, label: 'devices.manage_read', help: 'devices.manage_read_help' },
  { id: 'write', bits: P.manage_read | P.manage_write, label: 'devices.manage_write', help: 'devices.manage_write_help' },
]

export const INPUTS = [
  { bit: P.input_controller, label: 'permissions.input_controller' },
  { bit: P.input_kbd, label: 'permissions.input_kbd' },
  { bit: P.input_mouse, label: 'permissions.input_mouse' },
  { bit: P.input_touch, label: 'permissions.input_touch' },
  { bit: P.input_pen, label: 'permissions.input_pen' },
]

export const EXTRAS = [
  { bit: P.clipboard_set, label: 'devices.clipboard_set', help: 'devices.clipboard_set_help' },
  { bit: P.clipboard_read, label: 'devices.clipboard_read', help: 'devices.clipboard_read_help' },
  { bit: P.server_cmd, label: 'devices.server_cmd', help: 'devices.server_cmd_help' },
]

export function accessLevel(perm) {
  if (perm & P.launch) return 'launch'
  if (perm & P.view) return 'view'
  if (perm & P.list) return 'list'
  return 'none'
}

export function manageLevel(perm) {
  if (perm & P.manage_write) return 'write'
  if (perm & P.manage_read) return 'read'
  return 'none'
}

/** Replace the bits of one group, keeping every other bit (including ones this page doesn't show). */
export function setLevel(perm, mask, bits) {
  return ((perm & ~mask) | bits) >>> 0
}

/** Short labels for a device's permissions, for the list. */
export function summary(perm) {
  const out = [{ key: 'devices.access_' + accessLevel(perm) }]
  const inputs = perm & P.inputs
  if (inputs === P.inputs) {
    out.push({ key: 'devices.sum_all_input' })
  } else if (inputs) {
    out.push({ key: 'devices.sum_some_input' })
  } else {
    out.push({ key: 'devices.sum_no_input' })
  }
  if (perm & (P.clipboard_set | P.clipboard_read)) {
    out.push({ key: 'devices.sum_clipboard' })
  }
  if (perm & P.server_cmd) {
    out.push({ key: 'devices.server_cmd', careful: true })
  }
  if (manageLevel(perm) !== 'none') {
    out.push({ key: 'devices.manage_' + manageLevel(perm), careful: true })
  }
  return out
}

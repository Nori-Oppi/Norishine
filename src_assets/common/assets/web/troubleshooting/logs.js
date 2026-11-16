// Parse the host log: "[2026-11-16 08:08:47.292]: Info: message", where a message can
// continue on the following lines.
const LINE = /^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]: (\w+): ?(.*)$/

export const LEVELS = ['Verbose', 'Debug', 'Info', 'Warning', 'Error', 'Fatal']

export function parseLog(text) {
  const entries = []
  for (const line of String(text || '').split(/\r?\n/)) {
    const m = LINE.exec(line)
    if (m) {
      entries.push({ time: m[1], level: m[2], message: m[3] })
    } else if (entries.length && line !== '') {
      entries[entries.length - 1].message += '\n' + line
    } else if (line !== '') {
      entries.push({ time: '', level: 'Info', message: line })
    }
  }
  return entries
}

export function levelRank(level) {
  const i = LEVELS.indexOf(level)
  return i < 0 ? 2 : i
}

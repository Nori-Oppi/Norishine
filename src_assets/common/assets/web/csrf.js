// Every request that changes something on the host must carry the session's CSRF token
// (set as the readable "csrf" cookie at sign-in) in the X-CSRF-Token header. Imported by
// every page, so all their fetch() calls get it without changes.

export function csrfToken() {
  const match = document.cookie.match(/(?:^|;\s*)csrf=([^;]*)/)
  return match ? decodeURIComponent(match[1]) : ''
}

if (!window.__norishineCsrf) {
  window.__norishineCsrf = true
  const original = window.fetch.bind(window)
  window.fetch = (input, init = {}) => {
    const isRequest = typeof Request !== 'undefined' && input instanceof Request
    const method = String(init.method || (isRequest ? input.method : 'GET')).toUpperCase()
    if (method !== 'GET' && method !== 'HEAD') {
      const url = new URL(isRequest ? input.url : String(input), location.href)
      if (url.origin === location.origin) {
        const headers = new Headers(init.headers || (isRequest ? input.headers : undefined))
        headers.set('X-CSRF-Token', csrfToken())
        init = { ...init, headers }
      }
    }
    return original(input, init)
  }
}

// Calls to the host's web UI API. Every call sends the session cookie; a 401 means the
// session ended (for example after a restart), so the page goes to the login page and
// comes back here afterwards.

export class ApiError extends Error {
  constructor(message, status, body) {
    super(message)
    this.status = status
    this.body = body
  }
}

function toLogin() {
  const here = '.' + location.pathname + location.hash
  location.href = './login?redir=' + encodeURIComponent(here)
}

export async function api(path, { method = 'GET', body, timeout = 15000 } = {}) {
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeout)
  const headers = {}
  if (body !== undefined) {
    headers['Content-Type'] = 'application/json'
  }
  let response
  try {
    response = await fetch(path, {
      method,
      credentials: 'include',
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: controller.signal,
    })
  } catch (e) {
    throw new ApiError(e.name === 'AbortError' ? 'The host did not answer in time.' : 'Could not reach the host.', 0)
  } finally {
    clearTimeout(timer)
  }

  if (response.status === 401) {
    toLogin()
    throw new ApiError('Signed out', 401)
  }
  let data = null
  try {
    data = await response.json()
  } catch {
    // Not JSON; handled below by the status.
  }
  if (!response.ok) {
    throw new ApiError((data && data.error) || `HTTP ${response.status}`, response.status, data)
  }
  return data
}

/**
 * Ask the host to restart, then wait until it answers again.
 * The restart request itself gets no answer: the host exits while handling it.
 * @returns {Promise<boolean>} Whether the host came back within the time limit.
 */
export async function restartHost({ timeoutMs = 90000 } = {}) {
  fetch('./api/restart', {
    method: 'POST',
    credentials: 'include',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).catch(() => {})

  const started = Date.now()
  let wentAway = false
  // Wait a moment so we don't talk to the old process while it shuts down.
  await new Promise((r) => setTimeout(r, 1500))
  while (Date.now() - started < timeoutMs) {
    try {
      const controller = new AbortController()
      const timer = setTimeout(() => controller.abort(), 2000)
      const r = await fetch('./api/configLocale', { cache: 'no-store', signal: controller.signal })
      clearTimeout(timer)
      if (r.ok && (wentAway || Date.now() - started > 6000)) {
        return true
      }
    } catch {
      wentAway = true
    }
    await new Promise((r) => setTimeout(r, 1000))
  }
  return false
}

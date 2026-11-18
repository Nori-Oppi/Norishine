"""End-to-end check of the client management API (/api/v1 on the HTTPS port) on a dev host.

Usage: python manage_check.py <host> <port> <webuser> <webpass>
Run it against a dev host only: it unpairs every client first, changes settings, apps and
devices (and puts them back). It doesn't start apps or reset display changes. Needs
`pip install cryptography`.
"""
import json
import ssl
import sys
import urllib.error
import urllib.request

from gsclient import Client, WebUI, pair_via_webui

MANAGE_READ = 0x10000000
MANAGE_WRITE = 0x20000000

host, port, user, password = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
results = []


def check(name, ok, detail=''):
    results.append((name, bool(ok)))
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', name, ('  -- ' + str(detail)[:300]) if not ok and detail is not None else ''), flush=True)


def api(client, method, path, body=None):
    """JSON request over HTTPS with the client's certificate."""
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.load_cert_chain(client.cert_file, client.key_file)
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request('https://%s:%d%s' % (host, client.https_port, path), data=data, method=method)
    if data is not None:
        req.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx)).open(req, timeout=15) as r:
            text = r.read().decode()
            try:
                return r.status, json.loads(text)
            except ValueError:
                return r.status, text
    except urllib.error.HTTPError as e:
        text = e.read().decode()
        try:
            return e.code, json.loads(text)
        except ValueError:
            return e.code, text


def set_perm(web, name, perm):
    _, clients = web.call('GET', '/api/clients/list')
    c = next(c for c in clients['named_certs'] if c['name'] == name)
    web.call('POST', '/api/clients/update', {
        'uuid': c['uuid'], 'name': c['name'], 'display_mode': c.get('display_mode', ''),
        'perm': perm, 'do': c.get('do', []), 'undo': c.get('undo', []),
        'enable_legacy_ordering': c['enable_legacy_ordering'], 'allow_client_commands': c['allow_client_commands'],
        'always_use_virtual_display': c['always_use_virtual_display']})
    return c['perm']


web = WebUI(host, port + 1, user, password)
web.call('POST', '/api/clients/unpair-all', {})
client = Client(host, port, 'e2e-manager')
ok, detail = pair_via_webui(client, web)
check('client pairs', ok, detail)

info = client.https('serverinfo')
check('serverinfo advertises the management API', info.findtext('Norishine/ApiVersion') == '1'
      and 'config.write' in (info.findtext('Norishine/Features') or ''), info.find('Norishine'))
perm = int(info.findtext('Permission') or 0)
check('pairing grants no management permission', perm & (MANAGE_READ | MANAGE_WRITE) == 0, hex(perm))
plain = client.http('serverinfo')
check('plain HTTP serverinfo has the element too', plain.findtext('Norishine/ApiVersion') == '1')

s, r = api(client, 'GET', '/api/v1/status')
check('status refused without permission (403)', s == 403 and r['status'] is False, (s, r))
s, r = api(client, 'GET', '/api/v1/config/schema')
check('schema refused without permission', s == 403, (s, r))

base_perm = set_perm(web, 'e2e-manager', perm | MANAGE_READ)
s, r = api(client, 'GET', '/api/v1/status')
check('status allowed with manage_read', s == 200 and r['api_version'] == 1 and r['device']['name'] == 'e2e-manager', (s, r))
s, r = api(client, 'GET', '/api/v1/config/schema')
check('schema allowed with manage_read', s == 200 and len(r['settings']) == 103, (s, str(r)[:200]))
s, r = api(client, 'GET', '/api/v1/config')
check('values allowed with manage_read', s == 200 and 'values' in r, (s, r))
s, r = api(client, 'GET', '/api/v1/stats')
host_keys = {'cpu_percent', 'process_cpu_percent', 'memory_total_mb', 'memory_used_mb', 'process_memory_mb', 'gpu_percent', 'encoder_percent'}
check('stats allowed with manage_read', s == 200 and host_keys <= set(r.get('host', {})) and isinstance(r.get('sessions'), list), (s, r))
check('stats report CPU and memory', isinstance(r.get('host', {}).get('cpu_percent'), (int, float)) and r['host'].get('memory_total_mb', 0) > 0, r)
check('serverinfo lists stats', 'stats' in (client.https('serverinfo').findtext('Norishine/Features') or '').split(','))
s, r = api(client, 'GET', '/api/v1/update')
check('update state allowed with manage_read', s == 200 and {'current', 'supported', 'available', 'state'} <= set(r), (s, r))
check('dev build says it cannot update itself', r.get('supported') is False and isinstance(r.get('unsupported_reason'), str), r)
s, r = api(client, 'POST', '/api/v1/update', {'version': 'nightly-20991231-abcdef0'})
check('starting an update refused with only manage_read', s == 403, (s, r))
s, r = api(client, 'PATCH', '/api/v1/config', {'set': {'max_bitrate': 40000}})
check('change refused with only manage_read', s == 403, (s, r))
s, r = api(client, 'GET', '/api/v1/recommendations')
check('recommendations allowed with manage_read', s == 200 and isinstance(r.get('recommendations'), list) and 'applied' in r, (s, r))
check('serverinfo lists recommendations', 'recommendations' in (client.https('serverinfo').findtext('Norishine/Features') or '').split(','))
for path in ('/api/v1/recommendations/apply', '/api/v1/recommendations/rollback'):
    s, r = api(client, 'POST', path, {})
    check('%s refused with only manage_read' % path.split('/')[-1], s == 403, (s, r))

set_perm(web, 'e2e-manager', perm | MANAGE_WRITE)
s, r = api(client, 'GET', '/api/v1/config')
check('manage_write includes reading', s == 200, (s, r))
s, r = api(client, 'POST', '/api/v1/update', {'version': 'nightly-20991231-abcdef0'})
check('dev build refuses to update itself', s == 409 and r.get('status') is False, (s, r))
s, r = api(client, 'PATCH', '/api/v1/config', {'set': {'max_bitrate': 'lots'}})
check('invalid change gives per-key errors', s == 400 and 'max_bitrate' in r.get('errors', {}), (s, r))
s, r = api(client, 'PATCH', '/api/v1/config', {'set': {'max_bitrate': 40000}})
check('change allowed with manage_write', s == 200 and r['changed'] == ['max_bitrate'] and r['restart_required'] is True, (s, r))
s, r = api(client, 'GET', '/api/v1/config')
check('change visible', r['values'].get('max_bitrate') == 40000, r)
s, r = api(client, 'PATCH', '/api/v1/config', {'reset': ['max_bitrate']})
check('reset allowed', s == 200, (s, r))
s, r = api(client, 'POST', '/api/v1/recommendations/apply', {'keys': 'nvenc_preset'})
check('recommendations: malformed apply refused', s == 400, (s, r))
s, r = api(client, 'POST', '/api/v1/recommendations/apply', {'keys': ['no_such_setting']})
check('recommendations: applying a key without one is 409', s == 409, (s, r))
s, r = api(client, 'GET', '/api/v1/recommendations')
if r.get('applied') is None:
    s, r = api(client, 'POST', '/api/v1/recommendations/rollback', {})
    check('recommendations: nothing to undo is 409', s == 409, (s, r))
s, r = api(client, 'GET', '/api/v1/config/schema?locale=en')
title = next((x.get('title') for x in r.get('settings', []) if x['key'] == 'max_bitrate'), None)
check('schema has readable titles', s == 200 and title == 'Bitrate limit' and len(r.get('categories', [])) >= 12, (s, title))

stranger = Client(host, port, 'e2e-stranger')
s, r = api(stranger, 'GET', '/api/v1/status')
# nvhttp's verify-failed path answers in GameStream XML with status_code 401.
check('unpaired certificate refused', isinstance(r, str) and 'status_code="401"' in r, (s, r))

# Apps, devices, sessions and displays (roadmap M7).
features = (client.https('serverinfo').findtext('Norishine/Features') or '').split(',')
check('serverinfo lists the M7 features', all(f in features for f in ['apps', 'apps.write', 'devices', 'devices.write', 'sessions.write', 'displays']), features)

set_perm(web, 'e2e-manager', perm | MANAGE_READ)
s, r = api(client, 'GET', '/api/v1/apps')
check('apps listed with manage_read', s == 200 and isinstance(r.get('apps'), list) and 'running_app' in r, (s, r))
original_order = [a['uuid'] for a in r.get('apps', [])]
s, r = api(client, 'GET', '/api/v1/devices')
me = [d for d in r.get('devices', []) if d.get('this_device')]
check('devices listed, the requesting one marked', s == 200 and len(me) == 1 and me[0]['name'] == 'e2e-manager', (s, r))
s, r = api(client, 'GET', '/api/v1/displays')
check('displays listed', s == 200 and isinstance(r.get('displays'), list) and 'output_name' in r, (s, r))
for method, path, body in [('POST', '/api/v1/apps', {'name': 'x'}), ('POST', '/api/v1/apps/close', None),
                           ('POST', '/api/v1/displays/reset', None), ('PATCH', '/api/v1/devices/%s' % me[0]['uuid'] if me else '/api/v1/devices/x', {'name': 'y'}),
                           ('DELETE', '/api/v1/apps/x', None)]:
    s, r = api(client, method, path, body)
    check('%s %s refused with only manage_read' % (method, path.split('/')[3]), s == 403, (s, r))

set_perm(web, 'e2e-manager', perm | MANAGE_WRITE)
s, r = api(client, 'POST', '/api/v1/apps', {'name': ' ', 'exit-timeout': 'soon', 'prep-cmd': [{'do': 5}]})
check('invalid app gives per-field errors', s == 400 and {'name', 'exit-timeout', 'prep-cmd'} <= set(r.get('errors', {})), (s, r))
s, r = api(client, 'POST', '/api/v1/apps', {'name': 'E2E app', 'cmd': '', 'detached': [], 'exit-timeout': 5, 'uuid': 'ignored'})
uid = r.get('uuid', '')
check('app added', s == 200 and len(uid) == 36 and uid != 'ignored' and r.get('closed_running_app') is False, (s, r))
s, r = api(client, 'GET', '/api/v1/apps')
added = next((a for a in r.get('apps', []) if a['uuid'] == uid), None)
check('added app listed, empty lists dropped', added is not None and added['name'] == 'E2E app' and 'detached' not in added, added)
s, r = api(client, 'PUT', '/api/v1/apps/' + uid, {'name': 'E2E app renamed', 'uuid': 'other'})
check('replacing with another ID refused', s == 400 and 'uuid' in r.get('errors', {}), (s, r))
s, r = api(client, 'PUT', '/api/v1/apps/' + uid, dict(added, name='E2E app renamed'))
check('app replaced (listed form round-trips)', s == 200, (s, r))
s, r = api(client, 'GET', '/api/v1/apps')
check('replaced app listed', any(a['uuid'] == uid and a['name'] == 'E2E app renamed' for a in r.get('apps', [])), r)
s, r = api(client, 'PUT', '/api/v1/apps/00000000-0000-0000-0000-000000000000', {'name': 'x'})
check('replacing an unknown app is 404', s == 404, (s, r))
s, r = api(client, 'POST', '/api/v1/apps/order', {'order': [uid]})
s2, r2 = api(client, 'GET', '/api/v1/apps')
check('apps reordered', s == 200 and r2['apps'][0]['uuid'] == uid and len(r2['apps']) == len(original_order) + 1, (s, r))
s, r = api(client, 'POST', '/api/v1/apps/order', {'order': 'first'})
check('invalid order refused', s == 400, (s, r))
s, r = api(client, 'DELETE', '/api/v1/apps/' + uid)
s2, r2 = api(client, 'GET', '/api/v1/apps')
check('app removed, original order back', s == 200 and [a['uuid'] for a in r2['apps']] == original_order, (s, r, r2))
s, r = api(client, 'DELETE', '/api/v1/apps/' + uid)
check('removing it again is 404', s == 404, (s, r))
s, r = api(client, 'POST', '/api/v1/apps/00000000-0000-0000-0000-000000000000/launch')
check('starting an unknown app is 404', s == 404, (s, r))

other = Client(host, port, 'e2e-other')
ok, detail = pair_via_webui(other, web)
check('second client pairs', ok, detail)
s, r = api(client, 'GET', '/api/v1/devices')
theirs = next((d for d in r.get('devices', []) if d['name'] == 'e2e-other'), None)
mine = next((d for d in r.get('devices', []) if d.get('this_device')), None)
check('second device listed, not marked', theirs is not None and not theirs['this_device'], r)
if theirs and mine:
    s, r = api(client, 'PATCH', '/api/v1/devices/' + theirs['uuid'], {'name': 'e2e-other-renamed', 'perm': theirs['perm'] | MANAGE_READ, 'do': [{'cmd': 'echo hi', 'elevated': False}]})
    check('other device changed', s == 200 and r['device']['name'] == 'e2e-other-renamed' and r['device']['perm'] & MANAGE_READ and r['device']['do'][0]['cmd'] == 'echo hi'
          and r['device']['display_mode'] == theirs['display_mode'], (s, r))
    s, r = api(other, 'GET', '/api/v1/status')
    check('granted permission works at once', s == 200, (s, r))
    s, r = api(client, 'PATCH', '/api/v1/devices/' + theirs['uuid'], {'bogus': 1, 'do': [{'cmd': 1}], 'perm': -1})
    check('invalid device change gives per-field errors', s == 400 and {'bogus', 'do', 'perm'} <= set(r.get('errors', {})), (s, r))
    s, r = api(client, 'PATCH', '/api/v1/devices/' + mine['uuid'], {'perm': 0})
    check('a device can\'t change its own permissions', s == 403, (s, r))
    s, r = api(client, 'PATCH', '/api/v1/devices/' + mine['uuid'], {'name': 'e2e-manager', 'perm': mine['perm']})
    check('...but can rename itself', s == 200, (s, r))
    s, r = api(client, 'POST', '/api/v1/devices/%s/disconnect' % theirs['uuid'])
    check('disconnecting a device without a stream', s == 200 and r.get('disconnected') is False, (s, r))
    s, r = api(client, 'DELETE', '/api/v1/devices/' + theirs['uuid'])
    s2, r2 = api(client, 'GET', '/api/v1/devices')
    check('other device unpaired', s == 200 and all(d['uuid'] != theirs['uuid'] for d in r2['devices']), (s, r))
    s, r = api(client, 'DELETE', '/api/v1/devices/' + theirs['uuid'])
    check('unpairing it again is 404', s == 404, (s, r))
s, r = api(client, 'POST', '/api/v1/apps/close')
check('close the running app (none running)', s == 200, (s, r))

# The web UI gets the same calls with its session.
s, r = web.call('GET', '/api/v1/apps')
check('web UI: apps', s == 200 and [a['uuid'] for a in r['apps']] == original_order, (s, r))
s, r = web.call('GET', '/api/v1/devices')
check('web UI: devices, none marked as itself', s == 200 and not any(d['this_device'] for d in r['devices']), (s, r))
s, r = web.call('POST', '/api/v1/apps', {'name': ''})
check('web UI: validation', s == 400 and 'name' in r.get('errors', {}), (s, r))

# Cover images for the Apps page: a PNG, or 404 when the app has no picture of its own.
try:
    with web.opener.open(web.base + '/api/v1/apps/' + original_order[0] + '/image', timeout=15) as resp:
        img_status, img = resp.status, resp.read()
except urllib.error.HTTPError as e:
    img_status, img = e.code, b''
check('web UI: app cover is a PNG (or 404 without one)', (img_status == 200 and img[:8] == b'\x89PNG\r\n\x1a\n') or img_status == 404, img_status)
s, r = web.call('GET', '/api/v1/apps/no-such-app/image')
check('web UI: cover of an unknown app is 404', s == 404, (s, r))

# The encoder in use, and sub-headings in the settings schema (both added later).
s, r = web.call('GET', '/api/v1/status')
check('status names the encoder', s == 200 and isinstance(r.get('encoder'), str), (s, str(r)[:200]))
s, r = web.call('GET', '/api/v1/config/schema?locale=en')
sections = {x['id']: x for x in r.get('sections', [])} if s == 200 else {}
by_key = {x['key']: x for x in r.get('settings', [])} if s == 200 else {}
check('schema has sections', 'display_monitor' in sections and sections['display_monitor'].get('depends_on', {}).get('key') == 'headless_mode'
      and by_key.get('output_name', {}).get('section') == 'display_monitor', (s, list(sections)))

web.call('POST', '/api/clients/unpair-all', {})
failed = [n for n, ok in results if not ok]
print('\n%d passed, %d failed' % (len(results) - len(failed), len(failed)))
sys.exit(1 if failed else 0)

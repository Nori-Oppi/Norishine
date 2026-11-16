"""End-to-end check of the client management API (/api/v1 on the HTTPS port) on a dev host.

Usage: python manage_check.py <host> <port> <webuser> <webpass>
Run it against a dev host only: it unpairs every client first and changes settings (and
puts them back). Needs `pip install cryptography`.
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
s, r = api(client, 'PATCH', '/api/v1/config', {'set': {'max_bitrate': 40000}})
check('change refused with only manage_read', s == 403, (s, r))

set_perm(web, 'e2e-manager', perm | MANAGE_WRITE)
s, r = api(client, 'GET', '/api/v1/config')
check('manage_write includes reading', s == 200, (s, r))
s, r = api(client, 'PATCH', '/api/v1/config', {'set': {'max_bitrate': 'lots'}})
check('invalid change gives per-key errors', s == 400 and 'max_bitrate' in r.get('errors', {}), (s, r))
s, r = api(client, 'PATCH', '/api/v1/config', {'set': {'max_bitrate': 40000}})
check('change allowed with manage_write', s == 200 and r['changed'] == ['max_bitrate'] and r['restart_required'] is True, (s, r))
s, r = api(client, 'GET', '/api/v1/config')
check('change visible', r['values'].get('max_bitrate') == 40000, r)
s, r = api(client, 'PATCH', '/api/v1/config', {'reset': ['max_bitrate']})
check('reset allowed', s == 200, (s, r))
s, r = api(client, 'GET', '/api/v1/config/schema?locale=en')
title = next((x.get('title') for x in r.get('settings', []) if x['key'] == 'max_bitrate'), None)
check('schema has readable titles', s == 200 and title == 'Maximum Bitrate' and len(r.get('categories', [])) == 12, (s, title))

stranger = Client(host, port, 'e2e-stranger')
s, r = api(stranger, 'GET', '/api/v1/status')
# nvhttp's verify-failed path answers in GameStream XML with status_code 401.
check('unpaired certificate refused', isinstance(r, str) and 'status_code="401"' in r, (s, r))

web.call('POST', '/api/clients/unpair-all', {})
failed = [n for n, ok in results if not ok]
print('\n%d passed, %d failed' % (len(results) - len(failed), len(failed)))
sys.exit(1 if failed else 0)

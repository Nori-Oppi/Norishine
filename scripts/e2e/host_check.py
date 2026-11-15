"""End-to-end check of pairing IDs, permissions and virtual displays on a dev host.

Usage: python host_check.py <host> <port> <webuser> <webpass> [--no-vd]
Run it against a dev host only: it unpairs every client first. Needs `pip install cryptography`.
The virtual display check (Windows) briefly adds a monitor; skip it with --no-vd.
"""
import subprocess
import sys
import threading
import time

from gsclient import Client, WebUI, pair_via_webui, wait_for_pending

host, port, user, password = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
check_vd = '--no-vd' not in sys.argv

results = []


def check(name, ok, detail=''):
    results.append((name, bool(ok), detail))
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', name, ('  -- ' + str(detail)) if detail else ''), flush=True)


def monitors():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          '(Get-CimInstance -Namespace root\\wmi -ClassName WmiMonitorBasicDisplayParams | Where-Object Active).Count'],
                         capture_output=True, text=True).stdout.strip()
    return int(out or 0)


web = WebUI(host, port + 1, user, password)
web.call('POST', '/api/clients/unpair-all', {})
status, clients = web.call('GET', '/api/clients/list')
check('fresh host has no paired clients', not clients.get('named_certs'), clients.get('named_certs'))

# 1. Pairing request IDs (GHSA-36ff-frg7-492f)
first = Client(host, port, 'e2e-first')
ok, detail = pair_via_webui(first, web)
check('first client pairs through /api/pin with a pairing ID', ok, detail)

victim = Client(host, port, 'e2e-victim')
attacker = Client(host, port, 'e2e-attacker')
vt, vres = victim.start_pair('1111')
at, ares = attacker.start_pair('2222')
pv = wait_for_pending(web, 'e2e-victim')
pa = wait_for_pending(web, 'e2e-attacker')
check('two concurrent requests are listed separately', pv and pa and pv['id'] != pa['id'], (pv, pa))
status, r = web.call('POST', '/api/pin', {'pin': '1111', 'name': ''})
check('PIN without pairing_id is rejected', status == 400, (status, r))
status, r = web.call('POST', '/api/pin', {'pairing_id': pv['id'], 'pin': '1111', 'name': ''})
vt.join(30)
check('PIN goes to the selected request only', r.get('status') is True and 'root' in vres and wait_for_pending(web, 'e2e-attacker', 1), r)
ok, detail = victim.finish_pair(vres['root'])
check('selected client completes pairing', ok, detail)
status, r = web.call('DELETE', '/api/pin', {'pairing_id': pa['id']})
at.join(30)
check('cancel rejects the other request', r.get('status') is True and 'root' in ares and ares['root'].findtext('paired') == '0'
      and 'cancelled' in (ares['root'].get('status_message') or ''), (r, ares))

# 2. Permissions
status, clients = web.call('GET', '/api/clients/list')
perms = {c['name']: c['perm'] for c in clients.get('named_certs', [])}
print('permissions:', {k: hex(v) for k, v in perms.items()})
FIRST_DEVICE = 0x07001F00  # list, view, launch and all input
DEFAULT = 0x03000000  # list and view
check('first device gets streaming and input only', perms.get('e2e-first') == FIRST_DEVICE, hex(perms.get('e2e-first', 0)))
check('later devices get list and view only', perms.get('e2e-victim') == DEFAULT, hex(perms.get('e2e-victim', 0)))

apps = first.https('applist')
app_ids = {a.findtext('AppTitle'): a.findtext('ID') for a in apps.findall('App')}
check('first client can list apps', apps.get('status_code') == '200' and app_ids, app_ids)
desktop = app_ids.get('Desktop') or next(iter(app_ids.values()))

r = victim.https('launch', appid=desktop, mode='1280x720x60', rikey='00' * 16, rikeyid=1, localAudioPlayMode=0, virtualDisplay=1)
check('second client cannot launch with default permissions', r.get('status_code') == '403', r.attrib)
r = victim.https('applist')
check('second client can list apps', r.get('status_code') == '200', r.attrib)

# Denied by permission: HTTP 401. Allowed but not streaming: HTTP 403.
r = first.https('actions/clipboard', type='text')
perm_clip = r.get('status_code') != '401'
print('first client clipboard read ->', r.attrib)
check('first device may not read the host clipboard', r.get('status_code') == '401', r.attrib)

# 3. Virtual display
if check_vd:
    before = monitors()
    r = first.https('launch', timeout=60, appid=desktop, mode='1280x720x60', rikey='00' * 16, rikeyid=1,
                    localAudioPlayMode=0, virtualDisplay=1)
    check('first client launches Desktop with a virtual display', r.get('status_code') == '200', r.attrib)
    time.sleep(3)
    during = monitors()
    check('a virtual display was added', during == before + 1, (before, during))
    info = first.https('serverinfo')
    check('serverinfo reports the virtual display driver ready', info.findtext('VirtualDisplayDriverReady') in ('1', 'true'),
          info.findtext('VirtualDisplayDriverReady'))
    r = first.https('cancel', timeout=60)
    check('cancel succeeds', r.get('status_code') == '200', r.attrib)
    time.sleep(3)
    after = monitors()
    check('the virtual display was removed', after == before, (before, after))

# 4. Granting a permission from the web UI takes effect.
status, clients = web.call('GET', '/api/clients/list')
fc = next(c for c in clients['named_certs'] if c['name'] == 'e2e-first')
status, r = web.call('POST', '/api/clients/update', {
    'uuid': fc['uuid'], 'name': fc['name'], 'display_mode': fc.get('display_mode', ''),
    'perm': fc['perm'] | 0x00020000, 'do': fc.get('do', []), 'undo': fc.get('undo', []),
    'enable_legacy_ordering': fc['enable_legacy_ordering'], 'allow_client_commands': fc['allow_client_commands'],
    'always_use_virtual_display': fc['always_use_virtual_display']})
r = first.https('actions/clipboard', type='text')
check('granted clipboard read takes effect (now only "not streaming")', r.get('status_code') == '403', r.attrib)

failed = [n for n, ok, _ in results if not ok]
print('\n%d passed, %d failed' % (len(results) - len(failed), len(failed)))
print('first client clipboard read allowed:', perm_clip)
sys.exit(1 if failed else 0)

"""End-to-end check of changing settings during a stream (roadmap N9), on the real PC.

Usage: python scripts/e2e/live_settings_check.py [--norishine EXE] [--norilight DIR]

Starts the Norishine build and a portable copy of the Norilight build the way the benchmark
check does (scripts/benchmark/compare.py: own config folder, port base 54989, through the
relay), streams the test picture, and while the stream runs:

1. changes host settings that need no restart (error correction, the encoder preset) through
   PATCH /api/v1/config, and checks that the stream takes them over without reconnecting;
2. changes the stream's bitrate, error correction and encoder preset as Norilight does
   (POST /api/v1/session/settings with Norilight's own certificate), and once through
   Norilight's stream settings menu (Ctrl+Alt+Shift+O, keys sent to the stream window).

Every check prints PASS or FAIL; the exit code is the number of failures. Leave the PC alone
while it runs: windows open and close by themselves.
"""
import argparse
import ctypes
import json
import os
import re
import ssl
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'benchmark'))

import bench_net  # noqa: E402
import compare  # noqa: E402

results = []


def check(name, ok, detail=''):
    results.append((name, bool(ok)))
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', name, ('  -- ' + str(detail)[:400]) if not ok and detail else ''), flush=True)


class HostLog:
    """Reads what the host wrote to its log since the last call."""

    def __init__(self, host):
        self.path = os.path.join(host.dir, 'host.log')
        self.pos = os.path.getsize(self.path) if os.path.exists(self.path) else 0

    def new(self):
        if not os.path.exists(self.path):
            return ''
        with open(self.path, encoding='utf-8', errors='replace') as f:
            f.seek(self.pos)
            text = f.read()
            self.pos = f.tell()
        return text


def wait_for(fn, timeout=10, step=0.25):
    end = time.time() + timeout
    while time.time() < end:
        value = fn()
        if value:
            return value
        time.sleep(step)
    return fn()


def session(host):
    st, body = host.web.call('GET', '/api/v1/stats')
    if st != 200 or not isinstance(body, dict) or not body.get('sessions'):
        return None
    return body['sessions'][0]


def frames_since(stream, t):
    return sum(1 for c in stream.watcher.changes if c >= t)


def check_still_streaming(stream, label):
    t = time.perf_counter()
    time.sleep(3)
    shown = frames_since(stream, t)
    check('%s: the stream keeps showing new frames (%d in 3 s)' % (label, shown), shown > 60 and stream.proc.poll() is None)
    handshakes = sum(1 for _, l in stream.lines if 'Starting RTSP handshake' in l)
    ended = any('Connection terminated' in l for _, l in stream.lines)
    check('%s: no reconnect' % label, handshakes == 1 and not ended, '%d handshakes, ended %s' % (handshakes, ended))


def host_settings(host, stream, log):
    """Step 1: settings that need no restart apply at once, during the stream."""
    st, body = host.web.call('PATCH', '/api/v1/config', {'set': {'fec_percentage': 30}})
    check('fec_percentage: saved without a restart', st == 200 and body.get('restart_required') is False, body)
    s = wait_for(lambda: (session(host) or {}).get('adaptive') and session(host)['adaptive']['fec_percent'] == 30 and session(host))
    check('fec_percentage: the stream uses 30 % at once', s, session(host))

    _, schema = host.web.call('GET', '/api/v1/config/schema')
    st_status, status = host.web.call('GET', '/api/v1/status')
    encoder = status.get('encoder') if isinstance(status, dict) else ''
    key = {'nvenc': 'nvenc_preset', 'amdvce': 'amd_quality', 'quicksync': 'qsv_preset', 'software': 'sw_preset'}.get(encoder)
    if key:
        setting = next(s for s in schema['settings'] if s['key'] == key)
        value = 2 if key == 'nvenc_preset' else ('speed' if key == 'amd_quality' else ('fast' if key == 'qsv_preset' else 'veryfast'))
        log.new()
        st, body = host.web.call('PATCH', '/api/v1/config', {'set': {key: value}})
        check('%s: saved without a restart' % key, st == 200 and body.get('restart_required') is False and not setting['restart_required'], body)
        text = wait_for(lambda: (lambda t: t if 'rebuilt with the host' in t else None)(log.new()), timeout=10) or ''
        check('%s: the stream rebuilt its encoder with it' % key, 'rebuilt with the host' in text, text[-400:])
        host.web.call('PATCH', '/api/v1/config', {'reset': [key]})
    else:
        check('encoder preset: an encoder with a preset (%s)' % encoder, False, status)
    check_still_streaming(stream, 'host settings')
    host.web.call('PATCH', '/api/v1/config', {'reset': ['fec_percentage']})


def client_cert(client):
    """Norilight's certificate and key from its portable settings, as PEM files."""
    text = open(client.ini, encoding='utf-8').read()
    out = []
    for name in ('certificate', 'key'):
        m = re.search(r'^%s="@ByteArray\((.*?)\)"$' % name, text, re.S | re.M)
        value = m.group(1).replace('\\\n', '').replace('\\n', '\n')
        value = '\n'.join(l.rstrip('\\') for l in value.split('\n'))
        f = tempfile.NamedTemporaryFile('w', suffix='.pem', delete=False)
        f.write(value)
        f.close()
        out.append(f.name)
    return out


def client_api(client, host, method, path, body=None):
    """A request on the host's HTTPS port with Norilight's own certificate."""
    if not hasattr(client, 'pem'):
        client.pem = client_cert(client)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.load_cert_chain(client.pem[0], client.pem[1])
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request('https://127.0.0.1:%d%s' % (host.port - 5, path), data=data, method=method)
    if data is not None:
        req.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx)).open(req, timeout=15) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        text = e.read().decode(errors='replace')
        try:
            return e.code, json.loads(text)
        except ValueError:
            return e.code, text


KEYEVENTF_EXTENDEDKEY = 1
KEYEVENTF_KEYUP = 2
EXTENDED = {'right', 'left', 'up', 'down'}  # arrow keys, not the keypad's
VK = {'ctrl': 0x11, 'alt': 0x12, 'shift': 0x10, 'o': 0x4F, 'right': 0x27, 'left': 0x25, 'up': 0x26, 'down': 0x28, 'enter': 0x0D, 'esc': 0x1B}


def press(*keys):
    """Presses the keys together, then lets them go in reverse order."""
    user32 = ctypes.windll.user32
    for k in keys:
        user32.keybd_event(VK[k], user32.MapVirtualKeyW(VK[k], 0), KEYEVENTF_EXTENDEDKEY if k in EXTENDED else 0, 0)
        time.sleep(0.03)
    for k in reversed(keys):
        user32.keybd_event(VK[k], user32.MapVirtualKeyW(VK[k], 0), KEYEVENTF_KEYUP | (KEYEVENTF_EXTENDEDKEY if k in EXTENDED else 0), 0)
        time.sleep(0.03)
    time.sleep(0.2)


def focus(stream):
    user32 = ctypes.windll.user32
    for _ in range(5):
        user32.keybd_event(VK['alt'], 0, 0, 0)  # lets SetForegroundWindow work from a background process
        user32.SetForegroundWindow(stream.hwnd)
        user32.keybd_event(VK['alt'], 0, KEYEVENTF_KEYUP, 0)
        time.sleep(0.3)
        if user32.GetForegroundWindow() == stream.hwnd:
            return True
    return False


def client_log(stream, needle, since=0):
    return [l for t, l in stream.lines if t >= since and needle in l]


def stream_settings(host, client, stream, log):
    """Step 2: bitrate, error correction and encoder preset change during the stream."""
    st, body = client_api(client, host, 'POST', '/api/v1/session/settings', {})
    check('session settings: an empty change is refused', st == 400, body)
    st, body = client_api(client, host, 'POST', '/api/v1/session/settings', {'fec_percent': 0})
    check('session settings: error correction 0 is refused', st == 400, body)

    log.new()
    st, body = client_api(client, host, 'POST', '/api/v1/session/settings', {'bitrate_kbps': 10000})
    adaptive = (body.get('stream') or {}).get('adaptive') or {} if isinstance(body, dict) else {}
    check('bitrate: 10 Mbps taken', st == 200 and 0 < adaptive.get('max_kbps', 0) <= 10000, body)
    text = wait_for(lambda: (lambda t: t if 'Video bitrate' in t else None)(log.new()), timeout=10) or ''
    check('bitrate: the encoder was rebuilt with it', 'Video bitrate' in text, text[-300:])

    st, body = client_api(client, host, 'POST', '/api/v1/session/settings', {'fec_percent': 35})
    s = wait_for(lambda: (session(host) or {}).get('adaptive', {}).get('fec_percent') == 35)
    check('error correction: 35 % at once', st == 200 and s, body)

    st, body = client_api(client, host, 'POST', '/api/v1/session/settings', {'tuning': 'quality'})
    text = wait_for(lambda: (lambda t: t if 'tuning' in t else None)(log.new()), timeout=10) or ''
    check('encoder preset: Quality taken and the encoder rebuilt', st == 200 and 'quality' in text, (body, text[-300:]))
    check_still_streaming(stream, 'session settings')

    # Through Norilight's own menu: bitrate one step up from what it started with
    log.new()
    t = time.perf_counter()
    # Keys reach the window only once Windows and SDL agree it has the focus: try a few times
    opened = False
    for _ in range(4):
        focus(stream)
        time.sleep(1)
        press('ctrl', 'alt', 'shift', 'o')
        if wait_for(lambda: client_log(stream, 'stream settings menu combo', t), timeout=2):
            opened = True
            break
    check('menu: Ctrl+Alt+Shift+O opens it', opened, stream.lines[-3:])

    def menu_key(name, code):
        for _ in range(4):
            since = time.perf_counter()
            focus(stream)
            press(name)
            if wait_for(lambda: client_log(stream, 'Stream settings menu: key %d' % code, since), timeout=2):
                return True
        return False

    check('menu: Right and Enter reach it', menu_key('right', 3) and menu_key('enter', 4))
    sent = wait_for(lambda: client_log(stream, 'Stream settings {', t), timeout=10)
    check('menu: Norilight sent the change', sent and '"bitrate_kbps":30000' in sent[0] and ': 200' in sent[0], sent or stream.lines[-5:])
    text = log.new()
    check('menu: the host took it', '30000 kbps asked' in text, text[-300:])
    check_still_streaming(stream, 'menu')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--norishine', default=os.path.join(compare.ROOT, 'build', 'sunshine.exe'))
    p.add_argument('--norilight', default=os.path.join(os.path.dirname(compare.ROOT), 'Norilight', 'build', 'Norilight-x64-release'))
    p.add_argument('--step', type=int, default=3, help='check N9 steps up to this one (default 3)')
    args = p.parse_args()

    setup = compare.load_setup()
    client = compare.prepare_norilight(args.norilight, 'performance')
    host = compare.Host('norishine', args.norishine, setup['norishine_port'], None)
    relay = bench_net.Relay(host.port)
    host.start()
    motion = None
    try:
        relay.start()
        client.ensure_paired(host)
        motion = compare.start_motion(600)
        time.sleep(1.5)
        stream = compare.Stream(client, host, relay)
        stream.start()
        try:
            check('the stream starts', stream.wait_first_frame() is not None)
            time.sleep(5)
            log = HostLog(host)
            host_settings(host, stream, log)
            if args.step >= 2:
                stream_settings(host, client, stream, log)
        finally:
            stream.stop()
    finally:
        if motion and motion.poll() is None:
            motion.kill()
        relay.stop()
        host.stop()

    failed = sum(1 for _, ok in results if not ok)
    print('\n%d checks, %d failed' % (len(results), failed))
    return failed


if __name__ == '__main__':
    sys.exit(main())

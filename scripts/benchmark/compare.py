"""Benchmark check: is Norishine with Norilight ever worse than Apollo, Vibepollo or Sunshine
with Moonlight? See docs/BENCHMARK.md ("Check against other hosts").

  python scripts/benchmark/compare.py                    # check: stream Norishine + Norilight,
                                                         # compare with the stored references
  python scripts/benchmark/compare.py --record apollo    # measure a reference again and store it

Every host runs from its own folder with its own config and ports (never installed, never as a
service; the installed Norishine is left alone) and every stream goes through bench_net.Relay on
127.0.0.2. Metrics come from the client side (its end-of-stream stats and the stream window's
pixels) and from Windows counters, so every host is measured the same way. Windows only; runs
on the real PC, never in CI.
"""
import argparse
import base64
import datetime
import http.cookiejar
import json
import os
import platform
import re
import shutil
import ssl
import statistics
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import bench_net  # noqa: E402
import bench_win  # noqa: E402

REFS_DIR = os.path.join(HERE, 'references')
WORK = os.path.join(ROOT, 'build', 'benchmark')
STATE = os.path.join(WORK, 'state')
USER, PASS = 'bench', 'bench-compare-1'
PIN = '4821'
STUTTER_MS = 42     # a stutter: a gap of 3 refreshes or more at 60 Hz (2.5 refreshes); 50 was exactly 3
STRIP = 160          # rows at the bottom of the screen where the client's stream window shows
GOALS = {'off': 0, 'performance': 1, 'balanced': 2, 'quality': 3}

# Stream settings: the same for every host and client
SCENARIO = {'app': 'Desktop', 'resolution': '1920x1080', 'fps': 60, 'bitrate_kbps': 20000, 'codec': 'HEVC',
            'window': 'windowed, V-sync and frame pacing off', 'clean_s': 60, 'warmup_s': 5,
            'loss': '5 % random video packet loss for 10 s, then 5 blackouts of 300 ms'}

# Config every host gets on top of its defaults: no display changes, no virtual display, no tray
COMMON_CONFIG = {
    'upnp': 'disabled', 'origin_web_ui_allowed': 'pc', 'dd_configuration_option': 'disabled',
    'headless_mode': 'disabled', 'virtual_display_mode': 'disabled', 'system_tray': 'disabled',
    'notify_pre_releases': 'disabled', 'min_log_level': 'info',
}

# Metric: (label, unit, better, tolerance absolute, tolerance relative). A difference within
# max(absolute, relative * reference) counts as the same.
METRICS = {
    'ttff_ms': ('Time to first frame', 'ms', 'lower', 150, 0.10),
    'host_latency_ms': ('Capture to send (host processing)', 'ms', 'lower', 0.5, 0.15),
    'decode_ms': ('Decode time', 'ms', 'lower', 0.3, 0.20),
    'dropped_pct': ('Dropped frames (network + jitter)', '%', 'lower', 0.5, 0),
    'displayed_fps': ('Frames shown per second', 'fps', 'higher', 2, 0.05),
    'stutters_per_min': ('Stutters (gaps of 3 refreshes or more) per minute', '/min', 'lower', 2, 0.25),
    'loss_fps': ('Frames shown per second at 5 % loss', 'fps', 'higher', 3, 0.08),
    'loss_dropped_pct': ('Dropped frames, loss stream', '%', 'lower', 1.0, 0.25),
    'recovery_ms': ('Recovery after a 300 ms blackout', 'ms', 'lower', 50, 0.25),
    'encoder_pct': ('Video encoder load (host process)', '%', 'lower', 5, 0.20),
    'host_cpu_pct': ('Host process CPU', '%', 'lower', 2, 0.25),
    'system_cpu_pct': ('Whole PC CPU', '%', 'lower', 10, 0.30),
}
# Not in the verdict. Frame interval p99: the test picture changes on every refresh, so it's
# one refresh or two, depending on whether just over or under 1 % of the refreshes repeat (N16)
INFO = ['interval_p99_ms', 'dropped_all_pct', 'first_packet_ms', 'received_fps', 'render_ms', 'decoder_pct', 'client_cpu_pct']

STATS_RE = {
    'received_fps': r'Incoming frame rate from network: ([\d.]+) FPS',
    'host_latency_ms': r'Host processing latency min/max/average: [\d.]+/[\d.]+/([\d.]+) ms',
    'net_drop_pct': r'Frames dropped by your network connection: ([\d.]+)%',
    'jitter_drop_pct': r'Frames dropped due to network jitter: ([\d.]+)%',
    'decode_ms': r'Average decoding time: ([\d.]+) ms',
    'render_ms': r'Average rendering time \(including monitor V-sync latency\): ([\d.]+) ms',
    # Norilight only: frames the pacer dropped because its queue was full when a new one came.
    # Moonlight 6.2.0 drops these without counting them (counted upstream since moonlight-qt
    # 423cbdcd), so dropped_frames() leaves them out to count every client the same way.
    'queue_drop_pct': r'Of those, dropped because the frame queue was full: ([\d.]+)%',
}


def dropped_frames(st):
    """Frames the client dropped (network + pacer), counted as Moonlight 6.2.0 counts them."""
    return round(st.get('net_drop_pct', 0) + st.get('jitter_drop_pct', 0) - st.get('queue_drop_pct', 0), 2)


def refs_path(*p):
    return os.path.join(WORK, 'refs', *p)


def load_setup():
    with open(os.path.join(REFS_DIR, 'setup.json'), encoding='utf-8') as f:
        return json.load(f)


# ---------------------------------------------------------------- web UI

def _ctx():
    c = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    c.check_hostname = False
    c.verify_mode = ssl.CERT_NONE
    return c


class WebUI:
    """Sign-in that works with every host: session cookie and CSRF token (Apollo, Vibepollo,
    Norishine) or HTTP Basic (Sunshine)."""

    def __init__(self, port):
        self.base = 'https://127.0.0.1:%d' % port
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=_ctx()),
                                                  urllib.request.HTTPCookieProcessor(self.jar))
        self.basic = 'Basic ' + base64.b64encode(('%s:%s' % (USER, PASS)).encode()).decode()
        self.csrf = ''
        self.call('POST', '/api/login', {'username': USER, 'password': PASS})
        self.csrf = next((c.value for c in self.jar if c.name.lower() in ('csrf', 'csrf_token', 'xsrf-token')), '')
        if not self.csrf:
            _, r = self.call('GET', '/api/csrf-token')
            if isinstance(r, dict):
                self.csrf = r.get('csrf_token') or ''

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method)
        req.add_header('Authorization', self.basic)
        if data is not None:
            req.add_header('Content-Type', 'application/json')
        if self.csrf and method != 'GET':
            req.add_header('X-CSRF-Token', self.csrf)
        try:
            with self.opener.open(req, timeout=15) as r:
                text, status = r.read().decode(errors='replace'), r.status
        except urllib.error.HTTPError as e:
            text, status = e.read().decode(errors='replace'), e.code
        except (urllib.error.URLError, OSError) as e:
            return 0, str(e)
        try:
            return status, json.loads(text)
        except ValueError:
            return status, text


# ---------------------------------------------------------------- hosts

class Host:
    def __init__(self, name, exe, port, version):
        self.name, self.exe, self.port, self.version = name, exe, port, version
        self.dir = os.path.join(STATE, name)
        self.proc = None
        self.web = None

    def conf(self):
        os.makedirs(self.dir, exist_ok=True)
        p = lambda f: os.path.join(self.dir, f).replace('\\', '/')
        conf = dict(sunshine_name='bench-' + self.name, port=self.port, file_apps=p('apps.json'),
                    file_state=p('state.json'), credentials_file=p('credentials.json'),
                    log_path=p('host.log'), pkey=p('key.pem'), cert=p('cert.pem'))
        conf.update(COMMON_CONFIG)
        with open(p('sunshine.conf'), 'w', encoding='utf-8') as f:
            for k, v in conf.items():
                f.write('%s = %s\n' % (k, v))
        if not os.path.exists(p('apps.json')):
            with open(p('apps.json'), 'w', encoding='utf-8') as f:
                json.dump({'env': {}, 'apps': [{'name': 'Desktop', 'image-path': 'desktop.png'}]}, f)
        return p('sunshine.conf')

    def start(self):
        if not os.path.exists(self.exe):
            sys.exit('%s not found at %s (see docs/BENCHMARK.md, "Reference hosts")' % (self.name, self.exe))
        conf = self.conf()
        marker = os.path.join(self.dir, '.creds')
        if not os.path.exists(marker):
            subprocess.run([self.exe, conf, '--creds', USER, PASS], cwd=os.path.dirname(self.exe),
                           capture_output=True, timeout=120)
            open(marker, 'w').close()
        log = os.path.join(self.dir, 'host.log')
        if os.path.exists(log):
            os.replace(log, log + '.prev')
        self.proc = subprocess.Popen([self.exe, conf], cwd=os.path.dirname(self.exe),
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                     creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
        end = time.time() + 60
        while time.time() < end:
            if self.proc.poll() is not None:
                sys.exit('%s exited at start; see %s' % (self.name, log))
            try:
                urllib.request.urlopen('http://127.0.0.1:%d/serverinfo' % self.port, timeout=2).read()
                break
            except urllib.error.HTTPError:
                break
            except (urllib.error.URLError, OSError):
                time.sleep(0.5)
        else:
            sys.exit('%s did not come up on port %d' % (self.name, self.port))
        time.sleep(1)
        self.web = WebUI(self.port + 1)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            subprocess.run(['taskkill', '/PID', str(self.proc.pid), '/T', '/F'], capture_output=True)
            try:
                self.proc.wait(15)
            except subprocess.TimeoutExpired:
                pass
        self.proc = None

    def end_stream(self):
        self.web.call('POST', '/api/apps/close', {})

    def approve_pin(self, name):
        """Enters the client's PIN on the host: Norishine and newer hosts list pending
        pairings, the others take the PIN alone."""
        st, r = self.web.call('GET', '/api/pin')
        pending = (r.get('pairings') if isinstance(r, dict) else None) or []
        body = {'pin': PIN, 'name': name}
        if pending:
            body['pairing_id'] = pending[0]['id']
        st, r = self.web.call('POST', '/api/pin', body)
        return isinstance(r, dict) and r.get('status') in (True, 'true')

    def allow_streaming(self, name):
        """Apollo-family hosts give a new device list and view rights only: add launch and input."""
        st, r = self.web.call('GET', '/api/clients/list')
        if not isinstance(r, dict) or not r.get('named_certs'):
            return
        for c in r['named_certs']:
            if c.get('name') == name and 'perm' in c:
                c = dict(c)
                c['perm'] = int(c['perm']) | 0x0700001F
                self.web.call('POST', '/api/clients/update', c)


# ---------------------------------------------------------------- clients

class Client:
    def __init__(self, name, exe, version):
        self.name, self.exe, self.version = name, exe, version
        self.dir = os.path.dirname(exe)
        org, app = ('Norilight', 'Norilight') if name == 'norilight' else ('Moonlight Game Streaming Project', 'Moonlight')
        self.ini = os.path.join(self.dir, org, app + '.ini')

    def run(self, args, timeout=90):
        try:
            p = subprocess.run([self.exe] + args, cwd=self.dir, capture_output=True, timeout=timeout)
            return p.returncode, p.stdout.decode(errors='replace') + p.stderr.decode(errors='replace')
        except subprocess.TimeoutExpired:
            return -1, 'timeout'

    def ensure_paired(self, host):
        addr = '%s:%d' % (bench_net.RELAY_ADDR, host.port)
        keep_on_relay(self.ini)
        code, out = self.run(['list', addr], timeout=60)
        if code == 0 and SCENARIO['app'] in out:
            return
        print('  pairing %s with %s...' % (self.name, host.name), flush=True)
        p = subprocess.Popen([self.exe, 'pair', addr, '--pin', PIN], cwd=self.dir,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        threading.Thread(target=p.stdout.read, daemon=True).start()
        name = 'bench-' + self.name
        ok, end = False, time.time() + 60
        while time.time() < end and p.poll() is None:
            time.sleep(1)
            if host.approve_pin(name):
                ok = True
                break
        try:
            p.wait(60)
        except subprocess.TimeoutExpired:
            p.kill()
        if not ok:
            sys.exit('Pairing %s with %s failed' % (self.name, host.name))
        host.allow_streaming(name)
        code, out = self.run(['list', addr], timeout=60)
        if code != 0:
            sys.exit('%s paired with %s but cannot list its apps:\n%s' % (self.name, host.name, out[-800:]))


def prepare_norilight(build_dir, goal):
    """A portable copy of a Norilight build in build/benchmark/clients/norilight. Its settings
    (pairings, the Tuning goal) stay between runs; the program files are refreshed each time."""
    dest = os.path.join(WORK, 'clients', 'norilight')
    if not os.path.exists(os.path.join(build_dir, 'Norilight.exe')):
        sys.exit('No Norilight.exe in %s (build Norilight, or pass --norilight)' % build_dir)
    shutil.copytree(build_dir, dest, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('Norilight', 'Norilight-*.log', 'portable.dat'))
    open(os.path.join(dest, 'portable.dat'), 'a').close()
    set_goal(os.path.join(dest, 'Norilight', 'Norilight.ini'), GOALS[goal])
    exe = os.path.join(dest, 'Norilight.exe')
    # Norilight's file version is Moonlight's; the commit says which build it is
    commit = subprocess.run(['git', 'describe', '--always', '--dirty'], cwd=build_dir, capture_output=True).stdout.decode().strip()
    return Client('norilight', exe, commit or file_version(exe))


def ini_general(ini, key, value):
    """Sets a top-level value in a client's portable settings file."""
    os.makedirs(os.path.dirname(ini), exist_ok=True)
    lines = open(ini, encoding='utf-8').read().splitlines() if os.path.exists(ini) else []
    if '[General]' not in lines:
        lines = ['[General]'] + lines
    lines = [l for l in lines if not l.startswith(key + '=')]
    lines.insert(lines.index('[General]') + 1, '%s=%s' % (key, value))
    with open(ini, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def set_goal(ini, value):
    """Sets the Tuning goal in Norilight's own settings (no profile: the top level)."""
    ini_general(ini, 'norilighttuninggoal', value)


def keep_on_relay(ini):
    """Keeps the client on the relay's address: no host discovery on the network, and no LAN,
    public or IPv6 addresses learned earlier, which would reach the host around the relay."""
    ini_general(ini, 'mdns', 'false')
    lines = open(ini, encoding='utf-8').read().splitlines()
    lines = [re.sub(r'^(\d+\\(?:localaddress|remoteaddress|ipv6address))=.*$', r'\1=', l) for l in lines]
    with open(ini, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def file_version(path):
    try:
        out = subprocess.run(['powershell', '-NoProfile', '-Command', '(Get-Item -LiteralPath "%s").VersionInfo.ProductVersion' % path],
                             capture_output=True, timeout=30).stdout.decode().strip()
        return out or None
    except (OSError, subprocess.TimeoutExpired):
        return None


# ---------------------------------------------------------------- one stream

class Stream:
    """Starts the client's stream through the relay, keeps its window where the watcher sees
    it, and collects its log with this script's own timestamps."""

    def __init__(self, client, host, relay):
        self.client, self.host, self.relay = client, host, relay
        self.lines = []
        self.watcher = bench_win.FrameWatcher()
        self.t0 = None
        self.proc = None
        self.hwnd = None

    def _reader(self):
        for raw in iter(self.proc.stdout.readline, b''):
            self.lines.append((time.perf_counter(), raw.decode(errors='replace').rstrip()))

    def start(self):
        sw, sh = bench_win.screen_size()
        args = ['stream', '%s:%d' % (bench_net.RELAY_ADDR, self.host.port), SCENARIO['app'],
                '--resolution', SCENARIO['resolution'], '--fps', str(SCENARIO['fps']),
                '--bitrate', str(SCENARIO['bitrate_kbps']), '--video-codec', SCENARIO['codec'],
                '--display-mode', 'windowed', '--no-vsync', '--no-frame-pacing', '--no-performance-overlay']
        keep_on_relay(self.client.ini)
        self.t0 = time.perf_counter()
        self.proc = subprocess.Popen([self.client.exe] + args, cwd=self.client.dir,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        threading.Thread(target=self._reader, daemon=True).start()
        end = time.time() + 60
        while time.time() < end and self.proc.poll() is None:
            self.hwnd = bench_win.stream_window(self.proc.pid)
            if self.hwnd:
                break
            time.sleep(0.005)
        if not self.hwnd:
            self.stop()
            raise RuntimeError('no stream window appeared:\n' + '\n'.join(l for _, l in self.lines[-25:]))
        self.place()
        cx, cy, cw, ch = bench_win.client_origin(self.hwnd)
        bottom = min(sh, bench_win.work_area_bottom())  # above the taskbar
        self.watcher.start((max(cx, 0), sh - STRIP + 8, min(cw, sw - max(cx, 0)), bottom - (sh - STRIP) - 16))
        threading.Thread(target=self._keep_placed, daemon=True).start()

    def place(self):
        """Moves the stream window so only the top of its picture shows, in the strip below
        the motion window: that part shows the motion window, so it changes every frame,
        and the strip itself is never what the strip shows (no picture-in-picture loop)."""
        sw, sh = bench_win.screen_size()
        left, top, _, _ = bench_win.window_rect(self.hwnd)
        cx, cy, _, _ = bench_win.client_origin(self.hwnd)
        bench_win.move_window(self.hwnd, left - cx, top + (sh - STRIP) - cy)

    def _keep_placed(self):
        """The client may replace its stream window (SDL recreates it when the renderer
        changes), so the window is looked up again each time."""
        sw, sh = bench_win.screen_size()
        while self.proc.poll() is None:
            time.sleep(0.1)
            try:
                hwnd = bench_win.stream_window(self.proc.pid)
                if hwnd and (hwnd != self.hwnd or bench_win.client_origin(hwnd)[1] != sh - STRIP):
                    self.hwnd = hwnd
                    self.place()
            except OSError:
                return

    def report_failure(self, what):
        """Saves what the screen and the client looked like, then raises."""
        folder = os.path.join(WORK, 'failures')
        os.makedirs(folder, exist_ok=True)
        stem = os.path.join(folder, '%s-%s' % (self.host.name, datetime.datetime.now().strftime('%Y%m%d-%H%M%S')))
        try:
            bench_win.screenshot(stem + '.bmp')
        except OSError:
            pass
        with open(stem + '.txt', 'w', encoding='utf-8') as f:
            f.write('%s\nwatched %r, samples %d\nwindows:\n' % (what, self.watcher.rect, self.watcher.samples))
            if self.proc:
                for win in bench_win.client_windows(self.proc.pid):
                    f.write('  %r\n' % (win,))
            f.write('\nclient log:\n' + '\n'.join(l for _, l in self.lines if 'nal_unit' not in l))
        raise RuntimeError('%s (details in %s.txt)' % (what, stem))

    def wait_first_frame(self, timeout=60):
        end = time.time() + timeout
        while time.time() < end and self.watcher.first_bright is None and self.proc.poll() is None:
            time.sleep(0.02)
        return self.watcher.first_bright

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.host.end_stream()
            try:
                self.proc.wait(20)
            except subprocess.TimeoutExpired:
                self.client.run(['quit', '%s:%d' % (bench_net.RELAY_ADDR, self.host.port)], timeout=30)
                try:
                    self.proc.wait(15)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
        self.watcher.stop()
        self.save_log()
        time.sleep(0.5)

    def save_log(self):
        """Keeps the client's log of each stream in build/benchmark/logs."""
        folder = os.path.join(WORK, 'logs')
        os.makedirs(folder, exist_ok=True)
        name = '%s-%s-%s.txt' % (self.host.name, self.client.name, datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
        with open(os.path.join(folder, name), 'w', encoding='utf-8') as f:
            f.write('\n'.join(l for _, l in self.lines if 'nal_unit' not in l))

    def stats_block(self):
        """The client's last end-of-stream statistics, without the decoder's debug lines
        that can come in between."""
        lines = [l for _, l in self.lines]
        starts = [i for i, l in enumerate(lines) if 'Global video stats' in l]
        if not starts:
            return []
        return [l for l in lines[starts[-1]:] if 'FFmpeg' not in l][:24]

    def stats(self):
        block = '\n'.join(self.stats_block())
        out = {}
        for k, rx in STATS_RE.items():
            m = re.search(rx, block)
            if m:
                out[k] = float(m.group(1))
        return out

    def first_packet_ms(self):
        for t, l in self.lines:
            if 'Received first video packet' in l:
                return round((t - self.t0) * 1000)
        return None


def intervals_between(changes, a, b):
    xs = [t for t in changes if a <= t <= b]
    return xs, [(y - x) * 1000 for x, y in zip(xs, xs[1:])]


def percentile(values, q):
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, int(round(q * (len(s) - 1))))]


def start_motion(seconds):
    sw, sh = bench_win.screen_size()
    return subprocess.Popen([sys.executable, os.path.join(HERE, 'motion.py'), str(seconds), str(sw), str(sh - STRIP), 'borderless'])


def clean_stream(client, host, relay):
    motion = start_motion(SCENARIO['clean_s'] + SCENARIO['warmup_s'] + 70)
    time.sleep(1.5)
    s = Stream(client, host, relay)
    m = {}
    try:
        s.start()
        first = s.wait_first_frame()
        if first is None:
            s.report_failure('no picture in the stream window')
        m['ttff_ms'] = round((first - s.t0) * 1000)
        time.sleep(SCENARIO['warmup_s'])
        host_cpu, client_cpu, system = bench_win.ProcessCpu(host.proc.pid), bench_win.ProcessCpu(s.proc.pid), bench_win.SystemCpu()
        gpu = bench_win.GpuEngines([host.proc.pid, s.proc.pid])
        for c in (host_cpu, client_cpu, system, gpu):
            c.start()
        a = time.perf_counter()
        time.sleep(SCENARIO['clean_s'])
        b = time.perf_counter()
        m['host_cpu_pct'], m['client_cpu_pct'], m['system_cpu_pct'] = host_cpu.stop(), client_cpu.stop(), system.stop()
        engines = gpu.stop()
        m['encoder_pct'], m['decoder_pct'] = engines.get(host.proc.pid), engines.get(s.proc.pid)
        shown, iv = intervals_between(s.watcher.changes, a, b)
        m['displayed_fps'] = round(len(shown) / (b - a), 2)
        m['interval_p99_ms'] = round(percentile(iv, 0.99), 2) if iv else None
        m['stutters_per_min'] = round(sum(1 for x in iv if x > STUTTER_MS) * 60 / (b - a), 2)
        m['first_packet_ms'] = s.first_packet_ms()
    finally:
        s.stop()
        if motion.poll() is None:
            motion.kill()
    st = s.stats()
    if not st:
        raise RuntimeError('the client printed no stream statistics')
    m['client_stats'] = s.stats_block()
    m['host_latency_ms'] = st.get('host_latency_ms')
    m['decode_ms'] = st.get('decode_ms')
    m['render_ms'] = st.get('render_ms')
    m['received_fps'] = st.get('received_fps')
    m['dropped_pct'] = dropped_frames(st)
    m['dropped_all_pct'] = round(st.get('net_drop_pct', 0) + st.get('jitter_drop_pct', 0), 2)
    return m


def loss_stream(client, host, relay):
    motion = start_motion(SCENARIO['warmup_s'] + 60)
    time.sleep(1.5)
    s = Stream(client, host, relay)
    m = {}
    try:
        s.start()
        if s.wait_first_frame() is None:
            s.report_failure('no picture in the stream window')
        time.sleep(SCENARIO['warmup_s'])
        relay.loss = 0.05
        a = time.perf_counter()
        time.sleep(10)
        b = time.perf_counter()
        relay.loss = 0.0
        shown, _ = intervals_between(s.watcher.changes, a, b)
        m['loss_fps'] = round(len(shown) / (b - a), 2)
        time.sleep(3)
        recoveries = []
        for _ in range(5):
            relay.blackout(0.3)
            time.sleep(0.3)
            end = time.perf_counter()
            time.sleep(3)
            after = [t for t in s.watcher.changes if t > end]
            recoveries.append(round((after[0] - end) * 1000) if after else 3000)
        m['recovery_ms'] = statistics.median(recoveries)
        m['recoveries_ms'] = recoveries
    finally:
        relay.loss = 0.0
        s.stop()
        if motion.poll() is None:
            motion.kill()
    st = s.stats()
    m['loss_dropped_pct'] = dropped_frames(st) if st else None
    return m


def warm_up(client, host, relay):
    motion = start_motion(80)
    time.sleep(1.5)
    s = Stream(client, host, relay)
    try:
        s.start()
        if s.wait_first_frame() is None:
            s.report_failure('no picture in the stream window')
        time.sleep(5)
    finally:
        s.stop()
        if motion.poll() is None:
            motion.kill()


def retry(fn, client, host, relay):
    """A stream that doesn't start (it happens to the first one after pairing) is tried once more."""
    try:
        return fn(client, host, relay)
    except RuntimeError as e:
        print('    stream failed (%s); trying again' % str(e).splitlines()[0], flush=True)
        time.sleep(5)
        return fn(client, host, relay)


def measure(host, client, runs):
    relay = bench_net.Relay(host.port)
    host.start()
    try:
        relay.start()
        client.ensure_paired(host)
        # The first stream after the host starts takes 1 to 2.5 s longer to show its first
        # frame (cold start, for every host and client), so it isn't measured
        print('  warm-up stream...', flush=True)
        retry(warm_up, client, host, relay)
        time.sleep(3)
        results = []
        for i in range(runs):
            print('  run %d/%d: clean stream (%d s)...' % (i + 1, runs, SCENARIO['clean_s'] + SCENARIO['warmup_s']), flush=True)
            m = retry(clean_stream, client, host, relay)
            time.sleep(3)
            print('  run %d/%d: loss stream...' % (i + 1, runs), flush=True)
            m.update(retry(loss_stream, client, host, relay))
            print('   ', ', '.join('%s %s' % (k, m.get(k)) for k in METRICS), flush=True)
            results.append(m)
            time.sleep(3)
        return results
    finally:
        relay.stop()
        host.stop()


def medians(runs):
    out = {}
    for k in list(METRICS) + INFO:
        vals = [r[k] for r in runs if r.get(k) is not None]
        out[k] = round(statistics.median(vals), 2) if vals else None
    return out


def machine():
    def ps(cmd):
        try:
            return subprocess.run(['powershell', '-NoProfile', '-Command', cmd], capture_output=True,
                                  timeout=30).stdout.decode(errors='replace').strip()
        except (OSError, subprocess.TimeoutExpired):
            return None
    sw, sh = bench_win.screen_size()
    return {
        'cpu': ps('(Get-CimInstance Win32_Processor | Select-Object -First 1).Name'),
        'gpu': ps('(Get-CimInstance Win32_VideoController | Where-Object { $_.CurrentHorizontalResolution } | '
                  'Where-Object Name -notmatch "Virtual|Meta" | Select-Object -First 1).Name'),
        'os': platform.platform(),
        'screen': '%dx%d' % (sw, sh),
    }


# ---------------------------------------------------------------- verdict

def verdict(key, ours, ref):
    label, unit, better, tol_abs, tol_rel = METRICS[key]
    if ours is None or ref is None:
        return 'n/a', None
    diff = ours - ref
    tol = max(tol_abs, tol_rel * abs(ref))
    if abs(diff) <= tol:
        return 'same', diff
    good = diff < 0 if better == 'lower' else diff > 0
    return ('better' if good else 'worse'), diff


def fmt(v):
    if v is None:
        return 'n/a'
    return ('%.0f' % v) if abs(v) >= 100 else ('%.2f' % v).rstrip('0').rstrip('.')


def report(ours, refs, goal):
    names = list(refs)
    print()
    print('Tuning goal (Norilight): %s' % goal.capitalize())
    head = ['Metric', 'Norishine'] + ['%s %s' % (n.capitalize(), refs[n]['host']['version']) for n in names]
    rows, worse = [], []
    for k, (label, unit, *_rest) in METRICS.items():
        row = ['%s (%s)' % (label, unit), fmt(ours.get(k))]
        for n in names:
            v, diff = verdict(k, ours.get(k), refs[n]['median'].get(k))
            ref = fmt(refs[n]['median'].get(k))
            row.append('%s %s' % (ref, v) if v in ('same', 'n/a') else '%s %s (%s%s)' % (ref, v, '+' if diff > 0 else '', fmt(diff)))
            if v == 'worse':
                worse.append('%s: %s' % (n.capitalize(), label))
        rows.append(row)
    widths = [max(len(r[i]) for r in rows + [head]) for i in range(len(head))]
    for r in [head, ['-' * x for x in widths]] + rows:
        print('  '.join(c.ljust(widths[i]) for i, c in enumerate(r)))
    print()
    if worse:
        print('Verdict: WORSE than a reference on %d metric(s):' % len(worse))
        for x in worse:
            print('  - ' + x)
    else:
        print('Verdict: not worse than any reference on any metric.')
    return worse


# ---------------------------------------------------------------- main

def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--record', metavar='NAME', help='measure this reference (from references/setup.json) and store it')
    p.add_argument('--runs', type=int, default=3, help='streams of each kind; the medians are compared (default 3)')
    p.add_argument('--goal', default='performance', choices=list(GOALS), help="Norilight's Tuning goal (default performance)")
    p.add_argument('--norishine', default=os.path.join(ROOT, 'build', 'sunshine.exe'), help='Norishine build to check')
    p.add_argument('--norilight', default=os.path.join(os.path.dirname(ROOT), 'Norilight', 'build', 'Norilight-x64-release'),
                   help='folder of the Norilight build to check')
    p.add_argument('--label', default='', help='stored with the result')
    return p.parse_args()


def main():
    if os.name != 'nt':
        sys.exit('Windows only.')
    args = parse_args()
    setup = load_setup()
    started = datetime.datetime.now().astimezone()

    if args.record:
        ref = setup['references'].get(args.record)
        if not ref:
            sys.exit('Unknown reference %r; known: %s' % (args.record, ', '.join(setup['references'])))
        mc = setup['client']
        client = Client('moonlight', refs_path(*mc['exe'].split('/')), mc['version'])
        host = Host(args.record, refs_path(*ref['exe'].split('/')), ref['port'], ref['version'])
        print('Recording %s %s with Moonlight %s, %d run(s)' % (args.record, ref['version'], mc['version'], args.runs), flush=True)
        runs = measure(host, client, args.runs)
        result = {
            'host': {'name': args.record, 'version': ref['version'], 'source': ref['url']},
            'client': {'name': 'Moonlight', 'version': mc['version'], 'source': mc['url']},
            'recorded': started.isoformat(timespec='seconds'),
            'machine': machine(), 'scenario': SCENARIO, 'host_config': COMMON_CONFIG,
            'median': medians(runs), 'runs': runs,
        }
        with open(os.path.join(REFS_DIR, args.record + '.json'), 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2)
            f.write('\n')
        print(json.dumps(result['median'], indent=2))
        print('Stored references/%s.json' % args.record)
        return 0

    refs = {}
    for name in setup['references']:
        path = os.path.join(REFS_DIR, name + '.json')
        if os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                refs[name] = json.load(f)
    if not refs:
        sys.exit('No stored references; record them first (--record NAME).')
    here = machine()
    for n, r in refs.items():
        if r['machine'].get('cpu') != here['cpu'] or r['machine'].get('gpu') != here['gpu']:
            print('Warning: %s was recorded on another PC (%s, %s); record it again here.' % (n, r['machine'].get('cpu'), r['machine'].get('gpu')))

    client = prepare_norilight(args.norilight, args.goal)
    host = Host('norishine', args.norishine, setup['norishine_port'], None)
    print('Checking Norishine (%s) with Norilight %s, Tuning goal %s, %d run(s)' %
          (args.norishine, client.version, args.goal, args.runs), flush=True)
    runs = measure(host, client, args.runs)
    ours = medians(runs)
    worse = report(ours, refs, args.goal)

    os.makedirs(WORK, exist_ok=True)
    out = os.path.join(WORK, 'compare-%s.json' % started.strftime('%Y%m%d-%H%M%S'))
    git = subprocess.run(['git', 'describe', '--always', '--dirty'], cwd=ROOT, capture_output=True).stdout.decode().strip()
    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'label': args.label, 'time': started.isoformat(timespec='seconds'), 'norishine_commit': git,
                   'norishine': args.norishine, 'norilight': client.version, 'goal': args.goal,
                   'machine': here, 'scenario': SCENARIO, 'median': ours, 'runs': runs, 'worse': worse,
                   'references': {n: {'version': r['host']['version'], 'median': r['median']} for n, r in refs.items()}},
                  f, indent=2)
    print('Wrote %s' % out)
    return 1 if worse else 0


if __name__ == '__main__':
    sys.exit(main())

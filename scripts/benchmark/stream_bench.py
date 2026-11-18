"""Stream benchmark: run a fixed stream against a dev host and record what it measured.

Starts a stream with a Norilight client (or samples one already running, --no-client),
samples GET /api/v1/stats through the web UI once a second, ends the stream and writes
a JSON summary and a CSV of the samples. See docs/BENCHMARK.md.

Usage (from the repository root, with the dev host running):
  python scripts/benchmark/stream_bench.py --client C:\\path\\to\\Norilight.exe

Needs `pip install cryptography` (for scripts/e2e/gsclient.py). The web UI password is
read from --password-file (default build/dev/webui-password.txt) or the environment
variable NORISHINE_WEB_PASSWORD; it is never printed.
"""
import argparse
import csv
import datetime
import json
import os
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'scripts', 'e2e'))

from gsclient import WebUI  # noqa: E402

# Settings that change what the benchmark measures; recorded with every run.
SETTING_KEYS = [
    'nvenc_preset', 'nvenc_twopass', 'amd_quality', 'amd_usage', 'amd_preanalysis', 'amd_rc', 'amd_vbaq',
    'qsv_preset', 'sw_preset', 'fec_percentage',
    'adaptive_bitrate', 'adaptive_min_bitrate', 'adaptive_max_fec', 'client_tuning', 'max_bitrate',
    'minimum_fps_target', 'capture', 'encoder',
]


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, default=49989, help='base port of the host (web UI is port + 1)')
    p.add_argument('--user', default='dev', help='web UI user')
    p.add_argument('--password-file', default=os.path.join(ROOT, 'build', 'dev', 'webui-password.txt'))
    p.add_argument('--client', help='Norilight executable that streams; omit with --no-client')
    p.add_argument('--client-dir', help='working folder for the client (portable Norilight: the folder with portable.dat)')
    p.add_argument('--no-client', action='store_true', help='sample a stream that is already running')
    p.add_argument('--device', help='name of the paired device whose stream to sample (default: the first)')
    p.add_argument('--app', default='Desktop')
    p.add_argument('--resolution', default='1920x1080')
    p.add_argument('--fps', type=int, default=60)
    p.add_argument('--bitrate', type=int, default=20000, help='kbps')
    p.add_argument('--codec', default='HEVC', choices=['auto', 'H.264', 'HEVC', 'AV1'])
    p.add_argument('--duration', type=int, default=60, help='seconds sampled')
    p.add_argument('--warmup', type=int, default=10, help='seconds after the stream starts before sampling')
    p.add_argument('--no-motion', action='store_true', help="don't open the moving test window on the host")
    p.add_argument('--label', default='', help='free text stored with the result, such as "baseline"')
    p.add_argument('--out', default=os.path.join(ROOT, 'build', 'benchmark'), help='folder for the results')
    return p.parse_args()


def password(args):
    if os.environ.get('NORISHINE_WEB_PASSWORD'):
        return os.environ['NORISHINE_WEB_PASSWORD']
    with open(args.password_file, encoding='utf-8') as f:
        return f.read().strip()


def get(web, path):
    status, body = web.call('GET', path)
    if status != 200 or not isinstance(body, dict):
        raise RuntimeError('GET %s answered %s' % (path, status))
    return body


def pick_session(stats, device):
    for s in stats.get('sessions') or []:
        if not device or s.get('name') == device:
            return s
    return None


def wait_for_session(web, device, timeout):
    end = time.time() + timeout
    while time.time() < end:
        s = pick_session(get(web, '/api/v1/stats'), device)
        if s and (s.get('window_s') or 0) > 0:
            return s
        time.sleep(1)
    return None


def range_stats(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    return {'min': round(min(values), 2), 'avg': round(statistics.fmean(values), 2), 'max': round(max(values), 2)}


def summarize(samples, first, last):
    def col(name):
        return [s[name] for s in samples]

    def ranged(prefix):
        mins = [s[prefix + '_min'] for s in samples if s[prefix + '_min'] is not None]
        avgs = [s[prefix + '_avg'] for s in samples if s[prefix + '_avg'] is not None]
        maxs = [s[prefix + '_max'] for s in samples if s[prefix + '_max'] is not None]
        if not avgs:
            return None
        return {'min': round(min(mins), 2), 'avg': round(statistics.fmean(avgs), 2), 'max': round(max(maxs), 2)}

    def recovery(s):
        return (s.get('key_frame_requests') or 0) + (s.get('ref_invalidations') or 0)

    adaptive = last.get('adaptive') or None
    return {
        'samples': len(samples),
        'fps': range_stats(col('fps')),
        'bitrate_kbps': range_stats(col('bitrate_kbps')),
        'latency_ms': ranged('latency'),
        'send_ms': ranged('send'),
        'recovery_requests': recovery(last) - recovery(first),
        'key_frames': (last.get('key_frames') or 0) - (first.get('key_frames') or 0),
        'host_cpu_percent': range_stats(col('cpu_percent')),
        'process_cpu_percent': range_stats(col('process_cpu_percent')),
        'gpu_percent': range_stats(col('gpu_percent')),
        'encoder_percent': range_stats(col('encoder_percent')),
        'host_limited_samples': sum(1 for s in samples if s['host_limited']),
        'adaptive_end': None if not adaptive else {
            k: adaptive.get(k) for k in ('bitrate_kbps', 'fec_percent', 'decreases', 'increases', 'last_reason')
        },
    }


def flatten(t, host, s):
    lat = s.get('latency_ms') or {}
    send = s.get('send_ms') or {}
    a = s.get('adaptive') or {}
    return {
        't': round(t, 1),
        'stream_s': s.get('duration_s'),
        'fps': s.get('fps'),
        'bitrate_kbps': s.get('bitrate_kbps'),
        'latency_min': lat.get('min'), 'latency_avg': lat.get('avg'), 'latency_max': lat.get('max'),
        'send_min': send.get('min'), 'send_avg': send.get('avg'), 'send_max': send.get('max'),
        'recent_recovery_requests': s.get('recent_recovery_requests'),
        'key_frame_requests': s.get('key_frame_requests'),
        'ref_invalidations': s.get('ref_invalidations'),
        'cpu_percent': host.get('cpu_percent'),
        'process_cpu_percent': host.get('process_cpu_percent'),
        'gpu_percent': host.get('gpu_percent'),
        'encoder_percent': host.get('encoder_percent'),
        'adaptive_bitrate_kbps': a.get('bitrate_kbps'),
        'fec_percent': a.get('fec_percent'),
        'adaptive_reason': a.get('last_reason'),
        'adaptive_change_s': a.get('last_change_s'),
        'host_limited': bool(a.get('host_limited')),
    }


def main():
    args = parse_args()
    if not args.no_client and not args.client:
        sys.exit('--client is required (or --no-client to sample a running stream)')

    web = WebUI(args.host, args.port + 1, args.user, password(args))
    status = get(web, '/api/v1/status')
    values = get(web, '/api/v1/config').get('values', {})
    settings = {k: values[k] for k in SETTING_KEYS if k in values}

    procs = []
    motion = None
    client = None
    try:
        if not args.no_client:
            if status.get('active_sessions'):
                sys.exit('The host already has a stream; end it first or use --no-client.')
            w, h = args.resolution.split('x')
            if not args.no_motion:
                motion = subprocess.Popen([sys.executable, os.path.join(HERE, 'motion.py'),
                                           str(args.warmup + args.duration + 40), w, h])
                procs.append(motion)
            cmd = [args.client, 'stream', '%s:%d' % (args.host, args.port), args.app,
                   '--resolution', args.resolution, '--fps', str(args.fps), '--bitrate', str(args.bitrate),
                   '--video-codec', args.codec, '--display-mode', 'windowed', '--no-vsync', '--no-frame-pacing',
                   '--no-performance-overlay']
            client = subprocess.Popen(cmd, cwd=args.client_dir or os.path.dirname(os.path.abspath(args.client)))
            procs.append(client)

        print('Waiting for the stream...', flush=True)
        session = wait_for_session(web, args.device, 45)
        if not session:
            sys.exit('No stream appeared on the host.')
        device_uuid, device_name = session['uuid'], session['name']
        print('Stream from %s; warming up %d s' % (device_name, args.warmup), flush=True)
        time.sleep(args.warmup)

        samples, first, last = [], None, None
        start = time.time()
        while time.time() - start < args.duration:
            tick = time.time()
            stats = get(web, '/api/v1/stats')
            s = pick_session(stats, device_name)
            if not s:
                print('The stream ended early.', flush=True)
                break
            first = first or s
            last = s
            samples.append(flatten(tick - start, stats.get('host') or {}, s))
            time.sleep(max(0.0, 1.0 - (time.time() - tick)))

        if not args.no_client:
            web.call('POST', '/api/v1/devices/%s/disconnect' % device_uuid, {})
    finally:
        if client:
            try:
                client.wait(30)
            except subprocess.TimeoutExpired:
                client.kill()
        if motion and motion.poll() is None:
            motion.kill()

    if not samples:
        sys.exit('No samples.')

    result = {
        'label': args.label,
        'time': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
        'host': {
            'version': status.get('version'),
            'platform': status.get('platform'),
            'encoder': status.get('encoder'),
            'settings': settings,
        },
        'scenario': {
            'client': 'none (sampled a running stream)' if args.no_client else os.path.basename(args.client),
            'device': device_name,
            'app': args.app,
            'resolution': args.resolution,
            'fps': args.fps,
            'bitrate_kbps': args.bitrate,
            'codec': args.codec,
            'motion': not args.no_motion and not args.no_client,
            'warmup_s': args.warmup,
            'duration_s': args.duration,
        },
        'summary': summarize(samples, first, last),
        'samples': samples,
    }

    os.makedirs(args.out, exist_ok=True)
    stem = os.path.join(args.out, 'bench-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    with open(stem + '.json', 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=2)
    with open(stem + '.csv', 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(samples[0].keys()))
        writer.writeheader()
        writer.writerows(samples)

    print(json.dumps(result['summary'], indent=2))
    print('Wrote %s.json and .csv' % stem)


if __name__ == '__main__':
    main()

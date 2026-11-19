"""End-to-end check of changing settings during a stream (roadmap N9), on the real PC.

Usage: python scripts/e2e/live_settings_check.py [--norishine EXE] [--norilight DIR]

Starts the Norishine build and a portable copy of the Norilight build the way the benchmark
check does (scripts/benchmark/compare.py: own config folder, port base 54989, through the
relay), streams the test picture, and while the stream runs:

1. changes host settings that need no restart (error correction, the encoder preset) through
   PATCH /api/v1/config, and checks that the stream takes them over without reconnecting.

Every check prints PASS or FAIL; the exit code is the number of failures. Leave the PC alone
while it runs: windows open and close by themselves.
"""
import argparse
import os
import re
import sys
import time

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


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--norishine', default=os.path.join(compare.ROOT, 'build', 'sunshine.exe'))
    p.add_argument('--norilight', default=os.path.join(os.path.dirname(compare.ROOT), 'Norilight', 'build', 'Norilight-x64-release'))
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

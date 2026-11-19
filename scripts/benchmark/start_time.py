"""Stream start times, step by step (roadmap N5). See docs/BENCHMARK.md ("Stream start").

  python scripts/benchmark/start_time.py                  # 5 launches and 5 resumes, after
                                                          # one first launch on a fresh host
  python scripts/benchmark/start_time.py --runs 3 --label after

Starts the Norishine build and a portable Norilight copy the same way compare.py does (own
config folders and ports, through the relay on 127.0.0.2), then streams the Desktop app:

- launch: no app runs on the host; the stream starts it. Ended from the host (app closed).
- resume: the app is still running from the stream before; only the stream is ended.

Each step's time comes from the timelines both programs log ("Stream start ..."): the client's
from choosing the app to the first frame on screen, the host's from the launch or resume
request to the first frame sent. The test picture is watched as in compare.py, so the result
also has the time from starting the client process to the first picture on screen.
Windows only; runs on the real PC, never in CI. Leave the PC alone while it runs.
"""
import argparse
import datetime
import json
import os
import re
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bench_net  # noqa: E402
import compare  # noqa: E402

LINE_RE = re.compile(r' ([^,()]+?) (\d+(?:\.\d+)?) \(\+(\d+(?:\.\d+)?)\)')


def parse_timeline(line):
    """'Stream start (launch): a 1 (+1), b 5 (+4), first frame sent 9 (+4) ms' -> [(step, at, took)]."""
    i = line.find('Stream start')
    body = line[i:].split(':', 1)[1] if i >= 0 else line
    return [(m.group(1).strip(), float(m.group(2)), float(m.group(3))) for m in LINE_RE.finditer(body)]


def host_timeline(host, after_size):
    """The host's last stream start timeline written after the log had after_size bytes."""
    path = os.path.join(host.dir, 'host.log')
    end = time.time() + 5
    while time.time() < end:
        with open(path, encoding='utf-8', errors='replace') as f:
            f.seek(after_size)
            lines = [l for l in f if 'Stream start (' in l]
        if lines:
            kind = re.search(r'Stream start \((\w+)\)', lines[-1]).group(1)
            return kind, parse_timeline(lines[-1])
        time.sleep(0.2)
    return None, []


def end_stream(host, keep_app):
    """Ends the stream. keep_app: only disconnect (the next stream resumes the app)."""
    if not keep_app:
        host.end_stream()
        return
    st, r = host.web.call('GET', '/api/v1/stats')
    for s in (r.get('sessions') if isinstance(r, dict) else None) or []:
        host.web.call('POST', '/api/v1/devices/%s/disconnect' % s['uuid'], {})


def one_stream(client, host, relay, kind):
    """kind: 'launch' or 'resume'. Returns the measurements of one stream start."""
    log = os.path.join(host.dir, 'host.log')
    size = os.path.getsize(log) if os.path.exists(log) else 0
    motion = compare.start_motion(40)
    time.sleep(1.5)
    s = compare.Stream(client, host, relay)
    m = {'kind': kind}
    try:
        s.start()
        first = s.wait_first_frame()
        if first is None:
            s.report_failure('no picture in the stream window')
        m['screen_ms'] = round((first - s.t0) * 1000)
        time.sleep(2)
    finally:
        if s.proc and s.proc.poll() is None:
            end_stream(host, keep_app=True)
            try:
                s.proc.wait(20)
            except Exception:  # noqa: BLE001
                s.proc.kill()
        s.watcher.stop()
        if motion.poll() is None:
            motion.kill()
    client_lines = [(t, l) for t, l in s.lines if 'Stream start:' in l]
    if client_lines:
        t, line = client_lines[-1]
        m['client'] = parse_timeline(line)
        m['client_ms'] = m['client'][-1][1] if m['client'] else None
        # Client process start to the timeline's start: Qt start-up and finding the host
        m['before_ms'] = round((t - s.t0) * 1000 - m['client_ms']) if m['client_ms'] is not None else None
    # The client's log, each line with its time since the client started, for a closer look
    m['client_log'] = ['%6d %s' % ((t - s.t0) * 1000, l) for t, l in s.lines if 'nal_unit' not in l]
    host_kind, m['host'] = host_timeline(host, size)
    m['host_kind'] = host_kind
    m['host_ms'] = m['host'][-1][1] if m['host'] else None
    return m


def step_table(runs, side):
    """Median time of each step over the runs, in the order of the first run."""
    order, values = [], {}
    for r in runs:
        for step, at, took in r.get(side) or []:
            if step not in values:
                order.append(step)
                values[step] = ([], [])
            values[step][0].append(at)
            values[step][1].append(took)
    return [(step, statistics.median(values[step][0]), statistics.median(values[step][1]), len(values[step][0]))
            for step in order]


def report(results):
    for kind in ('first', 'launch', 'resume'):
        runs = [r for r in results if r['kind'] == kind]
        if not runs:
            continue
        print()
        title = 'First launch after the host started' if kind == 'first' else kind.capitalize()
        print('%s (%d streams)' % (title, len(runs)))
        for side, title in (('client', 'Norilight: choosing the app to first frame shown'),
                            ('host', 'Norishine: request to first frame sent')):
            print('  %s' % title)
            for step, at, took, n in step_table(runs, side):
                print('    %-28s at %6.0f ms  +%5.0f ms%s' % (step, at, took, '' if n == len(runs) else '  (%d of %d)' % (n, len(runs))))
        for k, label in (('client_ms', 'Choosing the app to first frame shown'),
                         ('host_ms', 'Host: request to first frame sent'),
                         ('screen_ms', 'Client process start to first picture seen'),
                         ('before_ms', 'Client process start to choosing the app')):
            vals = [r[k] for r in runs if r.get(k) is not None]
            if vals:
                print('  %-44s median %5.0f ms (%s)' % (label, statistics.median(vals), ', '.join('%.0f' % v for v in vals)))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--runs', type=int, default=5, help='streams of each kind (default 5)')
    p.add_argument('--goal', default='performance', choices=list(compare.GOALS), help="Norilight's Tuning goal (default performance)")
    p.add_argument('--norishine', default=os.path.join(compare.ROOT, 'build', 'sunshine.exe'), help='Norishine build')
    p.add_argument('--norilight', default=os.path.join(os.path.dirname(compare.ROOT), 'Norilight', 'build', 'Norilight-x64-release'),
                   help='Norilight build folder')
    p.add_argument('--label', default='', help='stored with the result')
    args = p.parse_args()

    host = compare.Host('norishine', args.norishine, 54989, compare.file_version(args.norishine))
    client = compare.prepare_norilight(args.norilight, args.goal)
    relay = bench_net.Relay(host.port)
    results = []
    host.start()
    try:
        relay.start()
        client.ensure_paired(host)
        # The first stream after the host starts is reported on its own: the host may still
        # have start-up work left (an encoder probe), and the client its first-run caches
        print('first stream after the host started...', flush=True)
        m = compare.retry(lambda c, h, r: one_stream(c, h, r, 'launch'), client, host, relay)
        m['kind'] = 'first'
        print('  client %s ms, host %s ms, screen %s ms' % (m.get('client_ms'), m.get('host_ms'), m.get('screen_ms')), flush=True)
        results.append(m)
        host.end_stream()
        time.sleep(3)
        for i in range(args.runs):
            for kind in ('launch', 'resume'):
                print('run %d/%d: %s...' % (i + 1, args.runs, kind), flush=True)
                m = compare.retry(lambda c, h, r: one_stream(c, h, r, kind), client, host, relay)
                print('  client %s ms, host %s ms (%s), screen %s ms' % (m.get('client_ms'), m.get('host_ms'), m.get('host_kind'), m.get('screen_ms')), flush=True)
                results.append(m)
                time.sleep(2)
            host.end_stream()  # the next launch starts with no app running
            time.sleep(3)
    finally:
        relay.stop()
        host.stop()

    report(results)
    out = os.path.join(compare.WORK, 'start-%s.json' % datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'label': args.label, 'goal': args.goal, 'host': host.version, 'client': client.version,
                   'machine': compare.machine(), 'runs': results}, f, indent=1)
    print('\nWritten to %s' % out)


if __name__ == '__main__':
    main()

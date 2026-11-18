# Stream benchmark

Two tools in `scripts/benchmark/`:

- **The check** (`compare.py`, roadmap N2): is Norishine with Norilight ever worse than
  Apollo, Vibepollo or Sunshine with Moonlight? Run it after every change that touches
  streaming. [Below](#check-against-apollo-vibepollo-and-sunshine).
- **The host benchmark** (`stream_bench.py`, roadmap M8): Norishine's own numbers from
  `/api/v1/stats`, for before-and-after measurements of host changes.
  [Further down](#host-benchmark-stream_benchpy).

## Check against Apollo, Vibepollo and Sunshine

**Rule:** run the check after any change to capture, encoding, the network code or the
client's decoding (in Norishine or Norilight), and put its verdict in the pull request.
A metric that comes out worse than a reference is fixed before merging, or the pull request
explains why it's acceptable. The check needs the real PC (GPU, display), so it never runs
in GitHub CI.

### Running it

On the PC, with a release build of Norishine (`build/sunshine.exe`) and of Norilight
(`..\Norilight\build\Norilight-x64-release`, built with `scripts\build-norilight.bat release`):

```sh
python scripts/benchmark/compare.py
```

It needs Python 3.11 or later on Windows, and no other packages. Leave the PC alone while it
runs: windows open and close by themselves. With the default three runs it takes about
ten minutes. A stream that doesn't start is tried once more; if it fails again, the check
stops and leaves a screenshot and the client's log in `build/benchmark/failures/`. `--runs 1` is quicker, but is only good for trying the script out.

What it does:

1. Starts the Norishine build as a user process with its own config folder
   (`build/benchmark/state/norishine`) and port base 54989. It is never installed, and the
   installed Norishine keeps running untouched (decision D7).
2. Copies the Norilight build to `build/benchmark/clients/norilight` as a portable copy
   (`portable.dat`). Its settings stay there between runs, so it pairs only once. It sets
   that copy's **Tuning goal** (`--goal`, default `performance`). The goal changes the host's
   encoder preset, so the result names it, and only results with the same goal can be
   compared.
3. Streams twice, three times over (`--runs`), always the same way:
   - **Clean stream**: the `Desktop` app at 1920×1080, 60 fps, 20 Mbps, HEVC, windowed,
     V-sync and frame pacing off. It waits 5 seconds after the first frame, then measures
     for 60 seconds.
   - **Loss stream**: 10 seconds with 5 % of the video packets dropped at random, then five
     blackouts of 300 ms in which every video packet is dropped.
4. Prints a table: per metric, Norishine's median over the runs, and for each reference its
   stored median and **better**, **same** or **worse**. Then the verdict, and an exit code
   of 0 (not worse anywhere) or 1. The whole result goes to
   `build/benchmark/compare-<time>.json`.

`--norishine` and `--norilight` point at other builds, and `--label` is stored with the
result.

### The test stream

`motion.py` draws seeded moving blocks over the whole screen except a strip at the bottom,
so every host encodes the same picture. The client's stream window is moved so that only
the top of its picture shows in that strip. That part shows the moving blocks, so it
changes with every frame. The strip itself is never part of what it shows, so there is no
picture-in-picture loop.

Every stream goes through a relay (`bench_net.py`) on 127.0.0.2 and 127.0.0.3, even when
nothing is dropped, so all hosts get the same path. The relay is also what drops packets.
The clients' network discovery (mDNS) is turned off in their copies, and addresses they
learned earlier are cleared. Without that, they would reach the host around the relay.

### Metrics

Every metric is measured outside the host or from the client's side, because Apollo,
Vibepollo and Sunshine don't have `/api/v1/stats`. So every host is measured the same way,
and none is n/a.

| Metric | How it's measured | Better | Same within |
| --- | --- | --- | --- |
| Time to first frame | From starting the client to the first frame of the test picture in its window. Most of it is the client starting up (about 5 s), and that part is the same for every host | lower | 150 ms or 10 % |
| Capture to send (host processing) | The host processing latency each host writes into every frame, averaged by the client (its end-of-stream statistics) | lower | 0.5 ms or 15 % |
| Decode time | The client's average decoding time | lower | 0.3 ms or 20 % |
| Dropped frames | Frames the client dropped for network loss or jitter, in the clean stream | lower | 0.5 points |
| Frames shown per second | New pictures seen in the stream window per second. Watched every 4 ms; the screen refreshes at 60 Hz | higher | 2 fps or 5 % |
| Frame interval, 99th percentile | Time between new pictures in the window | lower | 5 ms or 15 % |
| Stutters per minute | Gaps of more than 50 ms between new pictures | lower | 2 or 25 % |
| Frames shown at 5 % loss | Frames shown per second during the random loss | higher | 3 fps or 8 % |
| Dropped frames, loss stream | The client's dropped frames over the whole loss stream. Mostly the 1.5 s of blackouts, so about 5 % for every host; a host whose error correction recovers less of the random loss comes out higher | lower | 1 point or 25 % |
| Recovery after a 300 ms blackout | From the end of a blackout to the next new picture (median of five) | lower | 50 ms or 25 % |
| Video encoder load | The host process's busiest video engine (Windows "GPU Engine" counters; on AMD the "Video Codec" engine) | lower | 5 points or 20 % |
| Host process CPU | The host process's CPU time over the clean minute, in % of the whole PC | lower | 2 points or 25 % |
| Whole PC CPU | All processes, including the client and the test picture. Other programs move it a lot | lower | 10 points or 30 % |

"Same within 150 ms or 10 %" means the larger of the two, the percentage taken of the
reference. The tolerances are about the spread between runs, so noise doesn't count as a
difference. The result also stores the time to the first video packet, the frame rate
received, render time, the client's decoder load and its CPU, for information.

Not measured: picture quality (it would need decoded frames compared with the source) and
latency on the client's screen (no camera or capture card). Capture to send is the latency
every host reports in the same way.

### References

Stored in `scripts/benchmark/references/`, one file per host (`apollo.json`,
`vibepollo.json`, `sunshine.json`). Each holds the host and client version, the PC, the
scenario, every run and the medians. `references/setup.json` says which version of each host
and of Moonlight is used, where it comes from, and its port base.

| Reference | Version | Client |
| --- | --- | --- |
| Apollo | 0.4.6 (latest release) | Moonlight 6.2.0 |
| Vibepollo | 2.17.6 (latest nightly; there are no stable releases) | Moonlight 6.2.0 |
| Sunshine | 2026.914.233613 (latest release) | Moonlight 6.2.0 |

Measure a reference again only when its host or Moonlight releases a new version, or on a
different PC. The check warns when a reference was recorded on another CPU or GPU.

```sh
python scripts/benchmark/compare.py --record apollo     # or vibepollo, sunshine
```

`--record` streams with stock Moonlight and that host, the same way as the check, and
overwrites the reference's file. Commit the file, along with `setup.json` if a version
changed.

The hosts and Moonlight are unpacked, never installed, into `build/benchmark/refs/`
(ignored by git). Each runs as a user process from there, with its own config folder in
`build/benchmark/state/<name>` and its own port base (Apollo 51989, Vibepollo 52989,
Sunshine 53989). Every host gets the same overrides on top of its defaults: no display
changes, no virtual display, no tray icon, UPnP off (`COMMON_CONFIG` in `compare.py`). To
set up a new version, download the files named in `setup.json` and unpack them:

| File | Unpack with | Into |
| --- | --- | --- |
| `Apollo-<version>.exe` (NSIS installer) | `7z x Apollo-<version>.exe -o<folder>` | `hosts/apollo-<version>/` |
| `VibepolloSetup.exe` | It wraps an MSI: cut out the part from the first `D0 CF 11 E0 A1 B1 1A E1` to the end as `Vibepollo.msi`, then `msiexec /a Vibepollo.msi /qn TARGETDIR=<folder>` (unpacks only, installs nothing) | `hosts/vibepollo-<version>/` (the host is in `Apollo\`) |
| `Sunshine-Windows-AMD64-lite.zip` | any unzip; take the `Sunshine` folder | `hosts/sunshine-<version>/` |
| `MoonlightPortable-x64-<version>.zip` | any unzip, then create an empty `portable.dat` next to `Moonlight.exe` | `clients/moonlight-<version>/` |

Then update `setup.json` and record. Each host pairs with Moonlight by itself on its first
run.

What the reference hosts do to the PC while they run (the same as they do when installed):

- They switch the default playback device to a streaming device during a stream and back
  afterwards, as Norishine does.
- Vibepollo captures with Windows.Graphics.Capture through its helper by default. It writes
  the helper's log to `%APPDATA%\Sunshine\logs`. At start it tries to restart the Sunshine
  virtual display adapter when one is installed; as a user process that's refused, and it
  goes on with the active display.

### Results

First references recorded on 18 November 2026. PC: AMD Ryzen 7 5700X, AMD Radeon RX 6700 XT
(AMF, HEVC), Windows 11, one 1920×1080 60 Hz display. Then the check ran against `master` at
`d25025f0`, with Norilight `main` at `46fcc987`, **Performance goal**, three runs:

| Metric | Norishine | Apollo 0.4.6 | Vibepollo 2.17.6 | Sunshine 2026.914.233613 |
| --- | --- | --- | --- | --- |
| Time to first frame (ms) | 5156 | 5019 same | 5276 same | 4872 same |
| Capture to send (ms) | 3.0 | 5.9 **better** | 3.3 same | 4.1 **better** |
| Decode time (ms) | 0.48 | 0.52 same | 0.40 same | 0.44 same |
| Dropped frames (%) | 0.28 | 0.08 same | 0.08 same | 0.08 same |
| Frames shown per second | 55.2 | 53.8 same | 55.5 same | 51.6 **better** |
| Frame interval, 99th percentile (ms) | 34.0 | 34.5 same | 34.9 same | 34.4 same |
| Stutters per minute | 3 | 3 same | 1 same | 2 same |
| Frames shown per second at 5 % loss | 53.2 | 49.2 **better** | 53.2 same | 53.8 same |
| Dropped frames, loss stream (%) | 5.9 | 5.6 same | 5.5 same | 5.7 same |
| Recovery after a 300 ms blackout (ms) | 48 | 47 same | 45 same | 50 same |
| Video encoder load (%) | 17.0 | 16.9 same | 16.4 same | 16.9 same |
| Host process CPU (%) | 0.75 | 0.68 same | 0.39 same | 0.69 same |
| Whole PC CPU (%) | 44 | 58 same | 56 same | 43 same |

**Verdict: not worse than any reference on any metric.**

What it shows:

- Capture to send is where the hosts differ most: Norishine 2.7–3.6 ms, Vibepollo 3.3–3.5,
  Sunshine 4.0–4.2, Apollo 4.3–6.3. Everything else is within the spread between runs.
- Recovery after a blackout is about 50 ms everywhere: once packets flow again, every host
  sends a key frame within a few frames.
- Time to first frame is about 5 seconds for every host, and nearly all of it is the client
  starting up. Roadmap N5 needs a finer measurement of the steps.
- **To watch:** Norishine with Norilight dropped 0.2–0.3 % of frames in every clean run, the
  references 0.03–0.08 %. That's within the tolerance (half a point), but it's consistent.
  These are frames the client counts as lost on the network, on a loopback connection with
  nothing dropped. The cause (Norilight or the host) is not yet known.
- Apollo had one run with 51 stutters a minute and only 42 frames a second; its other runs
  were normal. The medians keep one bad run from deciding the result.

## Host benchmark (stream_bench.py)

A fixed stream, run the same way each time, so changes to the host (encoder settings,
capture, auto-tuning) can be compared against numbers measured before them. Roadmap M8
([ROADMAP.md](ROADMAP.md)).

### What it does

`scripts/benchmark/stream_bench.py`:

1. Signs in to the host's web UI and notes its version, encoder and the settings that change
   the result (presets, error correction, adaptive bitrate).
2. Opens a window of moving, changing blocks on the host (`scripts/benchmark/motion.py`,
   seeded, about 60 frames per second), so the encoder gets the same work every run.
3. Starts a stream with Norilight: the `Desktop` app at 1920×1080, 60 fps, 20 Mbps, HEVC,
   windowed, V-sync and frame pacing off.
4. Waits 10 seconds, then reads `GET /api/v1/stats` once a second for 60 seconds.
5. Ends the stream from the web UI (`POST /api/v1/devices/{uuid}/disconnect`), so the
   client exits on its own.
6. Writes `build/benchmark/bench-<time>.json` (settings, scenario, summary, every sample) and
   a `.csv` of the samples.

The summary has, over the sampled minute:

| Field | Meaning |
| --- | --- |
| `fps`, `bitrate_kbps` | Frame rate and video bitrate sent (min, average, max of the 5-second windows) |
| `latency_ms` | Capture to handed to the network: lowest, average, highest |
| `send_ms` | Time to send each frame (FEC, encryption, pacing) |
| `recovery_requests` | Key frame requests and lost reference frames the client reported |
| `key_frames` | Key frames sent |
| `host_cpu_percent`, `process_cpu_percent`, `gpu_percent`, `encoder_percent` | Host load (Windows only) |
| `host_limited_samples` | Seconds the adaptive controller saw the encoder fall behind |
| `adaptive_end` | Where the adaptive bitrate and FEC ended up |

### Running it

On the host, with a development build running (`build/sunshine.exe dev/sunshine.conf`, see
[DEVELOPMENT.md](DEVELOPMENT.md)) and a Norilight paired with it that may start apps:

```sh
pip install cryptography
python scripts/benchmark/stream_bench.py --client C:\path\to\Norilight.exe --label baseline
```

- The web UI password is read from `build/dev/webui-password.txt` (`--password-file`) or
  `NORISHINE_WEB_PASSWORD`. It isn't printed or stored in the results.
- A portable Norilight must start in the folder with `portable.dat`: pass it as
  `--client-dir`.
- `--device NAME` picks the paired device's stream when several are paired.
- `--no-client` samples a stream that is already running, from any client and any machine
  (for example over Wi-Fi), instead of starting one. The motion window isn't opened then.
- `--resolution`, `--fps`, `--bitrate`, `--codec`, `--duration`, `--warmup` change the
  scenario; results are only comparable with the same values.

Close other programs on the host first, and run it three times: one run varies by a few
frames per second and some host load.

### Baseline

Measured on 18 November 2026, before M9.4 (encoder setting recommendations) and any other
tuning, with the defaults above. Host and client on the same PC (Norilight streams over
loopback), so the network adds nothing.

- Host: development build of `master` at `e19a2afc`, all encoder and network settings at
  their defaults (`amd_quality` balanced, `fec_percentage` 20, `adaptive_bitrate` on).
- PC: AMD Ryzen 7 5700X, 32 GB, AMD Radeon RX 6700 XT (encoder `amdvce`), Windows 11.
- Client: Norilight `main` at `74e179a8` (portable). Its Tuning goal changes the host's
  encoder preset for the stream ([AUTOTUNE.md](AUTOTUNE.md)), so each table names it.

The benchmark doesn't set the goal: it streams with the client profile's. Note it with the
results (`--label`); the host's log says which one a stream used (`Tuning (performance)`).

**Performance goal** (the host used AMF Speed for these streams):

| Run | fps (min / avg / max) | Latency ms (min / avg / max) | Send ms avg | Bitrate kbps avg | Recovery | Host CPU % avg | Encoder % avg |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 52.0 / 53.7 / 57.6 | 2.33 / 4.37 / 6.80 | 0.11 | 6885 | 0 | 26.9 | 40.9 |
| 2 | 41.6 / 51.7 / 58.8 | 2.50 / 4.64 / 10.47 | 0.14 | 6299 | 0 | 52.3 | 39.3 |
| 3 | 50.0 / 56.2 / 59.0 | 2.07 / 4.40 / 6.40 | 0.13 | 7272 | 0 | 42.6 | 42.8 |

**Balanced goal** (the host's own preset, `amd_quality` balanced). Measured the same day with
the M9.4 build, which records each stream for its recommendations but encodes and sends the
same way:

| Run | fps (min / avg / max) | Latency ms (min / avg / max) | Send ms avg | Bitrate kbps avg | Recovery | Host CPU % avg | Encoder % avg |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 38.4 / 49.6 / 57.8 | 2.63 / 4.65 / 6.76 | 0.14 | 5866 | 0 | 54.4 | 38.4 |
| 2 | 47.2 / 53.8 / 58.4 | 1.99 / 4.34 / 8.00 | 0.12 | 6851 | 0 | 35.8 | 40.9 |
| 3 | 52.6 / 56.5 / 58.6 | 1.86 / 4.23 / 7.06 | 0.11 | 7149 | 0 | 32.7 | 43.1 |

What it shows:

- The encoder keeps up: about 4.5 ms from capture to network, under a third of the 16.7 ms
  frame interval, at about 40 % encoder load. The controller never saw it fall behind. Speed
  and Balanced presets measure the same here, within the spread between runs.
- The frame rate is below 60 because the host's desktop doesn't change 60 times a second
  everywhere; it follows the motion window, and dips when the host's CPU is busy (Performance run 2, Balanced run 1).
- Nothing was lost on loopback. Error correction still went to 30 % in each stream's first
  seconds and back to 20 % about 30 seconds later; fixed in M10, see below. The adaptive bitrate
  stayed at the stream's start: 14988 kbps, the 20000 the client asked for less room for
  error correction and audio.
- Bitrate sent is much lower than the target: the test picture compresses well.

Next measurements go below, with what changed.

## M10: error correction at stream start

**What changed:** the adaptive controller no longer counts the key frames a client asks for
while its decoder starts as loss. Norilight asks for four or five in the first two seconds;
those requests stayed in the controller's 3-second loss window past the 3-second startup
grace, so its first decision raised error correction to 30 %. Now requests (and client loss
reports) from the grace period are dropped when it ends, as they already were after a change
([AUTOTUNE.md](AUTOTUNE.md#live-adaptation-host-srcadaptive)).

Measured with `--warmup 0 --duration 60`, so the samples start about 1.5 s into the stream
(`stream_s` in the samples), **Performance goal**. Before: `master` at `335e42b6`; after: the
same with the change.

Error correction, second by second (time into the stream):

| Run | Before | After |
| --- | --- | --- |
| 1 | 20 % to 2 s, **30 % 3–24 s, 25 % 25–35 s**, 20 % from 36 s; average 24.6 % | 20 % throughout |
| 2 | 20 % to 3 s, **30 % 4–25 s, 25 % 26–35 s**, 20 % from 36 s; average 24.5 % | 20 % throughout |
| 3 | 20 % to 2 s, **30 % 3–24 s, 25 % 25–35 s**, 20 % from 36 s; average 24.6 % | 20 % throughout |

Every stream got 4 or 5 key frame requests in its first 2 seconds, before and after, and no
recovery requests after that. With 20 % error correction each frame is sent with 20 % more
data; with 30 %, with 30 %. So the first 35 seconds of every stream sent about 6 % more than
needed (8 % for 22 seconds, 4 % for 10), for nothing.

The rest of the stream is unchanged:

| Run | fps (min / avg / max) | Latency ms (min / avg / max) | Send ms avg | Bitrate kbps avg | Recovery | Host CPU % avg | Encoder % avg |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Before 1 | 32.2 / 42.5 / 53.0 | 2.82 / 4.85 / 51.40 | 0.17 | 4909 | 0 | 72.8 | 33.2 |
| Before 2 | 47.8 / 53.8 / 58.8 | 2.49 / 4.53 / 22.22 | 0.13 | 6740 | 0 | 51.9 | 40.7 |
| Before 3 | 37.4 / 51.9 / 58.2 | 2.18 / 4.55 / 16.78 | 0.14 | 6399 | 0 | 54.4 | 39.0 |
| After 1 | 42.2 / 55.5 / 59.6 | 1.85 / 4.41 / 36.95 | 0.12 | 7099 | 0 | 44.8 | 41.8 |
| After 2 | 34.0 / 51.7 / 58.8 | 2.13 / 4.51 / 21.58 | 0.13 | 6483 | 0 | 53.0 | 39.2 |
| After 3 | 38.2 / 48.6 / 54.8 | 2.70 / 4.69 / 32.86 | 0.14 | 5724 | 0 | 60.2 | 37.2 |

Recovery counts requests after the first sample, so it doesn't include the startup ones. The
highest latency is higher than in the baseline because these samples include the stream's
first seconds. Before run 1 had the host's CPU busy with something else (73 %).

The benchmark's samples now also carry `stream_s`, `key_frame_requests`, `ref_invalidations`,
`adaptive_reason` and `adaptive_change_s`, and the results record the other AMF settings.

## M10: AMF options

**What changed:** nothing in the code; this compares the AMD encoder's options on this PC
(RX 6700 XT, `amdvce`, HEVC) to see whether a different default would help. Each option was
set alone in the dev host's config, everything else at its default. **Balanced goal**, so
the host's own preset applies (with Balanced the host logs no `Tuning` line). Standard
scenario (10 s warmup, 60 s of samples). Host: a development build with the same code as
`master` at `dea0fab0`, plus the display-restore change (#38), which doesn't touch streaming.

| Option | Run | fps (min / avg / max) | Latency ms (min / avg / max) | Send ms avg | Bitrate kbps avg | Recovery | Host CPU % avg | Encoder % avg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Defaults: `amd_usage` ultralowlatency, `amd_quality` balanced | 1 | 46.4 / 53.6 / 59.0 | 2.44 / 4.51 / 9.37 | 0.12 | 6755 | 0 | 49.6 | 41.0 |
| | 2 | 43.4 / 51.8 / 58.4 | 2.14 / 4.46 / 10.39 | 0.12 | 6351 | 0 | 50.0 | 39.6 |
| | 3 | 29.8 / 43.9 / 57.6 | 2.34 / 4.95 / 11.18 | 0.16 | 5266 | 0 | 69.2 | 33.5 |
| `amd_quality` quality | 1 | 50.6 / 56.3 / 58.8 | 2.83 / 4.92 / 8.14 | 0.12 | 7267 | 0 | 41.1 | 47.4 |
| | 2 | 50.0 / 56.3 / 58.4 | 2.76 / 4.82 / 7.52 | 0.11 | 7249 | 0 | 36.7 | 47.0 |
| | 3 | 49.8 / 55.8 / 59.0 | 2.62 / 4.85 / 7.52 | 0.11 | 7097 | 0 | 35.1 | 46.6 |
| `amd_usage` lowlatency | 1 | 29.4 / 44.8 / 55.4 | 2.03 / 4.99 / 10.64 | 0.17 | 5351 | 0 | 67.4 | 34.3 |
| | 2 | 53.2 / 55.7 / 57.6 | 2.10 / 4.36 / 8.03 | 0.11 | 7094 | 0 | 31.3 | 42.3 |
| | 3 | 54.6 / 57.7 / 59.2 | 1.73 / 4.35 / 7.03 | 0.10 | 7317 | 0 | 27.9 | 43.6 |
| `amd_usage` lowlatency_high_quality | 1 | 54.2 / 57.1 / 58.6 | 2.55 / 4.75 / 9.13 | 0.12 | 7426 | 0 | 39.9 | 47.2 |
| | 2 | 33.2 / 48.9 / 57.2 | 3.13 / 5.17 / 10.08 | 0.14 | 6056 | 0 | 60.2 | 41.3 |
| | 3 | 37.8 / 52.7 / 58.4 | 2.69 / 4.94 / 9.35 | 0.13 | 6765 | 0 | 52.6 | 43.9 |
| `amd_preanalysis` enabled | 1 | 50.8 / 55.8 / 58.4 | 2.49 / 4.75 / 8.38 | 0.12 | 7122 | 0 | 37.2 | 45.9 |
| | 2 | 52.2 / 54.7 / 58.4 | 2.54 / 4.87 / 7.19 | 0.11 | 7003 | 0 | 24.1 | 45.1 |
| | 3 | 35.0 / 50.9 / 58.0 | 2.73 / 5.18 / 9.30 | 0.14 | 6183 | 0 | 48.4 | 42.6 |

Averaged over the runs, with the encoder load and bitrate divided by the frames actually
sent (the frame rate follows the host's CPU load, which other programs changed between
runs). `amd_quality` speed is the Performance goal's preset; its numbers are the baseline's
Performance table above.

| Option | fps avg | Latency avg ms | Encoder % per 60 fps | kbit per frame |
| --- | --- | --- | --- | --- |
| Defaults (ultralowlatency, balanced) | 49.8 | 4.64 | 45.9 | 123 |
| `amd_quality` speed (Performance baseline) | 53.9 | 4.47 | 45.7 | 126 |
| `amd_quality` quality | 56.1 | 4.86 | 50.2 | 128 |
| `amd_usage` lowlatency | 52.7 | 4.57 | 45.6 | 125 |
| `amd_usage` lowlatency_high_quality | 52.9 | 4.95 | 50.1 | 127 |
| `amd_preanalysis` enabled | 53.8 | 4.93 | 49.7 | 126 |

What it shows:

- Every option stays within about 0.5 ms of the others, under a third of a 16.7 ms frame,
  and the encoder kept up in every run. The run-to-run spread (0.5 ms) is as large as the
  differences, so the presets can't be told apart by latency here.
- The higher-quality options (`quality`, `lowlatency_high_quality`, pre-analysis) cost about
  4 points of encoder load per 60 fps and 0.2–0.3 ms. `lowlatency` measures the same as
  the default `ultralowlatency`.
- Bits per frame barely change (123–128 kbit): the test picture is easy, and the benchmark
  measures speed, not picture quality. Picture quality needs a comparison of decoded frames
  against the source, which the benchmark doesn't do.

**Decision:** keep the defaults. Nothing here is faster than `ultralowlatency` with
`balanced`, and the Quality goal's `quality` preset ([AUTOTUNE.md](AUTOTUNE.md#encoder-per-goal-norishine))
costs little enough on this card at 1080p60. Pre-analysis isn't turned on: its cost is
measured, its benefit isn't. Revisit with a picture-quality measurement, or at 4K or
120 fps, where the encoder has less room.

## M10: frame cap matched to the client

**Question:** would the host save work by never encoding more frames than the client's
display shows? Measured before building anything: the same stream asked for at 120, 60 and
30 fps. The host's display (and the client's, on this PC) runs at 60 Hz. **Balanced goal**,
standard scenario, same build as the AMF runs; the 60 fps runs are the AMF defaults above.

| Asked for | Run | fps (min / avg / max) | Latency ms (min / avg / max) | Send ms avg | Bitrate kbps avg | Recovery | Host CPU % avg | Encoder % avg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 120 fps | 1 | 37.8 / 50.0 / 58.0 | 3.92 / 4.72 / 12.26 | 0.14 | 5818 | 0 | 57.3 | 39.0 |
| | 2 | 48.4 / 55.1 / 58.8 | 3.84 / 4.56 / 11.38 | 0.11 | 6664 | 0 | 42.1 | 41.8 |
| | 3 | 50.0 / 54.2 / 58.4 | 3.84 / 4.52 / 9.85 | 0.11 | 6549 | 0 | 39.4 | 41.1 |
| 60 fps | 1 | 46.4 / 53.6 / 59.0 | 2.44 / 4.51 / 9.37 | 0.12 | 6755 | 0 | 49.6 | 41.0 |
| | 2 | 43.4 / 51.8 / 58.4 | 2.14 / 4.46 / 10.39 | 0.12 | 6351 | 0 | 50.0 | 39.6 |
| | 3 | 29.8 / 43.9 / 57.6 | 2.34 / 4.95 / 11.18 | 0.16 | 5266 | 0 | 69.2 | 33.5 |
| 30 fps | 1 | 29.6 / 30.0 / 30.0 | 2.46 / 3.70 / 8.18 | 0.17 | 5004 | 0 | 61.8 | 23.8 |
| | 2 | 30.0 / 30.0 / 30.0 | 2.34 / 3.27 / 7.77 | 0.16 | 5445 | 0 | 51.3 | 23.7 |
| | 3 | 30.0 / 30.0 / 30.0 | 2.00 / 3.16 / 5.78 | 0.15 | 5722 | 0 | 29.6 | 23.6 |

| Asked for | fps avg | Encoder % avg | Encoder % per 60 fps | Bitrate kbps avg | kbit per frame |
| --- | --- | --- | --- | --- | --- |
| 120 fps | 53.1 | 40.6 | 46.0 | 6344 | 119 |
| 60 fps | 49.8 | 38.0 | 45.9 | 6124 | 123 |
| 30 fps | 30.0 | 23.7 | 47.3 | 5390 | 180 |

What it shows:

- **The host already never encodes more frames than its own display shows.** Asked for
  120 fps on a 60 Hz display, it sent the same 50–55 fps as at 60, at the same encoder load:
  capture only produces a frame when the desktop changes.
- Encoder load follows the frames sent: halving them halves it. The bitrate doesn't: at 30
  fps each frame carries twice the motion, so 30 fps sent only 12 % less than 60.
- A cap would only change anything when the host's display is faster than the client's
  (say a 144 Hz host streamed to a 60 Hz phone) and the client asks for more than its display
  shows. Then it would save encoder load, and little bitrate.

**Decision: not built.** The case is narrow, and the clients cover it already:

- Moonlight and Norilight turn V-sync off when the stream is faster than the display, on
  purpose: the extra frames show up as newer parts of a torn picture, which lowers latency.
  A host cap would take that away from users who chose a high frame rate.
- Norilight picks the frame rate from the client's monitor when the profile matches it, and
  the host can't tell a choice from a mistake. Neither Norilight nor stock clients send the
  display's refresh rate to the host today (`display_hz` in the client report is accepted
  but no client fills it in).

Encoder load was 40 % with room to spare in every run, so there's nothing to win here on
this PC. If a client-side rule is wanted instead (for example: with V-sync on, Norilight asks
for no more than the display's refresh rate), it belongs in Norilight's launch settings.

## M10: capture method

**Question:** what would capturing with Windows.Graphics.Capture from a user-session helper
bring ([CAPTURE-HELPER.md](CAPTURE-HELPER.md))? The development host runs as the user, so it
can use `capture = wgc` directly. **Balanced goal**, standard scenario, same build as the AMF
runs; Desktop Duplication is the AMF defaults above.

| Capture | Run | fps (min / avg / max) | Latency ms (min / avg / max) | Send ms avg | Bitrate kbps avg | Recovery | Host CPU % avg | Encoder % avg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Desktop Duplication (default) | 1 | 46.4 / 53.6 / 59.0 | 2.44 / 4.51 / 9.37 | 0.12 | 6755 | 0 | 49.6 | 41.0 |
| | 2 | 43.4 / 51.8 / 58.4 | 2.14 / 4.46 / 10.39 | 0.12 | 6351 | 0 | 50.0 | 39.6 |
| | 3 | 29.8 / 43.9 / 57.6 | 2.34 / 4.95 / 11.18 | 0.16 | 5266 | 0 | 69.2 | 33.5 |
| `capture = wgc` | 1 | 53.6 / 55.6 / 57.4 | 2.05 / 3.85 / 6.15 | 0.11 | 7041 | 0 | 18.3 | 43.1 |
| | 2 | 45.2 / 54.8 / 58.2 | 1.90 / 3.96 / 7.92 | 0.12 | 6830 | 0 | 32.3 | 42.5 |
| | 3 | 34.0 / 45.8 / 54.6 | 1.67 / 4.22 / 9.23 | 0.15 | 5437 | 0 | 63.1 | 35.5 |

WGC measured 0.6 ms lower on average (4.01 against 4.64 ms) at the same encoder load. The
latency clock doesn't start at the same point, though: Desktop Duplication stamps a frame
when the app presented it, WGC when the desktop was composed, which is later. So this doesn't
show WGC is faster; that needs latency measured at the client.

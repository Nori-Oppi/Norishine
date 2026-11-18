# Stream benchmark

A fixed stream, run the same way each time, so changes to the host (encoder settings,
capture, auto-tuning) can be compared against numbers measured before them. Roadmap M8
([ROADMAP.md](ROADMAP.md)).

## What it does

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

## Running it

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

## Baseline

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

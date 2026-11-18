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
- Client: Norilight `main` at `74e179a8` (portable), Balanced tuning goal.

| Run | fps (min / avg / max) | Latency ms (min / avg / max) | Send ms avg | Bitrate kbps avg | Recovery | Host CPU % avg | Encoder % avg |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 52.0 / 53.7 / 57.6 | 2.33 / 4.37 / 6.80 | 0.11 | 6885 | 0 | 26.9 | 40.9 |
| 2 | 41.6 / 51.7 / 58.8 | 2.50 / 4.64 / 10.47 | 0.14 | 6299 | 0 | 52.3 | 39.3 |
| 3 | 50.0 / 56.2 / 59.0 | 2.07 / 4.40 / 6.40 | 0.13 | 7272 | 0 | 42.6 | 42.8 |

What it shows:

- The encoder keeps up: about 4.5 ms from capture to network, under a third of the 16.7 ms
  frame interval, at about 40 % encoder load. The controller never saw it fall behind.
- The frame rate is below 60 because the host's desktop doesn't change 60 times a second
  everywhere; it follows the motion window, and dips when the host's CPU is busy (run 2).
- Nothing was lost on loopback, so error correction stayed at 20 %. The adaptive bitrate
  stayed at the stream's start: 14988 kbps, the 20000 the client asked for less room for
  error correction and audio.
- Bitrate sent is much lower than the target: the test picture compresses well.

Next measurements go in the same table, with what changed.

# Auto-tuning: best stream settings for each display, network and machine

Norishine and Norilight pick and keep the best streaming settings they can, from what they
measure about the client's display and decoder, the host's encoder and load, and the network.
Tuning is continuous: when the network or a machine changes during a stream, the stream adapts.

This is roadmap milestone M9 ([ROADMAP.md](ROADMAP.md)). It builds on M8's measurements.

## Two time scales

Some settings can only be chosen when a stream starts; others can change while it runs.

| When | What | Who decides |
| --- | --- | --- |
| Before the stream (launch) | Resolution, frame rate, codec, HDR, 4:4:4, start bitrate, decoder, V-sync and frame pacing | Norilight, from what both sides can do (capability discovery) |
| During the stream (live) | Bitrate, error correction (FEC) | The host, from host measurements and the client's reports |
| Between streams | Next start bitrate and frame rate for this host and network; host encoder settings (recommended, applied by hand) | Norilight and the host, from what earlier streams measured |

The live part runs on the host, so stock Moonlight and Artemis clients benefit too. Norilight
adds what only a client can measure.

## Live adaptation (host, `src/adaptive.*`)

**Done.** Each stream gets a controller, run once a second by the control thread. It is on by
default (`adaptive_bitrate`); `adaptive_min_bitrate` and `adaptive_max_fec` bound it.

Signals:

| Signal | From | Means |
| --- | --- | --- |
| Recovery requests (key frame requests, reference frame invalidations) | Every client | A frame was lost that FEC couldn't repair |
| Frames lost, percent | Norilight report | Same, measured directly |
| Round-trip time against its lowest value of the last minute | Norilight report | Queues filling on the path: congestion before loss |
| Decode time against the frame interval | Norilight report | The client can't keep up; more bitrate won't help |
| Frame rate sent and capture-to-send latency | Host | The host encoder can't keep up; more bitrate won't help |

Rules:

- **Congestion** (heavy loss, or loss while the round-trip time rises): bitrate × 0.7.
- **Rising delay** for two seconds without loss: bitrate × 0.85.
- **Loss without rising delay** (radio, a busy link): FEC +10 points, up to `adaptive_max_fec`.
  Loss in more than two seconds out of ten: bitrate × 0.85 as well.
- **Clean for 10 s** and 10 s since the last change: bitrate × 1.15, up to the start bitrate.
  Near the bitrate where trouble last started (within 5 %, remembered for two minutes): wait
  30 s and step × 1.04 instead.
- **Clean for 20 s**: FEC −5 points every 10 s, back to `fec_percentage`.
- No changes in the first 3 s of a stream, and loss in the 2 s after a change is ignored.
  Key frame requests and client loss reports from the first 3 s don't count later either:
  clients ask for a few key frames while their decoder starts (Norilight four), which isn't
  network loss.
- Never above the bitrate the client started with (after `max_bitrate`), never below
  `adaptive_min_bitrate` (or the start bitrate if lower). No raises while the host or client
  is the bottleneck.

A bitrate change rebuilds the encoder with the new bitrate, the same way a display change
does: the client gets one key frame and nothing else changes. That's why raises are slow and
rare. Encoders with in-place reconfiguration (NVENC `nvEncReconfigureEncoder`) can skip the
rebuild later (M10). FEC is set per frame and changes at once.

Checks: `tests/unit/test_adaptive.cpp`.

### Client reports

`POST /api/v1/session/report` on the HTTPS port, once a second during a stream. See
[api.md](api.md). Every field is optional:

| Field | Meaning |
| --- | --- |
| `loss_percent` | Video frames lost by the network (after FEC) over the last report interval, percent |
| `rtt_ms`, `rtt_variance_ms` | Round-trip time to the host |
| `decode_ms` | Average decode time |
| `fps` | Frames received per second |
| `display_hz` | Refresh rate of the display the stream is on |

Reports older than 3 s are ignored, so a client that stops reporting falls back to host-only
signals.

## Tuning goals

Each Norilight profile has a **Tuning** goal: **Performance**, **Balanced** (the default),
**Quality** or **Off**. It steers every choice below; Off leaves the profile's settings as
they are.

| | Performance | Balanced | Quality |
| --- | --- | --- | --- |
| Codec (when the profile's codec is automatic) | HEVC if both sides do it in hardware | HEVC if both sides do it in hardware | AV1 if both sides do it in hardware, else HEVC |
| HDR | Off | Profile's setting | On when the display shows HDR and both sides can do 10-bit |
| V-sync and frame pacing | Off | Profile's settings | On |
| Start bitrate | Less | | More |
| Host encoder preset | Fastest | Host's settings | Higher quality |

## Launch settings (Norilight)

**Done.** At each launch Norilight looks at both machines and picks the settings above for
that stream only; the profile is never changed. Inputs:

| Input | Where it comes from |
| --- | --- |
| Display resolution and refresh rate | The target monitor (the profile's "match the monitor" options) |
| Display HDR | Windows: the monitor's DXGI output colour space (HDR on in Windows settings); macOS: the screen's extended dynamic range |
| Decoder: hardware HEVC, AV1, 10-bit | Norilight's decoder probe at the stream's resolution and frame rate (D3D11VA, DXVA2, VideoToolbox) |
| Host encoder: HEVC, AV1, 10-bit | serverinfo (`ServerCodecModeSupport`, `MaxLumaPixelsHEVC`) |
| Link: Ethernet, Wi-Fi, other (VPN, tunnel) | The local interface that routes to the host |
| Earlier streams to this host on this network | Norilight's settings (`autotune` group), see below |

**Start bitrate**, when the profile's bitrate is the recommended one:

- Hosts that adapt (Norishine, feature `adaptive`): the recommended bitrate (×1.25 for HDR),
  times 2.5 on Ethernet or 1.5 on Wi-Fi and other links, times 0.6 (Performance), 1 or 1.4
  (Quality). Capped at 300 Mbps (Ethernet), 150 Mbps (Wi-Fi), 100 Mbps (other) and the
  profile's maximum.
- After a stream to that host on that network: if the host had to lower the bitrate, start at
  what it held (×1.0, 1.2 or 1.3 by goal); if it never had to, start 25 % higher than last
  time, so the start bitrate climbs until the network says stop. Performance never climbs
  past its own start.
- Other hosts can't lower the bitrate during a stream, so the start stays near the
  recommended bitrate: ×0.75 (Performance), 1 or 1.25 (Quality).

**Learning.** At the end of a stream of 30 seconds or more, Norilight keeps, per host and per
network (link type and subnet), what the host sustained: the median bitrate after the first
seconds if it lowered it, otherwise the start bitrate. Two limited streams in a row are
averaged. The network is identified without sending anything.

Checks: `tests/autotune` in Norilight.

## Encoder per goal (Norishine)

**Done.** Norilight sends its goal with the launch request (`norishineTuning`, feature
`tuning`), and the host picks the encoder preset for that stream only:

| Encoder | Performance | Quality |
| --- | --- | --- |
| NVENC | P1, single pass | At least P4, two-pass (quarter resolution) |
| AMD AMF | Speed | Quality |
| Intel Quick Sync | veryfast | slow |
| Software x264/x265 | ultrafast | veryfast |
| Software SVT-AV1 | 12 | 10 |

Balanced keeps the host's settings, as do VA-API and VideoToolbox. `client_tuning` (on by
default) turns this off. Checks: `tests/unit/test_tuning.cpp`.

## Host settings (Norishine)

**Done (M9.4).** The host recommends changes to its own encoder settings from what earlier
streams measured. Nothing changes until someone applies a recommendation, in the web UI
(Settings, "Recommended") or in Norilight (Host Settings).

**What each stream leaves behind.** Once a second while a stream runs, the host notes whether
the encoder kept up (the adaptive controller's test: fewer frames than asked while each takes
most of a frame interval), the capture-to-network latency, the video encoder load (Windows),
the frames the client reported lost and each time the adaptive controller raised error
correction. Streams of 30
seconds or more (not counting the first 5) are kept, the last 10 per encoder, in
`state/recommendations.json`, with the client's tuning goal and the values the host ran with.

**Rules.** Only the newest 5 streams that ran with a setting's current value count for that
setting, and at least 2 are needed: after a change, streams with the old value no longer count.

| Reason | When | Recommends |
| --- | --- | --- |
| `encoder_limited` | In at least half the streams that used the host's own preset (Balanced goal, or clients without a goal), the encoder fell behind in 10 % of the seconds or more, or was 90 % busy on average | One step faster: `nvenc_preset` P(n−1), `amd_quality` speed, `qsv_preset` and `sw_preset` the next faster |
| `headroom` | Every stream: encoder behind in under 2 % of the seconds, encoder load under 60 %, latency under half a frame interval; and most asked for Quality | One step higher quality, up to P5, `quality`, `slow` or `fast` |
| `loss` | In at least half the streams, 3 or more frames lost a minute, or error correction raised at least twice and at least once every two minutes (a raise in the first 5 seconds doesn't count) | `fec_percentage` +10, up to 50 |

The higher-quality preset needs the encoder load, so it isn't recommended where the host
can't measure it (software encoding, Linux, macOS). VA-API and VideoToolbox have no preset
setting; only error correction is recommended for them.

**Apply and undo.** Apply writes the values through the same path as `PATCH /api/v1/config`
and keeps the values from before; Undo puts those back (a setting changed by hand since is
left alone). Encoder presets and error correction take effect at once, also in running
streams (see "Settings during a stream" below). See [api.md](api.md),
`/api/v1/recommendations`. Checks: `tests/unit/test_recommend.cpp`.

## Settings during a stream (N9)

**Host settings.** Settings that don't need a restart (`restart_required` false in the
schema) take effect as soon as they're saved, from the web UI, Norilight's Host Settings, a
recommendation or the encoder test. Running streams take them over without reconnecting:

| Settings | In a running stream |
| --- | --- |
| Encoder settings (NVENC, AMF, Quick Sync, VideoToolbox, VA-API and software options, `qp`, `minimum_fps_target`) | The encoder is rebuilt with them, as for a bitrate change: one key frame |
| `fec_percentage`, `adaptive_max_fec`, `adaptive_min_bitrate` | Error correction and the controller's bounds change at once; error correction the controller raised stays within the new bounds |
| `max_bitrate` | Lowered below a stream's bitrate: the stream drops to it and never goes above it again |
| `adaptive_bitrate`, `client_tuning`, `ping_timeout` | Next stream |

Everything else (ports, capture, the encoder choice, displays, audio, input, files) is read
at startup and still needs a restart. The list is `config::live_kind()`. Checks:
`tests/unit/test_config_schema.cpp`, `tests/unit/test_adaptive.cpp`, and on the PC
`scripts/e2e/live_settings_check.py`.

## Platforms

The host controller is platform-neutral: it uses only stream measurements, and the rebuild
works with every encoder that captures and encodes on separate threads, which is all of them.
Host load figures (`/api/v1/stats`) are Windows-only for now; the controller doesn't need
them. Norilight's reports and launch picks work on Windows and macOS.

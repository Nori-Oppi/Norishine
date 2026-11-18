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
| Between streams | Next start bitrate and frame rate for this host and network; host encoder settings | Norilight and the host, from what earlier streams measured |

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

## Launch settings (Norilight)

**Planned.** Norilight's "Auto" choice for a profile setting picks it at each launch:

| Input | Where it comes from |
| --- | --- |
| Display resolution, refresh rate, HDR | Norilight's display list (Windows and macOS) |
| Decoder: hardware support per codec, 10-bit, 4:4:4, maximum resolution | Norilight's decoder probe (D3D11VA, DXVA2, VideoToolbox) |
| Host encoder: codecs, HDR, 4:4:4 | serverinfo (`ServerCodecModeSupport`), plus `encoder` from `/api/v1/status` |
| Network: wired or wireless, link speed, round-trip time | The client OS, and a timed request to the host |
| Earlier streams to this host on this network | Norilight, from `GET /api/v1/session` at the end of each stream |

Picks, most important first: resolution and frame rate of the display the stream opens on
(capped by decoder and encoder limits), the most efficient codec both sides do in hardware,
HDR when display, decoder and encoder all can, and a start bitrate from resolution, frame rate
and codec, capped by the link and by what earlier streams on this network sustained.
Changes are shown before they're saved, and the previous values can be restored.

## Host settings (Norishine)

**Planned.** Recommendations for encoder settings from the GPU, the encoder and the load
earlier streams measured (for example a faster NVENC preset when the encoder was the
bottleneck), shown with the current value and applied with `PATCH /api/v1/config`, with
the previous values kept for undo.

## Platforms

The host controller is platform-neutral: it uses only stream measurements, and the rebuild
works with every encoder that captures and encodes on separate threads, which is all of them.
Host load figures (`/api/v1/stats`) are Windows-only for now; the controller doesn't need
them. Norilight's reports and launch picks work on Windows and macOS.

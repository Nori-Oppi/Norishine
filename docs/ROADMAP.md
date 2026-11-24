# Norishine architecture decisions and roadmap

Status key: **done**, **in progress**, **planned**. Updated 2026-11-25: phases 2–5 are done, and the next
items are ranked under [Next](#next). Evidence for the
decisions is in [PHASE1-RESEARCH.md](PHASE1-RESEARCH.md).

## Decisions

### D1. Baseline: Apollo `master` (`adc5c5a0`), with full history

Confirmed 2026-11-15. A switch to Sunshine as the base was considered and declined,
and declined again at the review after M2: Norishine stays on Apollo.
Merge Apollo's history into this repository (unrelated-histories merge, so the
existing initial commit and remote stay valid and nothing is force-pushed). Keep
`apollo` and `sunshine` as remotes for future cherry-picks.

- Why not Vibepollo: size, churn, forked drivers and dependencies, no clean upstream
  merge base.
- Why not current Sunshine with Apollo's features ported on top: the Apollo features
  we need most (virtual display, permissions, OTP) sit in the files Sunshine changed
  most; estimated at several weeks before anything streams.
- Consequence: Norishine carries Apollo's maintenance. Sunshine security fixes are
  ported first, before any feature work.
- Review after M2 (**done**): a re-base onto Sunshine was re-estimated and not
  taken. Sunshine fixes keep coming in by cherry-pick (D6).

### D2. Web UI: small schema-first rewrite, not a port
None of the three existing frontends meets the goals. Apollo's is hand-written
per-setting markup with defaults held in the page; Sunshine's search is a DOM filter
on key names; Vibepollo's schema is frontend-only and ships two UIs.

- The **host owns one settings schema** in C++: key, type, default, range or enum,
  platform, tier (basic / advanced / dangerous), category, restart-required,
  label and description keys. The same table drives parsing, validation, defaults
  and the API, so nothing is duplicated.
- The web UI is a thin single-page app (Vue, vue-router, vue-i18n; no component
  framework) that renders from the schema, with custom widgets only where needed.
- Norilight renders the same schema natively.

### D3. Management API: `/api/v1`, two ways to authenticate
- Browser: session cookie login (HttpOnly, rate-limited, CSRF token), as today but hardened.
- Norilight: the paired client certificate it already presents, on the existing
  HTTPS port, gated by new permission bits (for example read host status, change
  settings, manage apps and clients). No admin password stored on the client.
- Management permissions are never granted automatically: not to the first paired
  device, not to legacy devices that currently default to "all". They are granted
  per device from the host's own UI.
- No path-regex tokens. Named scopes only.

### D4. Compatibility: additive only
- Advertise Nori features through one new optional serverinfo element carrying an
  API version and a feature list. Moonlight and Artemis ignore unknown elements.
- Keep every existing Apollo tag, parameter and control message unchanged.
- Norilight must keep feature-detecting and never assume a Norishine host.
- No protocol-breaking change is currently needed for anything on this roadmap.

### D5. State and configuration
- Atomic writes with a backup copy for all state files.
- Split what is currently one file into: user preferences, machine state and
  detected values, pairing data, and credentials.
- Versioned, one-way migrations that keep the original file as a backup. Import
  from an existing Apollo install is a first-class path; Vibepollo import is best effort.
- Web admin password moves to a proper key-derivation function, upgraded on next login.

### D6. Upstream sync
- Remotes `apollo` and `sunshine` are kept. Sunshine security advisories are
  checked for every release and ported before other work.
- Other Sunshine fixes are cherry-picked in batches, and each batch is recorded in
  `docs/UPSTREAM-SYNC.md` (Sunshine commit, our commit, or reason skipped).

### D7. Local testing next to an existing host
Test builds run from the build folder with their own config directory and a port
base away from the defaults (47989). They are never installed as a service, so an
existing Apollo, Vibepollo or Sunshine installation on the same machine is untouched.

### D8. Platform scope
Windows is the developed and tested target. Linux and macOS code is kept and not
deliberately broken, but is not verified until CI exists for it. A Linux build is planned
(N11): it starts with Linux CI, and Windows-only features (SudoVDA virtual displays, the
installer, display restore) are marked as such rather than ported first.

### D9. Reset
A reset never deletes outright: configuration and state are moved to a dated backup folder
next to them, then the host or client starts as on first run, in its setup wizard. Putting
the folder back undoes the reset. On the host a reset is started from the web UI (signed
in, with confirmation) or the command line, never from a paired client.

## Done

Phases 2 to 5 are finished, and so are N1 to N6, N9, N10, N14, N16 and N18. Details are in the linked docs and the git history.

| Milestone | What it gave | More |
| --- | --- | --- |
| M0.1–M0.6 Baseline | Apollo history merged, renamed to Norishine, clean Windows build, installer that replaces or sits next to Apollo and Vibepollo, CI with tests, Sunshine security fixes, safer permission defaults | [DEVELOPMENT.md](DEVELOPMENT.md), [UPSTREAM-SYNC.md](UPSTREAM-SYNC.md) |
| M1 Settings schema | One C++ schema for all 103 settings, validated `/api/v1/config`, atomic writes with backups | [api.md](api.md) |
| M2–M3 Web UI | Every page rebuilt on the schema, searchable settings, devices and permissions, live log; Bootstrap gone; hardened sign-in with CSRF and PBKDF2 | [Norishine Guide](https://github.com/Nori-Oppi/norilight-docs/wiki/Norishine-Guide), [Features](https://github.com/Nori-Oppi/norilight-docs/wiki/Norishine-Features) |
| M4 State and migrations | State split into host, pairing and credentials files with versioned migrations; import from Apollo and Vibepollo, carry-back on uninstall | [KNOWN-ISSUES.md](KNOWN-ISSUES.md) |
| M5–M7 Norilight integration | Capability element, client-certificate management API with permission bits; Norilight's Host Settings and Manage Host pages | [api.md](api.md) |
| Updates | Update the host from the web UI or a paired client, checksum-checked | [Norishine Guide](https://github.com/Nori-Oppi/norilight-docs/wiki/Norishine-Guide#updating) |
| M8 Diagnostics | `GET /api/v1/stats` (host load, per-stream metrics), Stats tab in Norilight, repeatable benchmark with a baseline | [BENCHMARK.md](BENCHMARK.md) |
| M9 Auto-tuning | Live bitrate and FEC adaptation for every client, Norilight's reports, Tuning goals with learned launch settings, encoder recommendations with Apply and Undo | [AUTOTUNE.md](AUTOTUNE.md) |
| N1 Reset button (Norilight) | Settings › Reset Norilight: settings moved to a dated backup (D9), restart into the first-start wizard; `Norilight --reset` | [Norilight Guide](https://github.com/Nori-Oppi/norilight-docs/wiki/Norilight-Guide#resetting-norilight) |
| N3 Help links (Norilight) | Norilight's help buttons open the new [norilight-docs wiki](https://github.com/Nori-Oppi/norilight-docs/wiki) (setup, troubleshooting, hardware decoding, gamepad mapping) instead of moonlight-docs; the wiki also holds both apps' guides | [wiki](https://github.com/Nori-Oppi/norilight-docs/wiki) |
| N4 Setup wizard and reset | First start in the web UI: sign-in, import from Apollo or Vibepollo, host name, display, encoder check, first device and its permissions; Troubleshooting › Reset Norishine and `norishine --reset` move everything to a dated backup (D9) and open the wizard again | [Norishine Guide](https://github.com/Nori-Oppi/norilight-docs/wiki/Norishine-Guide#first-start) |
| N5 Near-instant stream start | Host and Norilight log every step from choosing the app to the first frame; the host no longer probes every encoder again on the first stream after it starts, and Norilight opens the audio device once, in the background, and checks each decoder once. Launch 1060 → 895 ms, resume 975 → 824 ms, first launch 2228 → 1080 ms; `scripts/benchmark/start_time.py` measures it | [BENCHMARK.md](BENCHMARK.md#stream-start-start_timepy) |
| N6 Optimise | Norilight's Optimise (a Norishine host's menu): short test streams of the host's test picture per codec, then lower frame rate or resolution if needed, then higher bitrates; the profile's Tuning goal picks among those that keep up, and Save writes it to the profile with Undo. The host's encoder test (`/api/v1/optimise`, Test encoder in the web UI) times each encoder preset on the test picture and applies the best, undone like the recommendations | [Norilight Guide](https://github.com/Nori-Oppi/norilight-docs/wiki/Norilight-Guide#optimise), [api.md](api.md#get-apiv1optimise) |
| N14 Fewer pacer drops (Norilight) | The full-queue drops all came from the first second of a stream: the decoder starts after the stream, so it began with a backlog of stale frames. A new decoder now shows its first frame and skips that backlog for a fresh key frame. Dropped frames 0.08 → 0.03 % in the benchmark check | [BENCHMARK.md](BENCHMARK.md#dropped-frames-norilight-counted-drops-moonlight-620-doesnt) |
| N9 Settings during a stream | Host settings that need no restart apply at once, in running streams too; a client changes its stream's bitrate, error correction, encoder preset, resolution, frame rate and codec without reconnecting (`POST /api/v1/session/settings`, control message `0x3100` for a new format); Norilight's Stream settings menu (Ctrl+Alt+Shift+O). Other clients reconnect as before | [AUTOTUNE.md](AUTOTUNE.md#settings-during-a-stream-n9), [api.md](api.md#post-apiv1sessionsettings) |
| N10 Through a host update | The host tells streaming clients it restarts (update or settings restart) and names each run (`RunId`); Norilight waits and starts the same app again by itself, 0.5 s after the host answers. A real handover isn't planned: the gap is the host's own downtime | [BENCHMARK.md](BENCHMARK.md#reconnect-after-a-host-restart-n10) |
| N16 Steadier benchmark runs | Bad runs came from the test picture skipping refreshes (Tk timers) and from capture to send measured from the encoder's frame grid instead of the capture time. The picture now changes on every refresh and the host measures from the real capture time: frames shown 57.8–59.9 instead of 38.6–58.7, capture to send 3.9–4.2 ms instead of 2.0–4.4. That showed two real differences, N18 and N19 | [BENCHMARK.md](BENCHMARK.md#steadier-runs-n16) |
| N18 Capture to send against Vibepollo | Measured, not a Norishine slowdown (25 November 2026). Capture and the hand-off to the encoder take 0.35 ms; the rest is the encode, 3.5 or 3.9 ms depending on where in the refresh it starts, with every encoder setting tried. Vibepollo starts its clock when its host takes the frame, not at capture, and its WGC helper's frames reach the encoder where it's fastest (3.1 ms); with Desktop Duplication it reports 3.6–3.7 ms. Norishine's own WGC capture measures the same as Desktop Duplication, so the capture helper wouldn't lower it. The host log shows each stage at debug level. Follow-up: N20 | [BENCHMARK.md](BENCHMARK.md#capture-to-send-against-vibepollo-n18) |
| N2 Benchmark check | `scripts/benchmark/compare.py`: Norishine with Norilight against stored Apollo, Vibepollo and Sunshine results with Moonlight, per metric better, same or worse; run after every streaming change | [BENCHMARK.md](BENCHMARK.md#check-against-apollo-vibepollo-and-sunshine) |

## In progress: M10 measured improvements

Each one benchmarked before and after ([BENCHMARK.md](BENCHMARK.md)).

- Error correction at stream start (**done**): start-up key frame requests no longer raise
  FEC (average over the first minute 24.6 % to 20 %).
- Display restore after crashes (**done**): the service and the uninstaller undo the display
  changes of a host that had to be killed ([DISPLAY-RESTORE.md](DISPLAY-RESTORE.md)).
- AMF options (**done**, nothing changed): every option within 0.5 ms of the others; the
  defaults stay ([BENCHMARK.md](BENCHMARK.md#m10-amf-options)).
- Frame cap matched to the client (**measured, not built**): the host already encodes no
  more frames than its display shows; a cap would undo the clients' V-sync-off for high
  frame rates ([BENCHMARK.md](BENCHMARK.md#m10-frame-cap-matched-to-the-client)).
- Capture from a user-session helper (**designed, not built**): it wouldn't make streams
  faster. With the steadier check, `capture = wgc` measures the same as Desktop Duplication
  (N18, [CAPTURE-HELPER.md](CAPTURE-HELPER.md)); its case is what WGC captures that Desktop
  Duplication can't.

## Next

Ranked by importance against effort: cheap, important items first; large items, and items
whose value depends on earlier ones, later. Effort: **S** days, **M** one to two weeks,
**L** several weeks, **XL** a month or more. Norilight-only items are also in
[Norilight's roadmap](https://github.com/Nori-Oppi/Norilight/blob/main/docs/ROADMAP.md);
they're listed here so there's one order for both.

| # | Item | Project | Effort | Importance | Needs |
| --- | --- | --- | --- | --- | --- |
| N19 | First video packet as early as Apollo's and Sunshine's | Both | S–M | Medium | — |
| N7 | Linux build | Norilight | M | Medium | — |
| N8 | Separate command-line builds | Both | M | Medium | — |
| N11 | Linux build | Norishine | XL | Medium | — |
| N12 | Android build, for handhelds | Norilight | XL | Medium–high | — |
| N13 | iOS build | Norilight | XL | Low | N12 |

Small fixes found by the benchmark check (N2). They aren't regressions, so they can be done
whenever there's room, between the items above:

| # | Item | Project | Effort | Importance |
| --- | --- | --- | --- | --- |
| N15 | Benchmark streams that show no picture | Norishine (check script) | S | Low |
| N17 | Virtual display follows a resolution change during a stream | Norishine | S–M | Low |
| N20 | Encode at a quicker point of the refresh | Norishine | M | Low |

### N7. Linux build (Norilight) — M
Moonlight's Linux build code is kept, so this is mostly CI, an AppImage or Flatpak, and
checking Norilight's own features there (several stream windows, monitor targeting,
clipboard; the screenshot pass-through stays Windows-only).

### N8. Separate command-line builds — M
`norilight-cli`: pair, list, stream and quit without the Qt GUI, for scripts and headless
machines. A Norishine CLI: status, settings, apps, devices and updates through `/api/v1`,
with a paired certificate or the web sign-in. The benchmark check (N2) can drive both.

### N11. Linux build (Norishine) — XL
Sunshine's Linux capture (KMS, wlroots, X11) and encoders are still in the tree, but none of
Norishine's additions have been built there: state store, web sign-in, adaptive bitrate,
stats, recommendations, updates. Linux CI first, then packages (Flatpak, deb or AppImage)
and a systemd service. Virtual displays and display restore stay Windows-only at first.

### N12. Android build, for handhelds (Norilight) — XL
Moonlight PC doesn't target Android. Two ways: port Norilight with Qt for Android (one
codebase, but decoding, input and controllers need new work), or start from Artemis, the
Android client whose Apollo features Norilight follows, and add Norilight's features
(profiles, tuning reports, host management). A short spike decides; Artemis is likely the
faster route. For handhelds (Retroid, AYN Odin, Ayaneo): controller-first navigation,
profiles matched to the built-in screen, and a battery-aware tuning goal.

### N13. iOS build (Norilight) — XL
Last: another separate codebase (Moonlight iOS or Qt for iOS), and unsigned nightlies can't
be installed on iOS without a developer account or sideloading. Done after Android, reusing
its decisions.

### N15. Benchmark streams that show no picture (check script) — S
About one stream in eleven in the benchmark check shows no picture: the client recreates its
renderer just as the script moves the stream window, and the window stays one flat colour. It
happens with every host and client alike and the retry always works, but it makes runs slower
and less predictable. Change how the script places the window (for example wait for the first
frame, or start the client on the target position) so streams don't need a retry. See
[BENCHMARK.md](BENCHMARK.md#streams-that-show-no-picture).

### N19. First video packet as early as Apollo's and Sunshine's — S–M
Norishine with Norilight receives the first video packet about 120 ms later than Apollo or
Sunshine with Moonlight (1590 against 1470 ms), right at the check's 150 ms tolerance for time to
first frame. Crossing hosts and clients, as for the dropped frames in
[BENCHMARK.md](BENCHMARK.md#dropped-frames-norilight-counted-drops-moonlight-620-doesnt), shows
which side it is; then the start steps both log (N5) show where.

### N17. Virtual display follows a resolution change — S–M
A resolution changed during a stream (N9) scales the captured display. A virtual display made
for the stream keeps the size it was created with; changing its mode too would give a sharp
picture at the new size.

### N20. Encode at a quicker point of the refresh — M
The same frame takes the AMD encoder 3.5 ms when the encode starts 8–12 ms after the vertical
blank and 4.0–4.4 ms when it overlaps the next refresh (N18). Desktop Duplication delivers
frames wherever the compositor puts them, so each stream lands somewhere else. Find what the
encoder competes with near the refresh, and whether frames can reach it earlier without waiting.
Until then the check reports capture to send worse than Vibepollo
([BENCHMARK.md](BENCHMARK.md#capture-to-send-against-vibepollo-n18)).

## Not planned
WebRTC browser streaming, PyroWave, Playnite and Lutris integration, custom gamepad
drivers. Revisit only with a concrete need.

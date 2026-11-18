# Norishine architecture decisions and roadmap

Status key: **done**, **in progress**, **planned**. Updated 2026-11-18: phases 2–5 are done, and the next
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

Phases 2 to 5 are finished, and so are N1 to N4. Details are in the linked docs and the git history.

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
- Capture from a user-session helper (**designed, not built**): WGC measured 0.6 ms lower,
  but proving it needs a client-side latency figure ([CAPTURE-HELPER.md](CAPTURE-HELPER.md)).

## Next

Ranked by importance against effort: cheap, important items first; large items, and items
whose value depends on earlier ones, later. Effort: **S** days, **M** one to two weeks,
**L** several weeks, **XL** a month or more. Norilight-only items are also in
[Norilight's roadmap](https://github.com/Nori-Oppi/Norilight/blob/main/docs/ROADMAP.md);
they're listed here so there's one order for both.

| # | Item | Project | Effort | Importance | Needs |
| --- | --- | --- | --- | --- | --- |
| N5 | Near-instant stream start | Both | L | Very high | — |
| N6 | Auto-optimisation wizard | Both | M | High | — |
| N7 | Linux build | Norilight | M | Medium | — |
| N8 | Separate command-line builds | Both | M | Medium | — |
| N9 | Change settings during a stream | Both | L | High | N5 |
| N10 | Update the host without losing the stream | Both | L | Medium | N5, N9 |
| N11 | Linux build | Norishine | XL | Medium | — |
| N12 | Android build, for handhelds | Norilight | XL | Medium–high | — |
| N13 | iOS build | Norilight | XL | Low | N12 |

Small fixes found by the benchmark check (N2). They aren't regressions, so they can be done
whenever there's room, between the items above:

| # | Item | Project | Effort | Importance |
| --- | --- | --- | --- | --- |
| N14 | Fewer full-queue frame drops in the pacer | Norilight | S–M | Medium |
| N15 | Benchmark streams that show no picture | Norishine (check script) | S | Low |

### N5. Near-instant stream start — L
Measure each step from choosing an app to the first frame (launch request, display change
or virtual display, encoder setup, RTSP handshake, first key frame, decoder start), then
cut the slow ones. Likely candidates: reuse the previous stream's encoder probe instead of
probing again, skip display changes that are already in place, keep the last session's
virtual display for a while, and run independent steps at the same time. The target is set
after measuring; resuming a running app should take well under a second.

### N6. Auto-optimisation wizard — M
One button in Norilight (and the web UI): short test streams per candidate setting (codec,
resolution, frame rate, bitrate, encoder preset), measured with M8's stats, and the best
saved as the profile's and the host's settings, with Undo. Builds on M9's tuning goals and
recommendations, which today only adjust after real streams.

### N7. Linux build (Norilight) — M
Moonlight's Linux build code is kept, so this is mostly CI, an AppImage or Flatpak, and
checking Norilight's own features there (several stream windows, monitor targeting,
clipboard; the screenshot pass-through stays Windows-only).

### N8. Separate command-line builds — M
`norilight-cli`: pair, list, stream and quit without the Qt GUI, for scripts and headless
machines. A Norishine CLI: status, settings, apps, devices and updates through `/api/v1`,
with a paired certificate or the web sign-in. The benchmark check (N2) can drive both.

### N9. Change settings during a stream — L
Without reconnecting, in steps: host settings that don't need a restart apply at once;
bitrate, FEC and encoder preset change live (the adaptive controller already rebuilds the
encoder for bitrate, see [AUTOTUNE.md](AUTOTUNE.md)); then resolution, frame rate and codec
through a new Norishine control message that rebuilds encoder and decoder behind one key
frame. Additive only (D4): Norilight with Norishine; other clients reconnect as they do now.

### N10. Update the host without losing the stream — L
An update replaces the host process, so streams end now. First step: Norilight waits for
the host to come back and reconnects to the same app by itself, which N5 makes quick. A
real handover (the new process taking over the running session) is only looked at after
that, if the reconnect is still noticeable.

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

### N14. Fewer full-queue frame drops in the pacer (Norilight) — S–M
The client's frame pacer throws away frames that arrive while its queue is already full:
about 0.15–0.3 % of frames, a few a minute. Moonlight 6.2.0 drops the same frames, so this
isn't a regression, but fewer drops would put Norilight ahead of Moonlight. Find why the queue
is full when a frame arrives (decode or present timing, V-sync off with the Performance goal,
queue depth) and reduce it; prove it with the benchmark check (no other metric worse). See
[BENCHMARK.md](BENCHMARK.md), the section on the extra frame drops.

### N15. Benchmark streams that show no picture (check script) — S
About one stream in eleven in the benchmark check shows no picture: the client recreates its
renderer just as the script moves the stream window, and the window stays one flat colour. It
happens with every host and client alike and the retry always works, but it makes runs slower
and less predictable. Change how the script places the window (for example wait for the first
frame, or start the client on the target position) so streams don't need a retry. See
[BENCHMARK.md](BENCHMARK.md#streams-that-show-no-picture).

## Not planned
WebRTC browser streaming, PyroWave, Playnite and Lutris integration, custom gamepad
drivers. Revisit only with a concrete need.

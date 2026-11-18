# Norishine architecture decisions and roadmap

Status key: **done**, **in progress**, **planned**. Updated 2026-11-18. Evidence for the
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
deliberately broken, but is not verified until CI exists for it.

## Roadmap

### Phase 2: establish the baseline
- **M0.1** Merge Apollo history (**done**); rename project identifiers and branding
  without changing file formats or ports (**done**): service `NorishineService`,
  install and config folder `Norishine`, firewall rule, logo and tray icons, web UI
  text, default mDNS name. Kept: `APOLLO_*` app environment variables (so existing app
  scripts work) and the Linux app ID `dev.lizardbyte.app.Sunshine`.
- **M0.2** Clean Windows build (**done**; see `docs/DEVELOPMENT.md`); installer builds
  locally and in CI and keeps the SudoVDA and ViGEmBus drivers shared with Apollo and
  Vibepollo. Replacing Vibepollo tested on one machine. **Deferred** until after M1:
  fresh install without Apollo or Vibepollo, the keep-both path (port 48989), and going
  back (uninstall Norishine, reinstall Vibepollo, settings carried back).
  Needed two fixes to Apollo itself and a newer FFmpeg for AMD encoders
  (`docs/UPSTREAM-SYNC.md`).
- **M0.3** Unit tests build and run, all pass (**done**); CI builds on Windows and runs
  the tests (`.github/workflows/ci.yml`, **done**). Fixing the stale tests found one real
  Apollo bug: display mode remapping by FPS never matched with the automatic refresh
  rate option.
- **M0.4** Port Sunshine security fixes, each with a test. **Done:** control packet
  parsing, input packet validation, exact client certificate match, pairing request IDs
  (adapted to OTP), log redaction, bounded hex parsing, duplicate client records, audio
  and video UDP endpoints, URL opening, PATH writing. CSRF tokens follow in M3.
- **M0.5** Smoke test streaming: Norilight pairs and streams the Desktop app (HEVC on
  AMD AMF, D3D11VA decode) from a development host (**done**, loopback only), and from
  an installed host over the network (**done**). Stock Moonlight: **deferred** until
  after M1.
- **M0.6** Permission defaults (**done**): the first paired device gets launch, view and
  input only (was every permission); clipboard and server commands are always granted
  explicitly. Checked end to end, with virtual displays, by `scripts/e2e/host_check.py`.

### Phase 3: settings and UI
- **M1 (first feature milestone)** Host-owned settings schema with server-side
  validation (**done**): schema table in C++ (`src/config_schema.cpp`) covering all 103
  keys config.cpp reads (the 104th, `dd_wa_hdr_toggle`, is dead and left out),
  `GET /api/v1/config/schema`, `GET /api/v1/config` (typed values, unknown and invalid
  entries), `PATCH /api/v1/config` (validated partial updates with per-key errors,
  rewriting only the changed lines), atomic writes with a `.bak` copy for the config,
  state, credentials and apps files, and tests for all of it. config.cpp still parses the
  file itself; a test keeps its keys and the schema identical. No visible UI change yet;
  this is what both the new web UI and Norilight build on.
- **M2** New settings UI on the schema (**done**): the settings page is rebuilt from
  `GET /api/v1/config/schema` and saves with `PATCH /api/v1/config`
  (`src_assets/common/assets/web/settings/`), on a shared Norilight-matched theme and
  header (`web/ui/`, light and dark). Categories reworked into General, Display, Video,
  Audio, Input, Network and pairing, Apps and commands, the encoders and Files. Search
  covers every setting (titles, help and value names); Basic / Advanced switch;
  confirmation before saving dangerous settings; per-key errors from the host shown at
  each setting with a summary; a restart notice with a restart button; reset to default
  per setting; settings that depend on another are indented and say when they have no
  effect. Real editors for commands, key mappings and display mode remapping. The schema
  gained optional `depends_on`, `value_titles` and category groups (additive); every
  setting now has English help text and every choice a readable name (tested), and the
  dead `dd_wa_hdr_toggle` strings are gone. Other pages keep their layout until M3; they
  share the new header colours. Pages stay separate HTML entries rather than one
  vue-router app, since the host serves each page at its own path.
- **M3** Remaining pages, login hardening and CSRF (**done**). Every page now uses the
  M2 theme and shell, and Bootstrap is gone from the web UI: home with host status, the
  running app and active streams (device, address, resolution, frame rate, codec, HDR,
  bitrate) with close and disconnect (`GET /api/v1/status`, now also with `sessions` for
  paired clients); pairing by PIN from the client or one-time PIN from the host; a new
  Devices page with permissions as plain choices (streaming level, input, extras, host
  management), per-device display mode and commands; apps with an editor, cover search,
  reordering, launch and close; troubleshooting with a live, filterable log and the
  close / restart / display-reset / quit actions; account page. Sign-in: several
  sessions at once, HttpOnly session cookie, CSRF token required on every change (and a
  matching Origin), sessions that survive a restart of the host, per-address lockout
  after failed sign-ins, PBKDF2 password hashes
  (old ones converted at the next sign-in), sign out, and no more passwords kept in the
  browser (`src/web_auth.*`, tested).
- **M4** State file split and migrations; import from Apollo (**done**). Import from an
  Apollo or Vibepollo install, and carrying settings back on uninstall, were done first
  (`src_assets/windows/misc/migration/other-hosts.ps1`). The state now has versions and
  one-way migrations (`src/state_store.*`): version 1 splits `sunshine_state.json` into
  `state/host.json` (unique ID), `state/pairing.json` (paired devices) and
  `state/credentials.json` (web UI account) on first start, keeping the old file as
  `sunshine_state.json.v0.bak`; settings stay in `sunshine.conf`. An unreadable file is
  never overwritten (nothing is saved that run), an interrupted migration completes on
  the next start, files from a newer version are used as they are, and a separate
  `credentials_file` stays where it is. Unique ID, pairings and the account carry over;
  tested from Apollo-format files and on a dev host. The uninstaller's carry-back rebuilds
  an Apollo-readable `sunshine_state.json`. Password hashes moved to PBKDF2 in M3.

### Phase 4: Nori integration
- **M5** Capability element in serverinfo; client-certificate authentication for
  `/api/v1`; management permission bits and the UI to grant them (**done**: see
  `docs/api.md`, "Management API for paired clients").
- **M6** Norilight: host status and read-only settings view, then editing (**done**:
  readable titles, help text and categories in the schema with `?locale=`,
  `POST /api/v1/restart`, and a Host Settings page in Norilight that shows the status,
  edits the settings with per-key errors and confirmation for dangerous ones, and
  restarts the host; settings with lists or scripts stay read-only there).
- **M7** Apps, clients, displays and active sessions managed from Norilight (**done**):
  `/api/v1` calls for apps (list, add, change, remove, reorder, start, close), paired
  devices (list, change, unpair, end a stream) and displays (list, reset saved changes),
  on both ports, checked end to end (`scripts/e2e/manage_check.py`); Norilight's Manage
  Host page uses them.

### Phase 5: diagnostics and optimisation
- **M8** Host stats and per-session metrics behind the API (**in progress**: the API is
  done, `GET /api/v1/stats` on both ports with CPU, memory, GPU and encoder load and, per
  stream, frame rate, bitrate, latency, send time and recovery requests; see
  `docs/api.md`). Still to do: Norilight showing it, and a repeatable benchmark procedure
  recorded before any tuning.
- **M9** Auto-tuning ([AUTOTUNE.md](AUTOTUNE.md)): the best settings for each display, network
  and machine, kept up to date while streaming. **In progress.**
  - **M9.1** Live adaptation on the host (**done**): each stream's bitrate and error correction
    follow the network (`src/adaptive.*`, settings `adaptive_bitrate`, `adaptive_min_bitrate`,
    `adaptive_max_fec`), for every client; `GET /api/v1/session` and
    `POST /api/v1/session/report` let a client see its own stream and report what it measures.
  - **M9.2** Norilight reports its measurements during the stream and shows what the host does
    in the performance overlay (**done**, Norilight #18).
  - **M9.3** Launch settings (**done**): a Tuning goal per Norilight profile (Performance,
    Balanced, Quality, Off) picks codec, HDR, V-sync, frame pacing and the start bitrate from
    both machines' hardware, the display, the link and earlier streams to that host on that
    network (learned per host and network); the host sets the encoder preset for the goal
    (`norishineTuning`, setting `client_tuning`). Picks apply to one stream and never change
    the profile, so there's nothing to roll back; Off turns them off.
  - **M9.4** Host encoder setting recommendations from what earlier streams measured, with
    preview and rollback.
- **M10** Measured improvements: display restore after crashes, capture from a
  user-session helper, frame cap matched to the client, AMF path evaluation.

## Not planned
WebRTC browser streaming, PyroWave, Playnite and Lutris integration, custom gamepad
drivers. Revisit only with a concrete need.

# Norishine architecture decisions and roadmap

Status key: **done**, **in progress**, **planned**. Updated 2026-11-15. Evidence for the
decisions is in [PHASE1-RESEARCH.md](PHASE1-RESEARCH.md).

## Decisions

### D1. Baseline: Apollo `master` (`adc5c5a0`), with full history

Confirmed 2026-11-15. A switch to Sunshine as the base was considered and declined
for now; it is re-evaluated at the review point below.
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
- Review point: after milestone M2, re-estimate a re-base onto Sunshine. By then the
  settings and API layers will be our own code and easier to carry across.

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
  without changing file formats or ports (**planned**). The rename must give Norishine
  its own Windows service name, install folder and config folder: Apollo and Vibepollo
  both use `ApolloService`. Preserve licence, NOTICE and attribution.
- **M0.2** Clean Windows build (**done**; see `docs/DEVELOPMENT.md`); installer (**planned**).
  Needed two fixes to Apollo itself and a newer FFmpeg for AMD encoders
  (`docs/UPSTREAM-SYNC.md`).
- **M0.3** Unit tests build and run: 195 of 231 pass (**done**); fix the 33 stale tests
  listed in `docs/KNOWN-ISSUES.md` and add CI for Windows build and tests (**planned**).
- **M0.4** Port Sunshine security fixes, each with a test. **Done:** control packet
  parsing, input packet validation, exact client certificate match, pairing request IDs
  (adapted to OTP), log redaction. **Next:** bounded hex parsing and the remaining
  advisories in `docs/UPSTREAM-SYNC.md`.
- **M0.5** Smoke test streaming: Norilight pairs and streams the Desktop app (HEVC on
  AMD AMF, D3D11VA decode) from a development host (**done**, loopback only); stock
  Moonlight and a second machine (**planned**).

### Phase 3: settings and UI
- **M1 (first feature milestone)** Host-owned settings schema with server-side
  validation: schema table in C++ covering all 104 keys, `GET /api/v1/config/schema`,
  validated partial updates with per-key errors, atomic state writes, tests for all
  of it. No visible UI change yet; this is what both the new web UI and Norilight build on.
- **M2** New settings UI on the schema: Norilight-matched theme, categories reworked,
  full-text search, Basic / Advanced, confirmation for dangerous settings,
  restart-required indication. Remove dead settings (`dd_wa_hdr_toggle`), fix
  missing strings, regroup misplaced ones.
- **M3** Remaining pages: apps, clients and permissions, pairing, status and active
  sessions, logs and troubleshooting. Login hardening and CSRF.
- **M4** State file split and migrations; import from Apollo.

### Phase 4: Nori integration
- **M5** Capability element in serverinfo; client-certificate authentication for
  `/api/v1`; management permission bits and the UI to grant them.
- **M6** Norilight: host status and read-only settings view, then editing.
- **M7** Apps, clients, displays and active sessions managed from Norilight.

### Phase 5: diagnostics and optimisation
- **M8** Host stats and per-session metrics behind the API; repeatable benchmark
  procedure recorded before any tuning.
- **M9** Wizard, first version: capability discovery on both sides and conservative
  recommendations with preview and rollback.
- **M10** Measured improvements: display restore after crashes, capture from a
  user-session helper, frame cap matched to the client, AMF path evaluation.

## Not planned
WebRTC browser streaming, PyroWave, Playnite and Lutris integration, custom gamepad
drivers. Revisit only with a concrete need.

# Phase 1 research: upstream comparison and baseline

Status: research complete, nothing built yet. Snapshot date 2026-11-15.

How to read this: items marked **[checked]** were confirmed directly with git or by
reading the cited source during this phase. Everything else comes from a structured
read of the source by research passes and has file references, but was not
independently re-checked. Nothing was compiled or run.

## 1. The three hosts at a glance

| | Sunshine | Apollo | Vibepollo |
|---|---|---|---|
| Repository | LizardByte/Sunshine | ClassicOldSong/Apollo | Nonary/Vibepollo (GitHub fork of Apollo) |
| Head inspected | `43b74505` 2026-11-15 | `adc5c5a0` 2026-05-21 | `5e6ed859` 2026-11-02 |
| Licence | GPL-3.0 | GPL-3.0 | GPL-3.0 |
| C++ in `src/` **[checked]** | 78k lines, 157 files | 53k lines, 139 files | 237k lines, 644 files |
| Web UI source **[checked]** | 11k lines, 48 files | 5.6k lines, 39 files | 58k lines, 172 files (two frontends) |
| Test files **[checked]** | 105 | 24 | 484 |
| Activity **[checked]** | daily; 958 commits since Apollo's last sync | 18 commits in 12 months; last release tag v0.4.8 (2025-09-27) | 337 to 567 commits a month Aug to Oct 2026, one main author |
| Last Sunshine sync **[checked]** | n/a | `1a96d135`, 2025-09-26 | same merge base, plus one squashed import from May 2026 |
| CI in repo | yes | none | yes |

Relationships **[checked]**: Apollo is 457 commits ahead of and 958 behind Sunshine.
Vibepollo is 2,642 commits ahead of Apollo and 22 behind it.

## 2. What each one brings

### Apollo over Sunshine
Apollo's own delta is about 14.5k changed lines in 172 files. The parts that matter:

- **Virtual display (Windows, SudoVDA)**: `src/platform/windows/virtual_display.cpp`,
  lifecycle in `src/process.cpp`. Created per client or per app, matched to the
  client's resolution and refresh rate, with a scale factor.
- **Per-client permissions**: bitmask in `src/crypto.h` (input classes, clipboard,
  server commands, list/view/launch), enforced in nvhttp, the control stream and input.
- **OTP pairing**, **per-client overrides** (display mode, do/undo commands, always
  use virtual display), **server commands** (control message `0x3000`), **clipboard
  sync** over HTTPS `/actions/clipboard` (Windows only in practice).
- Cookie login for the web UI instead of HTTP Basic auth.
- How a client recognises Apollo: extra serverinfo XML tags (`Permission`,
  `VirtualDisplayCapable`, `VirtualDisplayDriverReady`, `ServerCommand`), extra
  launch parameters (`virtualDisplay`, `scaleFactor`, `appuuid`). There is no
  dedicated capability or version field.

Weak points: no CI, no tests for any Apollo-specific feature, non-atomic writes to
`sunshine_state.json` (pairing data and web credentials in one file), single-round
salted SHA-256 for the admin password, no login rate limit, one global web session,
config saved without validation, and permissive permission defaults (first paired
device and all legacy devices get every permission).

### Sunshine since Apollo stopped syncing
- **Security fixes Apollo lacks.** Three confirmed in Apollo's source **[checked]**:
  - `src/stream.cpp:595` reads the control packet type with no length check
    (Sunshine fix `82bccdf6`).
  - `src/nvhttp.cpp:856` applies an entered PIN to whichever pending pairing session
    is first in the map (Sunshine fix `ccf97e38` adds per-request pairing IDs).
  - `src/crypto.cpp` accepts any client certificate that verifies with a partial
    chain; Sunshine now also requires an exact certificate match (`4d768847`).
  - Also reported: input packets cast without size validation (`1583e7c4`), plus
    about eight smaller hardening commits and token-based CSRF protection.
- Windows capture and encoder fixes (WGC 60 fps cap, NVENC split encode and dynamic
  SDK selection, video re-init crashes, HDR/SDR fallback, audio sink selection).
- New options: `max_stream_resolution`, `bind_address`, `dscp_tagging`,
  `packetsize`, `nvenc_split_encode`.
- Web UI rewritten as a single-page app with unit tests; build moved to CPM with a
  dependency lock file; much larger C++ test suite.
- libvirtualhid for extra gamepad types depends on a separately licensed broker on
  Windows, so it should stay optional.

### Vibepollo over Apollo
About 141k lines in 507 files that do not exist in Apollo. Its README states that
roughly 99% of the code is AI-generated.

| Area | Size | Verdict | Reason |
|---|---|---|---|
| Display helper, layout snapshot and restore, own virtual display driver | ~48k | Reimplement the ideas | Depends on forked libdisplaydevice and Nonary's own driver |
| WGC capture from a helper process in the user session | small | Reimplement | Useful for running as a service; several of its perf changes were reverted |
| Native AMF encoder (`src/amf/`) | 4.6k | Evaluate later | Self-contained, integration not yet reviewed |
| Session history, host stats, hardware metrics | 6.4k | Reimplement small | Wanted for diagnostics and the wizard, but behind our own API |
| Login rate limiter, session tokens, atomic state writes with backup | <4k | Adopt | Small, self-contained, fixes real Apollo weaknesses |
| API tokens with path-regex scopes | part of auth | Do not adopt | A narrowly scoped token can create an unrestricted one (`confighttp.cpp:5937`, `http_auth.cpp:244`) |
| Schema-driven settings page with Basic view, search, validation | 1.4k schema | Adopt the design | Schema lives only in the frontend and duplicates C++ defaults |
| RTSS / frame limiter / frame generation hooks | ~9.6k | Reimplement only the frame cap | Rest is niche |
| WebRTC browser streaming | 6.8k + libwebrtc | Skip | Multi-hour dependency build; a native client does not need it |
| PyroWave codec | 5.5k + vendored | Skip for now | Needs matching client code; LAN only |
| Playnite / Steam / Lutris library sync | ~13.6k | Skip (maybe slim Steam sync later) | Deeply threaded through `process.cpp` and `nvhttp.cpp` |
| Custom gamepad drivers, DualSense USB/IP | ~8k + drivers | Skip | Requires Nonary's driver binaries and a protocol fork |

Why not base on Vibepollo: 4.5 times the size, churn of hundreds of commits a month
with frequent reverts, seven Nonary-owned dependency forks, a squashed Sunshine
import that removes the merge base for future upstream merges, and core files that
have grown to 7k to 8k lines each.

## 3. Feature matrix

| Capability | Sunshine | Apollo | Vibepollo | Norishine plan |
|---|---|---|---|---|
| Standard Moonlight streaming | yes | yes | yes | keep, never break |
| Virtual display matched to client | no | yes (SudoVDA) | yes (own driver + SudoVDA) | keep Apollo's |
| Per-client permissions | enable/disable only | yes | yes | keep, tighten defaults |
| OTP pairing | no | yes | yes | keep, harden |
| Clipboard sync, server commands | no | yes | yes | keep |
| Pairing request IDs, cert pinning, packet validation | yes | **no** | partial | port from Sunshine first |
| CSRF protection on web API | yes | no (SameSite cookie only) | partial | add |
| Login rate limiting | no | no | yes | add |
| Atomic state writes with backup | no | no | yes | add |
| Config validated on save | no | no | frontend only | server-side, from schema |
| Settings search | key names only | no | full text | full text |
| Basic / Advanced modes | no | no | yes | yes |
| Host-owned settings schema | no | no | no | **new** |
| Versioned, scoped management API | no | no | unversioned, flawed scopes | **new** |
| Capability negotiation field | no | no | no | **new**, optional |
| Live session and host stats | no | no | yes | reimplement small |
| Display layout restore after crash | partial | partial | yes | reimplement |
| Browser (WebRTC) streaming | no | no | yes | no |
| Web UI tests | yes | no | yes | yes |

## 4. Norilight today

- Already speaks Apollo's extensions: reads the virtual display and permission tags,
  sends `virtualDisplay` and `scaleFactor`, syncs clipboard text, and runs server
  commands through a one-commit fork of moonlight-common-c (`LiSendExecServerCmd`).
- Apollo clipboard support is guessed from an empty HTTP body, which is fragile; a
  real capability field would replace that.
- Every HTTPS request already presents the paired client certificate and pins the
  host certificate. Apollo already maps that certificate to a paired device with
  permission bits, so a management API can authenticate Norilight with no password.
- Has what a wizard needs on the client side: decoder availability per codec
  (H.264, HEVC, AV1, 10-bit, 4:4:4), HDR support, per-monitor resolution and refresh
  rate, and in-stream statistics. Missing: any stored measurements and any
  pre-stream bandwidth or latency test.
- Visual identity to mirror in the host UI (`app/gui/Theme.qml`): dark surfaces
  `#28282D` / `#2F2F34` / `#37373E`, accent `#F2A7BE`, primary `#AD4F70`,
  text `#F4F4F6` / `#ECECEE` / `#A3A3AB`, status `#5FCB82` / `#E0B050` /
  `#E57373`, 6px card radius, platform default font, settings laid out as a 256px
  sidebar with grouped sections and always-visible descriptions.
- Local state: `main` is 1 ahead and 1 behind `origin/main`; a second worktree
  exists at `C:\Users\Alice\Norilight-merge-all-20261115`. Not touched.

## 5. Dependencies and build

- Apollo builds on Windows only with MSYS2 UCRT64 (mingw gcc), CMake and Ninja; the
  web UI builds with Node and Vite during the CMake build.
- Submodules: 16, of which two are ClassicOldSong forks (moonlight-common-c,
  Simple-Web-Server); SudoVDA headers are vendored. Runtime needs the SudoVDA driver
  for virtual displays and ViGEmBus for gamepads.
- This machine **[checked]**: MSYS2 at `C:\msys64` with gcc 16.2, cmake 4.4, ninja,
  openssl; official Node 26; `gh` logged in. Missing MSYS2 packages: cppwinrt,
  curl-winssl, MinHook, miniupnpc, nsis, onevpl, opus, nlohmann_json (boost optional).
- Build risk: Apollo's code dates from September 2025 and has not been compiled here
  with gcc 16 and CMake 4.4; expect some fixes.

## 6. Risks

1. **We become Apollo's maintainers.** Apollo is close to dormant, so security and
   driver fixes will only arrive if we port them from Sunshine.
2. **Drift from Sunshine.** 958 commits and growing, with large mechanical refactors
   upstream that break patch context. The longer a re-base is postponed, the more it costs.
3. **Remote management is remote code execution by design** (prep commands, server
   commands, app definitions). The management permission must be explicit, off by
   default, and separate from streaming permissions.
4. **Existing installs.** Config, `apps.json`, and pairing state must migrate from
   Apollo and Vibepollo layouts without loss; Vibepollo's state format has diverged.
5. **SudoVDA** is a third-party driver we do not control.
6. **Toolchain.** Single supported Windows compiler, currently a very new gcc.
7. **Scope.** The Vibepollo feature list is large; adopting more than a few items
   recreates the problem this project is meant to avoid.

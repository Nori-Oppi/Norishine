# Development

## Windows build

Requirements: MSYS2 (UCRT64), official Node.js on `PATH`, git.

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,boost,cppwinrt,curl,MinHook,miniupnpc,nsis,onevpl,openssl,opus,nlohmann-json}
git submodule update --init --recursive
cmake -B build -G Ninja -S . -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

Installer (NSIS), written to `build/cpack_artifacts/Norishine.exe`; CI builds it too:

```bash
cd build && cpack -G NSIS --config ./CPackConfig.cmake
```

Configure also downloads `nefconc.exe` (nefcon, SHA256-verified), which the installer
uses to create the SudoVDA device. `SudoVDA.dll` is committed, from the Apollo 0.4.6
release, and must match `sudovda.cat`.

Configure downloads the pinned FFmpeg package (about 140 MB, SHA256-verified) into
`build/_deps`. See `cmake/dependencies/ffmpeg.cmake`.

If configure reports FFmpeg binaries for `Windows-` (empty architecture), the shell did
not pass `PROCESSOR_ARCHITECTURE`; set it to `AMD64`.

A build folder configured before the Norishine rename keeps Apollo's publisher name
and support URL in its CMake cache. Reset them with `cmake -U 'SUNSHINE_PUBLISHER_*' build`.

## Tests

Run from `build/tests` with `C:\msys64\ucrt64\bin` on `PATH`:

```
test_sunshine.exe --gtest_filter=-MouseInputs/*:Configurations/AudioTest.*
```

All tests pass; see [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for the suites skipped locally.

## Streaming changes: the benchmark check

Norishine with Norilight must never stream worse than Apollo, Vibepollo or Sunshine with
Moonlight. After any change to capture, encoding, the network code or the client's
decoding (here or in Norilight), build a release and run the check on the real PC:

```
python scripts/benchmark/compare.py
```

Put its verdict (the table and the verdict line) in the pull request. If a metric is worse
than a reference, fix it before merging, or explain in the pull request why it's
acceptable. The check needs a GPU and a display, so CI can't run it. See
[BENCHMARK.md](BENCHMARK.md#check-against-apollo-vibepollo-and-sunshine).

## CI

`.github/workflows/ci.yml` runs on pull requests and pushes to `master`. A first job
sorts the changed files:

| Changed | Runs |
| --- | --- |
| Only docs (`docs/`, `*.md`, licence and translation config) | nothing else |
| Web UI only (`src_assets/common/assets/web/`, `package.json`, `vite.config.js`) | web UI build on Linux (about a minute) |
| Anything else, `docs/configuration.md` or the locale files (the tests read them) | Windows build, tests and installer, plus the web UI build |

Manual runs and the nightly always build everything. `[skip ci]` in a commit message
skips the run entirely.

The Windows build uses ccache. A pull request starts from its own last cache, otherwise
from `master`'s, so a warm build takes about 5 minutes instead of about 20. The version
and commit are compiled only into `version.cpp` (generated from `src/version.cpp.in`):
as compile definitions they would change every file on each commit and nothing could be
reused. ccache runs in depend mode, so a cache miss costs no extra preprocessor run.
Failed tests and cache statistics are in the run's summary. Installers and test results
are kept 14 days.

Measured in November 2026 (whole CI run):

| Change | Before | After |
| --- | --- | --- |
| Code, cache warm | ~21 min (15–25) | ~4 min |
| Code, no cache (first run, compiler update) | ~21 min | ~18 min |
| Web UI only | ~21 min | ~30 s |
| Docs only | ~21 min | ~10 s |

## Nightly builds

`.github/workflows/nightly.yml` runs daily at 20:17 Amsterdam time. When `master` has
new commits since the last nightly (other than documentation alone), it builds, tests and packages the installer through
`ci.yml` with a version such as `nightly-20261116-be295d3`, then replaces the rolling
`nightly` pre-release with `Norishine-Windows-x64-<version>-Setup.exe`. Start it by hand
from the Actions tab (Nightly, Run workflow) to build even when nothing changed. Runs
from other branches build but don't publish.

## Running a development host next to an installed one

Do not install development builds as a service. Run `build/sunshine.exe` with its own
config file, for example `build/dev/sunshine.conf`:

```
sunshine_name = Norishine-dev
port = 49989
file_apps = <build>/dev/apps.json
file_state = <build>/dev/sunshine_state.json
credentials_file = <build>/dev/sunshine_state.json
log_path = <build>/dev/sunshine.log
pkey = <build>/dev/credentials/cakey.pem
cert = <build>/dev/credentials/cacert.pem
upnp = off
```

The host keeps its state in a `state` folder next to `file_state` (here `<build>/dev/state/`:
`host.json`, `pairing.json`, `credentials.json`); an old single state file there is moved in
on start and kept as `sunshine_state.json.v0.bak`.

`port = 49989` moves every port away from the defaults (47984-48010), so an installed
Apollo, Vibepollo or Sunshine keeps working. Create web UI credentials with
`sunshine.exe <conf> --creds <user> <password>` and copy
`src_assets/windows/assets/apps.json` into the dev folder.

## End-to-end host check

`scripts/e2e/host_check.py` acts as GameStream clients against a running dev host: it
pairs through the web UI pairing API (including two requests at once), checks the
permission defaults and that granting a permission takes effect, and launches Desktop
with a virtual display and checks that a monitor is added and removed again. It unpairs
every client first, so use a separate dev host for it (for example `port = 49889` with
its own state files), never an installed one.

```
python -m venv .venv && .venv/Scripts/pip install cryptography
.venv/Scripts/python scripts/e2e/host_check.py 127.0.0.1 49889 <webuser> <webpass>
```

To test with Norilight without touching its installed settings, copy a Norilight build
folder elsewhere and create an empty `portable.dat` in it; settings then live in that
folder. Add the host as `127.0.0.1:49989`.

## Branches

Branch review: **18 November 2026**. All of these are included in `master`; later work
(M8 to M10) went straight into `master` through pull requests.

| Branch | Work |
| --- | --- |
| `security/remaining-advisories` | Remaining Sunshine security ports and persistent host identity |
| `installer/other-hosts` | Replace/coexist installer, shared-driver handling, configuration copies and PATH cleanup |
| `m1/settings-schema` | Settings schema, validated configuration API, atomic writes and backups |
| `m5/client-management` | Capability discovery, paired-certificate management API and per-device grants |
| `m6/host-strings` | Readable schema labels and help, paired-client restart endpoint |
| `ci/nightly` | Rolling Windows nightly installer built through CI |
| `phase3/m2-settings-ui` | Schema-driven settings page and shared visual theme |
| `phase3/m3` | Remaining web pages, active-stream status, sign-in hardening and CSRF |
| `phase3/m4` | Split state files with versioned migrations; sign-ins that survive a restart |
| `phase3/m7-manage-api` | Apps, paired devices, streams and displays in the management API |

Clone with submodules (`git clone --recursive`). The [general build notes](building.md)
keep the upstream platform instructions; [initial research](PHASE1-RESEARCH.md) explains
the starting point.

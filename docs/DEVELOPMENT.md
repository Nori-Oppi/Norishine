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

## Nightly builds

`.github/workflows/nightly.yml` runs daily at 20:17 Amsterdam time. When `master` has
new commits since the last nightly, it builds, tests and packages the installer through
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

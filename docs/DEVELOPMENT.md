# Development

## Windows build

Requirements: MSYS2 (UCRT64), official Node.js on `PATH`, git.

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,boost,cppwinrt,curl,MinHook,miniupnpc,nsis,onevpl,openssl,opus,nlohmann-json}
git submodule update --init --recursive
cmake -B build -G Ninja -S . -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

Configure downloads the pinned FFmpeg package (about 140 MB, SHA256-verified) into
`build/_deps`. See `cmake/dependencies/ffmpeg.cmake`.

If configure reports FFmpeg binaries for `Windows-` (empty architecture), the shell did
not pass `PROCESSOR_ARCHITECTURE`; set it to `AMD64`.

## Tests

Run from `build/tests` with `C:\msys64\ucrt64\bin` on `PATH`:

```
test_sunshine.exe --gtest_filter=-MouseInputs/*:Configurations/AudioTest.*
```

Known failures are listed in [KNOWN-ISSUES.md](KNOWN-ISSUES.md).

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

To test with Norilight without touching its installed settings, copy a Norilight build
folder elsewhere and create an empty `portable.dat` in it; settings then live in that
folder. Add the host as `127.0.0.1:49989`.

# Known issues

Tracked here until fixed. Last checked 2026-11-15 on Windows 11, AMD Radeon 780M.

## Failing unit tests (33 of 240)

All are tests that Apollo changed behaviour for without updating. None is a streaming failure.

- `ConfigConsistencyTest` (2): settings missing from `docs/configuration.md` or
  `en.json`, and option order differs between the web UI and the docs. To be fixed
  by the settings schema milestone (M1).
- `MdnsInstanceNameTest` (3): Apollo's default instance name is "Apollo"; the tests
  expect "Sunshine". Fixed with the rename.
- `DisplayDeviceConfigTest` (28): Apollo passes refresh rates as millihertz
  (60 Hz = 60000/1000); the tests expect Sunshine's integer form.

Not run locally because they act on the live desktop: `MouseInputs/*` (moves the
real cursor) and `Configurations/AudioTest.*` (opens real audio devices).

## Host

- Before the first client pairs, the host's unique ID is not saved, so it changes on
  every restart. A client that saw the host before pairing then refuses it ("Found
  unexpected PC"). Persist the generated ID in `load_state`.
- `QOSAddSocketToFlow() failed: 1168` warnings on loopback streams. Expected for
  127.0.0.1; check on a real network.
- Install identity is still Apollo's (service `ApolloService`, install folder, config
  folder). Vibepollo uses the same service name, so Norishine must not be installed
  until the rename (M0.1) changes these.

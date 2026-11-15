# Known issues

Tracked here until fixed. Last checked 2026-11-15 on Windows 11, AMD Radeon 780M.

## Unit tests

All 252 pass locally. Not run locally because they act on the live desktop:
`MouseInputs/*` (moves the real cursor) and `Configurations/AudioTest.*` (opens real
audio devices). CI runs the full suite.

## Host

- `QOSAddSocketToFlow() failed: 1168` warnings on loopback streams. Expected for
  127.0.0.1; check on a real network.
- Do not run the installer on a machine with Apollo or Vibepollo yet. The service,
  install folder, config folder and firewall rule are now Norishine's own, but the
  installer still (re)creates and offers to remove the SudoVDA virtual display device
  (`root\sudomaker\sudovda`), which those hosts share.

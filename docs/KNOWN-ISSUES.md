# Known issues

Tracked here until fixed. Last checked 2026-11-15 on Windows 11, AMD Radeon 780M; updated
2026-11-17.

## Unit tests

All pass locally. Not run locally because they act on the live desktop:
`MouseInputs/*` (moves the real cursor) and `Configurations/AudioTest.*` (opens real
audio devices). CI runs the full suite.

## Host

No open host issues. Fixed on 2026-11-17:

- SudoVDA displays left behind when a session hung on shutdown ("Hang detected! Session
  failed to terminate"). Norishine lists the displays it creates in
  `config\state\virtual_displays.txt`, removes them before a forced exit, and removes any
  still listed from an earlier run when it starts.
- `QOSAddSocketToFlow() failed: 1168` warnings: QoS is skipped for loopback addresses.
- `POST /api/apps/close` answered 400 when sent without a body or `Content-Type`. Calls
  without a body no longer need one, and closing the app also ends its streams at once.
- Devices paired after the first are **Watch only** by design (see
  [Devices and permissions](GUIDE.md#devices-and-permissions)); launching from them now
  answers "Permission denied. Change this device's permissions on the host's Devices
  page." and the host logs it.

## Installer next to Apollo, Vibepollo or Sunshine

When Apollo or Vibepollo is installed, the installer asks to replace it (recommended):
its settings, apps and paired devices are copied to Norishine and it is uninstalled.
Every uninstall of Norishine (upgrades too) keeps a copy of its configuration in that
host's config folder, so reinstalling Apollo or Vibepollo carries the settings back.
Otherwise (and for silent installs) both stay, and Norishine uses port 48989. Only
Norishine's `bin` folder (`norishine.cmd`) goes on the system `PATH`, and the leftover
`PATH` entries of a replaced Vibepollo are removed. See
`src_assets/windows/misc/migration/other-hosts.ps1`. Shared drivers: an existing SudoVDA
device is kept (its driver is only updated in place when ours is newer), ViGEmBus is kept
when 1.17 or later, and uninstalling does not offer to remove either while Apollo or
Vibepollo is installed.

Handled since 2026-11-17:

- Apollo, Vibepollo or Sunshine installed (again) while Norishine is: when its service
  uses the default port, Norishine moves to port 48989 at its next start and saves that
  in `sunshine.conf` (replacing that host later removes the line again). Clients that
  added the host by address need `:48989`.
- Apollo's and Vibepollo's uninstallers delete a `SunshineSvc` service. When replacing
  them, the installer saves the settings of any Sunshine service that isn't theirs and
  puts it back (started again if it was running) if the uninstaller removed it.

Remaining:

- The web UI password: Norishine stores it with a stronger hash (converted at the first
  sign-in), which Apollo and Vibepollo can't read. The copy carried back to them leaves
  the account out, so they ask for a new one when their web UI is first opened.
- Going back to a Norishine build from before the state split (M4): paired devices and the
  web UI account are in `config\state\` and the old file is `sunshine_state.json.v0.bak`.
  Rename it to `sunshine_state.json` first.
- NVIDIA only: the global-profile undo file is `%ProgramData%\Sunshine\nvprefs_undo.json`
  for Sunshine, Apollo, Vibepollo and Norishine alike.

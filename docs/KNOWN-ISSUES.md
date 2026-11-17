# Known issues

Tracked here until fixed. Last checked 2026-11-15 on Windows 11, AMD Radeon 780M; updated
2026-11-17.

## Unit tests

All pass locally. Not run locally because they act on the live desktop:
`MouseInputs/*` (moves the real cursor) and `Configurations/AudioTest.*` (opens real
audio devices). CI runs the full suite.

## Host

- The SudoVDA device is opened for overlapped I/O, but its calls
  (`third-party/sudovda/sudovda.h`) pass no `OVERLAPPED`, which Windows doesn't support.
  It works because the driver completes them at once. Inherited from Apollo; left as is
  until it can be tested with the driver.

Fixed on 2026-11-17:

- SudoVDA displays left behind when a session hung on shutdown ("Hang detected! Session
  failed to terminate"). Norishine lists the displays it creates in
  `config\state\virtual_displays.txt` and removes them before a forced exit (on a separate
  thread, given 3 seconds, so the exit happens even if the driver doesn't answer). Any
  still listed from an earlier run are removed when it starts.
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

- Apollo, Vibepollo or Sunshine installed (again) while Norishine is: when another
  program listens on the default ports, or such a host's service is running, and no port
  is set, Norishine uses port 48989 for that run and shows it on its home page. Nothing
  is saved, so it is back on the default port once the other host is gone. Clients that
  added the host by address need `:48989` meanwhile. If both start at boot and Norishine
  is first, the other host is the one that can't stream.
- Apollo's and Vibepollo's uninstallers delete a `SunshineSvc` service. When replacing
  them, the installer saves any Sunshine service that isn't theirs (start type, account,
  dependencies, recovery actions) and puts it back, started again if it was running, if
  the uninstaller removed it. A service that ran under a user account comes back as
  LocalSystem: set the account again in Services.

Remaining:

- The web UI password: Norishine stores it with a stronger hash (converted at the first
  sign-in), which Apollo and Vibepollo can't read. The copy carried back to them leaves
  the account out, so they ask for a new one when their web UI is first opened.
- Going back to a Norishine build from before the state split (M4): paired devices and the
  web UI account are in `config\state\` and the old file is `sunshine_state.json.v0.bak`.
  Rename it to `sunshine_state.json` first.
- NVIDIA only: the global-profile undo file is `%ProgramData%\Sunshine\nvprefs_undo.json`
  for Sunshine, Apollo, Vibepollo and Norishine alike.

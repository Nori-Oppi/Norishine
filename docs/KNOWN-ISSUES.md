# Known issues

Tracked here until fixed. Last checked 2026-11-15 on Windows 11, AMD Radeon 780M.

## Unit tests

All 306 pass locally. Not run locally because they act on the live desktop:
`MouseInputs/*` (moves the real cursor) and `Configurations/AudioTest.*` (opens real
audio devices). CI runs the full suite.

## Host

- `QOSAddSocketToFlow() failed: 1168` warnings on loopback streams. Expected for
  127.0.0.1; check on a real network.
- Rare: when a session hangs on shutdown ("Hang detected! Session failed to terminate"), the
  host exits without removing its SudoVDA virtual display. On a headless PC the desktop
  then stays on that orphaned display, and the next session streams a second, empty
  one. Restarting the service does not remove it while another process keeps the
  SudoVDA watchdog alive; it has to be removed through the driver by its GUID (the
  client's UUID for Desktop).

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
Vibepollo is installed. Remaining:

- Installing Apollo or Vibepollo again while Norishine is still installed: both use the
  default ports, so whichever starts second cannot stream. Uninstall Norishine first.
- Apollo's and Vibepollo's own uninstallers (run when replacing them) also delete a
  `SunshineSvc` service, which would remove a Sunshine install on the same PC.
- Norishine stores the web UI password with a stronger hash than Apollo and Vibepollo
  (converted at the first sign-in). If you go back to Apollo or Vibepollo with Norishine's
  carried-back settings, set a new web UI password there (`sunshine.exe --creds <user>
  <password>`, or delete the credentials from its state file).
- Since the state split (M4), a Norishine build from before it no longer finds the paired
  devices and web UI account after a newer one ran: they are in `config\state\`, and the
  old file is `sunshine_state.json.v0.bak`. To go back, rename that file to
  `sunshine_state.json`. Carrying settings back to Apollo or Vibepollo on uninstall
  rebuilds the file for them (without the web UI account if its password was changed or
  used in Norishine, since they can't read the new hash: they then ask for a new one).
- NVIDIA only: the global-profile undo file is `%ProgramData%\Sunshine\nvprefs_undo.json`
  for Sunshine, Apollo, Vibepollo and Norishine alike.

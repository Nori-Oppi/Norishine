# Known issues

Tracked here until fixed. Last checked 2026-11-15 on Windows 11, AMD Radeon 780M.

## Unit tests

All 252 pass locally. Not run locally because they act on the live desktop:
`MouseInputs/*` (moves the real cursor) and `Configurations/AudioTest.*` (opens real
audio devices). CI runs the full suite.

## Host

- `QOSAddSocketToFlow() failed: 1168` warnings on loopback streams. Expected for
  127.0.0.1; check on a real network.

## Installer next to Apollo, Vibepollo or Sunshine

When Apollo or Vibepollo is installed, the installer asks to replace it (recommended):
its settings, apps and paired devices are copied to Norishine and it is uninstalled.
Uninstalling Norishine later puts Norishine's configuration back in its config folder, so
reinstalling Apollo or Vibepollo carries the settings back. Otherwise (and for silent
installs) both stay, and Norishine uses port 48989. See
`src_assets/windows/misc/migration/other-hosts.ps1`. Shared drivers: an existing SudoVDA
device is kept (its driver is only updated in place when ours is newer), ViGEmBus is kept
when 1.17 or later, and uninstalling does not offer to remove either while Apollo or
Vibepollo is installed. Remaining:

- Installing Apollo or Vibepollo again while Norishine is still installed: both use the
  default ports, so whichever starts second cannot stream. Uninstall Norishine first.
- Apollo's and Vibepollo's own uninstallers (run when replacing them) also delete a
  `SunshineSvc` service, which would remove a Sunshine install on the same PC.
- Both add their install folder to the system `PATH`, and both have a `sunshine.exe`.
  `sunshine.exe` on the command line runs whichever folder comes first.
- NVIDIA only: the global-profile undo file is `%ProgramData%\Sunshine\nvprefs_undo.json`
  for Sunshine, Apollo, Vibepollo and Norishine alike.

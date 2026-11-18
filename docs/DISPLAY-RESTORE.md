# Display restore after the host stops unexpectedly

A stream can change the host's displays: the display settings for the client (resolution,
refresh rate, HDR, primary display, which displays are on) and SudoVDA virtual displays.
A normal stop undoes both. This page lists what undoes them when the host doesn't stop
normally. Roadmap M10 ([ROADMAP.md](ROADMAP.md)).

## What the host keeps

- **Display settings**: before changing anything, the host writes what it changed and the
  values from before to `display_device.state` in its config folder. The file is removed
  once they're undone. As long as it exists, the next run undoes the changes when it
  starts, and keeps retrying (each time a display is added or removed) until it succeeds.
- **Virtual displays**: the host lists each one it creates in
  `config\state\virtual_displays.txt` until it's removed. The next run removes any still
  listed. The SudoVDA driver also removes the displays of a host that stops answering its
  watchdog.

## Cases

| How the host stopped | Display settings undone | Virtual displays removed |
| --- | --- | --- |
| Normal stop, service stop, restart from the web UI | At once | At once |
| Crash, as a service | When the service starts it again, about 3 s later | By the new run, or by the driver's watchdog |
| Hang on stop (forced exit after 10 s) | By the next run, which the service starts at once | Before the forced exit |
| Service stopped while the host hangs (killed after 20 s) | **Now at once** (`--restore-displays`); before: at the next service start, usually the next boot | **Now at once**; before: by the driver's watchdog or the next start |
| Uninstalled after a crash or kill | **Now by the uninstaller**; before: never | **Now by the uninstaller**; before: by the driver's watchdog |
| Run without the service (by hand), crash | When it's started again, or `sunshine.exe --restore-displays` | Same, or by the driver's watchdog |

## `--restore-displays`

`sunshine.exe --restore-displays` undoes what an earlier run left behind, once, and exits:
the display settings in `display_device.state` and the virtual displays in
`virtual_displays.txt`. It exits with 0 when nothing is left to undo and 1 when undoing
failed (the next start tries again). With display changes turned off
(`dd_configuration_option` disabled) it forgets the saved changes, as a normal start does.
It logs to `sunshine.log.restore`, so the log of the host that was killed stays as it was.

The service runs it after it had to kill a host that didn't stop within 20 seconds, and
the uninstaller runs it before removing the files.

## Not covered

- A crash of Windows itself, or a power cut: the next start undoes the changes, as after a
  crash.
- Changes made by apps' own prep commands.

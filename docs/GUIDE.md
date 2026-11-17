# Norishine guide

## Getting started

1. **Install on your host PC.** Download the Windows x64 installer from the
   [nightly release](https://github.com/Nori-Oppi/Norishine/releases/tag/nightly).
   Nightlies are unsigned development builds; there is no stable release yet.
2. **Set up the host.** Open Norishine's local web UI and create your sign-in. Use the
   pairing page to approve Norilight or another compatible client.
3. **Start streaming.** Pick Desktop or an app in your client. Norishine's SudoVDA
   integration provides virtual displays that can follow the client.

## Coming from another host

The Windows installer can replace Apollo or Vibepollo and copy its settings, apps and
paired devices, or keep both hosts installed.

| Installation choice | What happens |
| --- | --- |
| **Replace Apollo or Vibepollo** | Copies its configuration to Norishine and uninstalls the previous host. |
| **Keep both** | Leaves the existing host installed and uses port **48989** for Norishine. |
| **Uninstall Norishine later** | Keeps a configuration copy for migration back and preserves shared drivers when another host needs them. |

Replacement has been tested on one machine. Fresh installation, coexistence and
migration back still need broader validation. Read the
[known issues and migration limits](KNOWN-ISSUES.md) before switching hosts.

## Host controls in Norilight

[Norilight](https://github.com/Nori-Oppi/Norilight) can show the host's status, change its
settings, restart it, and manage its apps, paired devices and displays. It signs in with
the certificate from pairing, so it never needs the web UI password.

1. In the host's web UI, open **Devices**, edit your Norilight device, and under
   **Host management** choose **View** (look only) or **Manage** (change things too).
   Manage is full control of the PC, because app and device commands run on it.
2. In Norilight, right-click the host and choose **Host Settings** or **Manage Host**.

A device can't change its own permissions. Settings with lists or scripts are changed in
the web UI.

## Pairing

Pairing lets a device stream from this PC. Afterwards, choose what it may do (see
[Devices and permissions](#devices-and-permissions)). Only pair devices you control: a
paired device can get full control of this PC if you allow it.

**PIN shown on the device** (Moonlight, Norilight and most clients): in the client, add
this PC and start pairing. The request shows up on the **Pairing** page within a few
seconds; type the 4-digit PIN the client shows, optionally give the device a name, and
press **Pair**. **Set permissions** then opens the new device. If several devices ask at
once, pick the right one; the ✕ turns a request down.

**One-time PIN (Artemis only)**: pick a passphrase of at least 4 letters or digits and press
**Make a PIN**. Enter the PIN and passphrase in Artemis, or scan the code with it. The PIN
works for 3 minutes. If the address in the code is wrong (for example when the web UI is
opened through another name), change it under the code.

## Devices and permissions

The **Devices** page lists paired devices; click one to change it. The first device you
pair may start apps and send input. Devices paired later may only see apps until you allow
more.

**What it can do** sets the usual combinations in one click:

| Choice | Means |
| --- | --- |
| Watch only | Joins streams others started. No input. |
| Play | Starts apps. Controller and touch input. |
| Full control | Starts apps. All input and the clipboard. |

**Fine-tune** has the individual parts, and opens by itself when a device has a mix that
isn't one of the three:

- **Streaming**: Nothing (can't see or start apps), See apps (lists them, can't stream),
  Watch (joins streams others started), Start apps (starts and stops apps).
- **Input it may send**: controller, keyboard, mouse, touch and pen, each on or off.
- **Extras**: *Send clipboard* (paste from the device into this PC), *Read clipboard* (copy
  from this PC to the device), *Run server commands* (the commands listed under
  Settings › Commands).

**Host management** is for Norilight: it lets the device see (**View**: status, settings,
apps, devices and displays) or change (**Manage**: settings, apps and paired devices; end
streams; restart the host) this host without the web UI password. App and device commands
run on this PC, so only give Manage to devices you trust like this account.

**Display**: *Display mode for this device* replaces the resolution the device asks for, as
width x height x refresh rate (for example `1920x1080x60`); leave it empty to use the
device's. *Always create virtual display* gives this device its own screen every time.
*Enable legacy ordering* sorts the app list for older clients; turn it off if the device
shows app names wrongly.

**Commands** run on this PC when the device connects and disconnects (and allow apps' client
commands for it). Turn them off to skip both.

**Unpair** removes a device; it has to pair again to stream. A running stream continues
until it ends, so disconnect it first to stop it now. **Unpair all** is at the bottom of the
list.

## Apps

**Apps** are what clients can start on this PC; clients see changes the next time they
refresh their list, and the list is read again when a stream ends. Click an app to edit it.
**Launch** starts it on the PC without a stream. Drag the handle, or use **⋯ › Move up /
Move down**, to change the order clients show; **Alphabetize** sorts by name. **⋯** also has
**Export for Artemis** (a launcher file) and **Delete** (nothing is removed from the PC).
Saving a change reloads the list, which closes a running app.

In the editor:

- **Command**: the program to start. Leave it empty to stream the desktop. Put quotes around
  a path with spaces. **Working directory** is the folder it starts in; **Output log file**
  is where its output goes (optional).
- **Image**: a PNG file on this PC, shown as the app's picture. **Find cover** downloads one.
- **Run as administrator**, the gamepad type and how long to wait when closing.
- **Commands before and after**: run before the app starts and after it ends. If one fails,
  the app doesn't start. **Resume and pause commands** run when the first device connects and
  the last one disconnects. **Background commands** start with the app and keep running.
- Hover a switch's name for what it does. **Environment variables** (at the bottom) lists
  what commands and the app receive.

## Settings

Every setting is explained in [Settings](SETTINGS.md); hover a setting's name in the web UI
for the same text. Search (press `/`) covers all settings, advanced ones too. **Show advanced
settings** adds the rarely needed ones to each page, and shows each setting's name in
`sunshine.conf` on hover. Pages under **Expert** are for working around specific problems.
Most changes take effect after the host restarts: **Save and restart** does both, **Save
only** keeps them for the next restart. Ctrl+S saves.

## Troubleshooting

- **Close the running app**: when a client says an app is still running. Streams of it end.
- **Restart Norishine**: streams on this host stop while it restarts.
- **Forget display changes** (Windows): when Norishine keeps trying to put back display
  settings it changed for a stream. Your displays aren't changed now; set them in Windows
  yourself if needed.
- **Quit Norishine**: stops the host. Start it again from the Start menu, or restart the
  Norishine service.

The log below shows what the host did; filter it, search it, or copy and download it for a
bug report.

## Forgot the password

Changing the sign-in (account menu › **Change password**) signs everyone out. If you forgot
it: on the host PC, open a Command Prompt as administrator, run
`norishine --creds <username> <password>`, then restart Norishine.

## Updating

Download the latest installer from the
[nightly release](https://github.com/Nori-Oppi/Norishine/releases/tag/nightly) and run it
over the installed version; settings, sign-in and paired devices are kept. The nightly is
rebuilt when `master` changes; check its commit when you're looking for a specific change.

Windows x64 is the developed and tested target. The Linux and macOS source is kept but
isn't verified here.

## More

- [Settings](SETTINGS.md): every setting explained
- [Features](FEATURES.md), with screenshots
- [Known issues](KNOWN-ISSUES.md)
- [Roadmap](ROADMAP.md)
- [Management API](api.md), for client authors
- [Development](DEVELOPMENT.md): building, installers and test hosts

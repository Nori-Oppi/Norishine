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

## Updating

Download the latest installer from the
[nightly release](https://github.com/Nori-Oppi/Norishine/releases/tag/nightly) and run it
over the installed version; settings, sign-in and paired devices are kept. The nightly is
rebuilt when `master` changes; check its commit when you're looking for a specific change.

Windows x64 is the developed and tested target. The Linux and macOS source is kept but
isn't verified here.

## More

- [Features](FEATURES.md), with screenshots
- [Known issues](KNOWN-ISSUES.md)
- [Roadmap](ROADMAP.md)
- [Management API](api.md), for client authors
- [Development](DEVELOPMENT.md): building, installers and test hosts

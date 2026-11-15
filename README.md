<img src="assets/icon.svg" width="64" height="64" alt="">

# Norishine

Norishine is the streaming host of the Nori project. It pairs with
[Norilight](https://github.com/azijnwater/Norilight) and stays compatible with
Moonlight, Artemis and other clients of the GameStream protocol.

It is a fork of [Apollo](https://github.com/ClassicOldSong/Apollo), which is itself a
fork of [Sunshine](https://github.com/LizardByte/Sunshine). Norishine keeps Apollo's
virtual displays, per-client permissions, OTP pairing, clipboard sync and server
commands, and carries Sunshine's security fixes forward.

## Status

Early development. Nothing here is released, and the code is currently Apollo with
project changes on top. See:

- [Roadmap and architecture decisions](docs/ROADMAP.md)
- [Phase 1 research](docs/PHASE1-RESEARCH.md)
- [Original Apollo README](docs/APOLLO-README.md)

## Building

Windows builds use MSYS2 UCRT64. See [docs/building.md](docs/building.md).

## License and credits

GPL-3.0, like Apollo and Sunshine. Copyright notices and history from both projects
are preserved in this repository. Thanks to the Apollo and Sunshine authors and to
the Moonlight project.

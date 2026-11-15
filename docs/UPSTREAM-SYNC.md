# Upstream sync log

Norishine is based on Apollo `adc5c5a0` (2026-05-21), whose last Sunshine merge is
`1a96d135` (2025-09-26). Changes taken from Sunshine since then are listed here.
Cherry-picks use `git cherry-pick -x`, so each commit message also names its source.

| Sunshine commit | What | Norishine commit | Notes |
|---|---|---|---|
| `86a25385` | Only flush the encoder on teardown if it produced frames | cherry-picked | Did not by itself fix the AMF hang below |
| build-deps `v2026.1017.131201` | Newer FFmpeg (`946fcce`) | `cmake/dependencies/ffmpeg.cmake` | Fixes AMF encoders hanging on flush with AMD driver 32.0.32015 (Radeon 780M). Downloaded and SHA256-verified at configure time instead of the build-deps submodule |

## Pending, in priority order

Security (published advisories Apollo lacks; see docs/PHASE1-RESEARCH.md):

- `82bccdf6` GHSA-c428-87f8-rrv5: short ENet control packet (confirmed in `src/stream.cpp`)
- `1583e7c4` GHSA-26q2-58j6-qmvv / GHSA-6w33-pjh7-p77c: input packet validation
- `ccf97e38` GHSA-36ff-frg7-492f: PIN applied to an unintended pairing session (confirmed in `src/nvhttp.cpp`); adapt to OTP pairing
- `4d768847` GHSA-6jvv-jqr7-m6m3: exact client certificate match (confirmed in `src/crypto.cpp`)
- `31b85802`: redact sensitive data from logs. Debug logging currently writes the web UI session cookie.
- `f86c4c9c`, `127b4948`, `c33f3b46`, `0daeb0e3`, `c1012508`, `c8ea783c`, `4dadc7d4`

Then stability and Windows capture/encoder fixes listed in docs/PHASE1-RESEARCH.md.
NVENC SDK update (`fb3d85cf` and follow-ups) is needed before the FFmpeg package's own
NVENC headers can be used; until then `ffmpeg.cmake` removes them.

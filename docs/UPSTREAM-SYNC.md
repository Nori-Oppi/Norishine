# Upstream sync log

Norishine is based on Apollo `adc5c5a0` (2026-05-21), whose last Sunshine merge is
`1a96d135` (2025-09-26). Changes taken from Sunshine since then are listed here.
Cherry-picks use `git cherry-pick -x`, so each commit message also names its source.

| Sunshine commit | What | Norishine commit | Notes |
|---|---|---|---|
| `86a25385` | Only flush the encoder on teardown if it produced frames | cherry-picked | Did not by itself fix the AMF hang below |
| build-deps `v2026.1017.131201` | Newer FFmpeg (`946fcce`) | `cmake/dependencies/ffmpeg.cmake` | Fixes AMF encoders hanging on flush with AMD driver 32.0.32015 (Radeon 780M). Downloaded and SHA256-verified at configure time instead of the build-deps submodule |

| `82bccdf6` | GHSA-c428-87f8-rrv5: validate control packet length | cherry-picked | Test include conflict resolved |
| `1583e7c4` | GHSA-26q2-58j6-qmvv, GHSA-6w33-pjh7-p77c: validate input packets | ported by hand | Validation also runs before Apollo's permission check, which read the packet type unchecked |
| `4d768847` | GHSA-6jvv-jqr7-m6m3: exact client certificate match | ported by hand | Only the crypto part; the nvhttp part is Sunshine's client enable/disable feature |
| `ccf97e38` | GHSA-36ff-frg7-492f: PIN applied to the wrong pairing session | ported by hand | Adapted to Apollo's OTP pairing (OTP sessions are never listed as waiting) and pin page. Client name stays optional. Also guards Apollo's OTP state with the session mutex |
| `c33f3b46` | Retried `getservercert` replaces an abandoned handshake | ported by hand | Follow-up to `ccf97e38`; without it a client that gave up mid-pairing is blocked for 5 minutes |
| `31b85802` | Redact sensitive data from logs | adapted | Config half not applicable (Apollo has no `csrf_allowed_origins` or secret settings). Instead redacts what Apollo actually logged at debug level: the web UI session `Cookie`, and GameStream `rikey` and pairing values (`salt`, challenges, `otpauth`) |
| `71323d38` | Build Boost.Locale on Linux only | ported by hand | Needed for CI: Boost 1.89's locale CMakeLists fails to configure with CMake 4.5 |
| `f86c4c9c` | Bound `from_hex` to the target size | ported by hand | In Apollo a long input overflowed a stack buffer |
| `127b4948` | Read an odd hex digit count as a leading zero | ported by hand | In Apollo an odd count read before the start of the input |
| `0daeb0e3` | Replace duplicate client certificates on pairing | adapted | Apollo's client list instead of Sunshine's client auth store. In Apollo, saving kept only the first record per certificate, so pairing again was silently dropped and the old record (name, permissions) stayed |
| `c1012508` | Separate audio and video UDP receive endpoints | ported by hand | With its loopback test |
| `c8ea783c` | Open browser URLs through the Windows shell | ported by hand | Upstream's 200-line launcher test not ported; a test of the command line is |
| `4dadc7d4` | Keep trailing backslashes when writing system PATH | ported by hand | Also moves `/f` before `/d` (an earlier upstream fix Apollo lacked). Not run locally: it writes the system PATH |

## Pending, in priority order

Security (published advisories Apollo lacks; see docs/PHASE1-RESEARCH.md):

- All security commits identified in docs/PHASE1-RESEARCH.md are ported. Token-based
  CSRF protection for the web UI is part of the login hardening in roadmap M3. Check
  Sunshine's security advisories again before each release.

Then stability and Windows capture/encoder fixes listed in docs/PHASE1-RESEARCH.md.
NVENC SDK update (`fb3d85cf` and follow-ups) is needed before the FFmpeg package's own
NVENC headers can be used; until then `ffmpeg.cmake` removes them.

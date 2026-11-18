# Capture from a user-session helper (design)

Status: designed, not built. Roadmap M10 ([ROADMAP.md](ROADMAP.md)).

## Why

As a service, Norishine runs as SYSTEM in the console session. That lets it capture the
secure desktop (sign-in screen, UAC prompts) with Desktop Duplication (DXGI, `ddx`), but
Windows.Graphics.Capture (WGC) only works for a signed-in user, so `capture = wgc` isn't
available to the service ([configuration.md](configuration.md#capture)). WGC captures some
things Desktop Duplication handles badly, such as displays on another GPU than the
encoder's.

A helper process that runs as the signed-in user can capture with WGC and hand the frames
to the host on the GPU.

## Measured first

The development host runs as the user, so it can use WGC directly: that's what a helper
would deliver, without the hand-off cost. Same benchmark, Balanced goal, 3 runs each
([BENCHMARK.md](BENCHMARK.md#m10-capture-method)):

| Capture | fps avg | Latency avg ms (runs) | Encoder % avg | Host CPU % avg |
| --- | --- | --- | --- | --- |
| Desktop Duplication (`ddx`, today's default) | 49.8 | 4.64 (4.51, 4.46, 4.95) | 38.0 | 56 |
| Windows.Graphics.Capture (`wgc`) | 52.1 | 4.01 (3.85, 3.96, 4.22) | 40.3 | 38 |

WGC measured 0.6 ms lower in every run, but the two don't start the clock at the same
point: Desktop Duplication stamps a frame when the app presented it, WGC when the desktop
was composed, which is later. So some or all of the 0.6 ms is the clock, not capture. A
fair comparison needs latency measured at the client (capture to display). The encoder load
is the same; the host CPU difference is other programs, which varied between runs.

What the helper would bring is what WGC can do and Desktop Duplication can't, not speed.

## Design

**Processes.** The host (SYSTEM) starts `norishine-capture.exe` in the console session
with the signed-in user's token (`WTSQueryUserToken`, the same way it starts apps as the
user), in a job object so it ends with the host. One helper per stream display; it exits
when its pipe closes.

**Control.** A named pipe the host creates, with an ACL for SYSTEM and the user's SID
only. Messages: start (display, frame rate, HDR format, cursor on or off), stop, and from
the helper: started (size, format), failed (reason), frame ready (slot, timestamp).

**Frames.** The host creates a ring of three textures on the encoder's adapter with
`D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX` and
duplicates their handles into the helper. The helper opens them on its own D3D11 device
(the same adapter, by LUID), copies each WGC frame into a free slot under the keyed
mutex and sends "frame ready". Frames never leave the GPU. A slot the encoder still holds
is skipped, so a slow encoder drops frames instead of queueing them.

**Host side.** A new `display_t` (`display_wgc_helper_vram_t`) with the same
`snapshot()` / `release_snapshot()` as `display_wgc_vram_t`; the capture loop, pacing and
encoders don't change. `capture = wgc` uses it when the host runs as SYSTEM.

**Secure desktop.** WGC can't capture the sign-in screen or UAC prompts. On a desktop
switch the host falls back to Desktop Duplication (it already detects the switch) and
returns to the helper afterwards.

**Failures.** If the helper doesn't start, fails, or sends nothing for 2 seconds while
frames are expected, the host logs it and uses Desktop Duplication for the rest of the
stream. Signing out ends the helper; the host falls back the same way.

**Security.** The helper takes no input other than the pipe, does nothing but capture,
and the pipe only accepts SYSTEM and that user. It runs with the user's rights, which are
fewer than the host's.

## Size

About 1500 lines: the helper (WGC, shared textures, pipe), the host's display type and
fallback, the installer, and tests for the pipe protocol. Measure before and after with
the benchmark: latency must stay within the run-to-run spread of Desktop Duplication's.

## Decision

Not built today. Two reasons:

- It can only be tested where it matters, with the host running as a service, and the
  development build isn't installed as a service on this PC.
- The measured gain may be only the clock (see above), so the case rests on what WGC can
  capture that Desktop Duplication can't, and on measuring capture-to-display latency at the
  client first.

Next step, if the owner wants it: add a client-side latency figure to the benchmark (Norilight
knows when it shows each frame), measure `ddx` against `wgc` again, then build the helper on
a machine where the service can be installed for testing.

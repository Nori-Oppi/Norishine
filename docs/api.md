# API

Sunshine has a RESTful API which can be used to interact with the service.

Unless otherwise specified, every call needs a signed-in session: `POST /api/login` with
`{"username": ..., "password": ...}` sets two cookies, `auth` (HttpOnly, the session) and
`csrf` (readable by the pages). Every call that changes something (any method other than
GET) must also send the `csrf` value in an `X-CSRF-Token` header, and a browser `Origin`, if
present, must be this server; otherwise the answer is 403. `POST /api/logout` ends the
session. Several browsers can be signed in at once; sessions last 15 days, survive restarts of the host
(`state/sessions.json`, token hashes only) and end when the password changes.

After five failed sign-ins from one address, further attempts from it get 429 with
`Retry-After` for 30 seconds, doubling with each further failure up to 15 minutes; a
successful sign-in or an hour without failures resets it. Passwords are stored as
PBKDF2-HMAC-SHA256; a password stored by Apollo or Sunshine (one round of SHA-256) still
works and is converted at the next sign-in.

@htmlonly
<script src="api.js"></script>
@endhtmlonly

## GET /api/apps
@copydoc confighttp::getApps()

## POST /api/apps
@copydoc confighttp::saveApp()

## POST /api/apps/close
@copydoc confighttp::closeApp()

## DELETE /api/apps/{index}
@copydoc confighttp::deleteApp()

## GET /api/clients/list
@copydoc confighttp::getClients()

## POST /api/clients/unpair
@copydoc confighttp::unpair()

## POST /api/clients/unpair-all
@copydoc confighttp::unpairAll()

## GET /api/config
@copydoc confighttp::getConfig()

## GET /api/configLocale
@copydoc confighttp::getLocale()

## POST /api/config
@copydoc confighttp::saveConfig()

## GET /api/v1/config/schema
@copydoc confighttp::getConfigSchema()

Each setting has `key`, `category` (an `id` from `categories`), `type` (`boolean`, `integer`,
`number`, `string`, `choice`, `path`, `prep_commands`, `server_commands`, `keybindings`,
`mode_remapping`), `tier` (`basic`, `advanced`, `dangerous`), `platforms`, `available` (on
this host), `default`, `restart_required` (false for settings the running host takes over as
soon as they're saved, also in running streams; see [AUTOTUNE.md](AUTOTUNE.md#settings-during-a-stream-n9)),
`label` and `description` (keys into the web UI
locale files), and where it applies `min`, `max` and `choices` (an empty choice means
automatic). The table is `src/config_schema.cpp`.

Optional fields, added later (clients must not require them):

- `depends_on`: `{"key": ..., "values": [...]}`. The setting only has an effect while that
  other setting (always earlier in the list, in the same category) has one of these values.
  The web UI indents such settings and says when they have no effect.
- `value_titles`: readable names for values, keyed by the value as text (`"0"` for an
  integer, `""` for automatic). Choices always have them; integers with a few named values
  (`hevc_mode`, `min_log_level`, ...) have one per value and can be shown as a list.

- `value_titles` on wide numeric ranges: only their special values are named, such as
  `{"0": "No limit"}` for `max_bitrate` or `{"-1": "Off"}` for `back_button_timeout`.
- `unit`: `kbps`, `ms`, `percent`, `fps` or `per_second`, for display (a UI may show `kbps`
  as Mbps; the stored value stays in kbps).
- `section`: the `id` of a sub-heading in `sections` (same category) the setting belongs under.

`categories` lists `id` and `title` in display order; related categories also have `group`
and `group_title` (the encoder categories, under "Encoder tuning"). Optional: `description`
(one line) and `expert: true` for categories few people need (encoder tuning, fixes, files),
which a UI may tuck away. Category ids and the order of settings can change between
versions, so render them from the schema rather than by name.

Optional, added later: `sections` lists sub-headings inside a category, in display order,
each with `id`, `category` and `title`. A section with `depends_on` (as for settings) only
matters while that setting has one of the values; it then also has a `note` saying why,
which a UI can show while dimming the section (for example "When streaming a monitor" while
streaming to a virtual display). Settings without a `section` come before the sections.

## GET /api/v1/config
@copydoc confighttp::getConfigV1()

## PATCH /api/v1/config
@copydoc confighttp::patchConfigV1()

## Management API for paired clients

Norilight (and other clients) use the same `/api/v1` calls on the GameStream HTTPS port
(47984 by default), authenticated by the certificate the client paired with; no admin
password is stored on the client. Each call also needs a permission that is never granted
by default, only per device from the web UI (paired devices, "Manage"):

| Permission | Bit | Allows |
| --- | --- | --- |
| `manage_read` (Read Host Settings) | `0x10000000` | every `GET` below: status, stats, settings, apps, devices, displays |
| `manage_write` (Change Host Settings) | `0x20000000` | the above, and every call that changes something: settings, `POST /api/v1/restart` (settings apply after a restart), apps, devices, streams, displays |

`manage_write` is full control of the host: settings, app and device commands run
programs on it. Grant it only to devices you trust as much as the host's own account.

Without the permission the answer is 403 with a JSON error. `serverinfo` (HTTP and HTTPS)
carries `<Norishine><ApiVersion>1</ApiVersion><Features>tuning,optimise,status,stats,session,session.settings,session.video,adaptive,recommendations,update,config.schema,config.read,config.write,restart,apps,apps.write,devices,devices.write,sessions.write,displays</Features></Norishine>`
(check for a feature before using it; older hosts list fewer),
and `Permission` shows what the requesting device may do. `GET /api/v1/status` returns the
host name, version, platform, unique ID, running app, number of active sessions, and the
requesting device's name and permissions. The schema takes `?locale=` (a value of the
`locale` setting) and gives each setting a readable `title` and `help`, plus category
names, for clients without the web UI's locale files. `scripts/e2e/manage_check.py` checks all of it
against a dev host.

## GET /api/v1/status
@copydoc confighttp::getStatusV1()

Host name, version, platform, unique ID, running app (`id`, `uuid`, `name`, or null) and
`active_sessions`, plus `sessions`: one entry per stream with the device's `uuid` and
`name`, its `address`, `width`, `height`, `fps`, `bitrate_kbps`, `codec` (`H.264`, `HEVC`,
`AV1`), `hdr` and `state`. Optional, added later: `encoder`, the encoder the host chose
(`nvenc`, `quicksync`, `amdvce`, `vaapi`, `videotoolbox`, `software`, or empty before the
first check); `port`, the base port in use; `port_moved_for`, on Windows the other host
that had the default port at startup, so this run uses 48989 (empty otherwise). The same
object (plus `device`) is what paired clients get from
`GET /api/v1/status` on the HTTPS port.

## GET /api/v1/stats
@copydoc confighttp::getStatsV1()

Host load and measured figures for each active stream, on both ports (feature `stats`).
Poll it every few seconds; readings closer than a quarter of a second apart return the
previous answer, and the first call takes up to two seconds.

`host`: `cpu_percent` (all processes), `process_cpu_percent` (Norishine, share of all
cores), `memory_total_mb`, `memory_used_mb`, `process_memory_mb`, `gpu_percent` (the
busiest GPU engine, as Task Manager shows it) and `encoder_percent` (the busiest video
encode engine; on AMD the combined encode and decode engine). Each is null where the host
can't measure it: only Windows reports them, and GPU figures need the "GPU Engine"
performance counters. `encoder` is as in `/api/v1/status`.

`sessions`: one entry per stream, with the device's `uuid` and `name` and:

| Field | Meaning |
| --- | --- |
| `duration_s` | Time since the first frame |
| `window_s` | Complete seconds the recent figures cover, up to 5 (0 in the first second) |
| `fps`, `bitrate_kbps` | Frames and video sent per second over the window (bitrate before FEC and headers); null until `window_s` > 0 |
| `latency_ms` | `min`, `avg`, `max` from capture to handing the frame to the network, captured frames only; null if none |
| `send_ms` | `min`, `avg`, `max` time spent sending each frame (FEC, encryption, pacing) |
| `recent_recovery_requests` | Key frame requests and lost reference frame reports in the window |
| `frames`, `duplicate_frames`, `bytes`, `key_frames` | Totals sent |
| `key_frame_requests`, `ref_invalidations`, `frames_invalidated` | Totals of what the client reported lost |
| `client_lost_frames` | From the periodic loss reports of older clients; 0 for current Moonlight |
| `adaptive` | What the stream's controller does now (feature `adaptive`), or null when `adaptive_bitrate` is off; see below |

`adaptive` ([AUTOTUNE.md](AUTOTUNE.md)):

| Field | Meaning |
| --- | --- |
| `enabled`, `live_bitrate` | The controller runs; the encoder can change bitrate during the stream (otherwise only FEC changes) |
| `bitrate_kbps` | Bitrate the encoder is set to now |
| `max_kbps`, `min_kbps` | Bounds: the stream's start bitrate, and `adaptive_min_bitrate` |
| `fec_percent` | Error correction now |
| `last_reason`, `last_change_s` | Why and when (seconds into the stream) it last changed something: `congestion`, `delay`, `loss`, `probe`, `fec_relax`, `settings` (the host's settings changed), `client` (the client asked, `POST /api/v1/session/settings`) or `none` |
| `decreases`, `increases` | Bitrate changes so far |
| `client_reports` | The client sent a report in the last 3 seconds |
| `host_limited`, `client_limited` | The host can't encode, or the client can't decode, as fast as asked; the bitrate isn't raised meanwhile |
| `base_rtt_ms` | Lowest round-trip time the client reported in the last minute, or null |

## GET /api/v1/recommendations
@copydoc confighttp::getRecommendationsV1()

Feature `recommendations`. On both ports (`manage_read` on the HTTPS port). Recommended
encoder settings from what earlier streams on this host measured
([AUTOTUNE.md](AUTOTUNE.md), "Host settings"):

| Field | Meaning |
| --- | --- |
| `encoder` | The encoder they are for (as in `/api/v1/status`; before the host's first encoder check, the one the last stream used) |
| `streams` | Streams recorded for it (up to 10) |
| `recommendations` | One entry per setting: `key`, `current` and `recommended` (typed as in `/api/v1/config`), `reason` (`encoder_limited`, `headroom` or `loss`), `streams` (how many it is based on), `restart_required` |
| `applied` | What the last apply changed, for undo, or null: `at` (Unix time) and `settings`, each with `key`, `previous`, `value` (what apply set) and `current` |

## POST /api/v1/recommendations/apply

`{"keys": ["nvenc_preset", ...]}` applies those recommendations; an empty body or `{}`
applies all of them. Needs `manage_write` on the HTTPS port (and the CSRF header on the web
UI port). The change goes through the same path as `PATCH /api/v1/config` and answers the
same way (`changed`, `restart_required`). The values from before are kept (in
`state/recommendations.json`) until undone; applying again keeps the oldest ones, so one
undo goes all the way back. 409 when a key has no recommendation or there's nothing to
apply, 400 for a malformed body.

## POST /api/v1/recommendations/rollback

Puts back the values from before the last apply: settings that weren't in the config file
are removed again. A setting changed since the apply is left as it is and listed in
`skipped`. Answers as apply; 409 when there is nothing to undo.

## GET /api/v1/optimise

Feature `optimise` (the host has its test picture, `tools/norishine-testpattern.exe`). On
both ports (`manage_read` on the HTTPS port). The host's encoder test (roadmap N6): the
moving test picture is encoded with each step of the encoder's preset, fastest first, up
to the step the recommendations stop at, 120 frames each at the display's size and 60 fps.
The pick is the highest-quality step whose 95th percentile time per frame stays within 40 %
of a frame (6.7 ms), else the fastest one.

| Field | Meaning |
| --- | --- |
| `state` | `idle`, `running`, `done` or `failed` (`error` says why) |
| `progress` | 0 to 100 |
| `encoder`, `codec`, `width`, `height`, `fps` | What was tested |
| `setting` | The preset setting (`nvenc_preset`, `amd_quality`, `qsv_preset` or `sw_preset`) |
| `current` | Its value in the config file (null when unset) |
| `results` | One entry per step: `value`, `frames`, `avg_ms`, `p95_ms`, `error` |
| `pick` | The step to use, or null |

## POST /api/v1/optimise

Starts the test; needs `manage_write` on the HTTPS port (and the CSRF header on the web UI
port). 202 with the state above; 409 while a stream or a test runs, or when the encoder has
no preset to choose; 503 without a working encoder. Streams can't start while it runs (503).

## POST /api/v1/optimise/apply

Writes the pick to the config the way `POST /api/v1/recommendations/apply` does, and is
undone the same way (`POST /api/v1/recommendations/rollback`). 409 when there is no pick.

**Test streams.** On hosts with the feature, `serverinfo` also carries
`<OptimiseAppId>` inside `<Norishine>`: a hidden app that shows the test picture on the
stream's display. Launching it by that id (or by the UUID `6F7E21C4-5B0A-4D2E-9C3B-0E5A9D4B7C11`)
gives a test stream; the app ends when the stream does. Norilight's Optimise uses it.

## GET /api/v1/session

Feature `session`. HTTPS port only. The requesting device's own stream; no management
permission is needed, since a device only sees its own. 404 when it has no stream running.

`stream`: the per-stream fields of `/api/v1/stats` (including `adaptive`), plus `width`,
`height`, `target_fps`, `start_bitrate_kbps`, `codec`, `hdr` and `tuning` (the goal the
encoder was set up for, or empty).

## Launch parameter norishineTuning

Feature `tuning` (listed only while the `client_tuning` setting is on). `/launch` and
`/resume` take an optional `norishineTuning=performance|balanced|quality`: the encoder preset
for that stream ([AUTOTUNE.md](AUTOTUNE.md), "Encoder per goal"). Other values, and the
parameter on hosts without the feature, are ignored.

## POST /api/v1/session/report

Feature `adaptive`. HTTPS port only, no management permission. The client's own measurements
for its stream's controller, about once a second (JSON, every field optional): `loss_percent`
(video frames lost after FEC, percent), `rtt_ms`, `rtt_variance_ms`, `decode_ms`, `fps`,
`display_hz`. Answers as `GET /api/v1/session`; 400 for a value of the wrong type or out of
range, 404 without a stream.

## POST /api/v1/session/settings

Feature `session.settings` (roadmap N9). HTTPS port only, no management permission: a device
changes only its own stream. Changes the running stream without reconnecting (JSON, every
field optional, at least one needed):

| Field | Meaning |
| --- | --- |
| `bitrate_kbps` | The bitrate to stream at, 500 to 1000000, counted as at launch (before error correction, audio and packet overhead; capped by `max_bitrate`). It becomes the stream's new ceiling for the adaptive controller too |
| `fec_percent` | Error correction, 1 to 255 %: used at once, and the level the controller returns to |
| `tuning` | `performance`, `balanced` or `quality`: the encoder preset, as `norishineTuning` at launch. Needs the `tuning` feature |
| `width`, `height` | Size of the streamed picture, together, even, 256 to 8192. Feature `session.video` |
| `fps` | Frame rate, 10 to 480; the host's capture starts again at it. Feature `session.video` |
| `codec` | `H.264`, `HEVC` or `AV1` (any case); 409 when the host doesn't stream it, or H.264 for an HDR stream. Feature `session.video` |

A new bitrate or preset rebuilds the encoder: the client gets one key frame, as after a
bitrate change by the controller. A new size, frame rate or codec also needs a new decoder on
the client: before the first frame in the new format, the control stream sends message
`0x3100` (Norishine extension; only to a client that asked for the change), little endian:
first frame number (32 bits), the `VIDEO_FORMAT_*` value of moonlight-common-c (32), width,
height and frame rate (16 each). Frames from that number on are in the new format and start
with a key frame. Norilight's moonlight-common-c switches its depacketizer there and asks the
client to rebuild its decoder. Answers as `GET /api/v1/session`. 400 for a malformed
body or a value out of range, 404 without a stream, 409 when the stream can't change (not
running yet, an encoder that can't be rebuilt during a stream, or `tuning` while
`client_tuning` is off).

## GET /api/v1/update
@copydoc confighttp::getUpdateV1()

Feature `update`. Needs `manage_read` on the HTTPS port. The host checks GitHub's release
list at most every ten minutes; `?refresh=1` checks again, at most once a minute.

| Field | Meaning |
| --- | --- |
| `current` | This host's version |
| `include_pre_releases` | The `notify_pre_releases` setting: whether nightlies are offered |
| `supported`, `unsupported_reason` | Whether this host can update itself (Windows, installed with the installer, not a development build), and why not |
| `checked_at`, `check_error` | Last check of GitHub, and why it failed |
| `available` | The newest release newer than this one, or null: `version`, `name`, `url` (release page), `published_at`, `prerelease`, `size` |
| `state` | `idle`, `downloading`, `installing` (the installer runs; the host stops and restarts), or `failed` |
| `error`, `target`, `progress` | Why it failed; the release being installed; `bytes` and `total` while downloading |
| `last_update` | The previous update, as the new host found it: `from`, `to`, `result` (`updated` or `failed`), `finished`, `error`; or null |

## POST /api/v1/update

`{"version": "<available.version>"}` starts the update. Needs `manage_write` on the HTTPS
port (and the CSRF header on the web UI port). The answer is 202 with `state` and `target`.
It is 409 when the host can't update itself, an update is already running, or `version`
isn't the one on offer (so a client can't install something it didn't show), and 400 for a
malformed body.

The host downloads installers only from this project's GitHub releases, refuses one unless
its size and SHA-256 match the `digest` GitHub publishes for it, and runs
`scripts\apply-update.ps1` outside the service's process group. The script runs the
installer silently, starts the service again if it isn't running afterwards, and writes
`config\updates\update-status.json` and `update.log`.

## Apps, devices, streams and displays (/api/v1)

The same calls are on the web UI port (signed-in session, CSRF header) and on the HTTPS
port for paired devices (permissions as above). Answers are JSON with `status`; failures
have `error` (one message), or `errors` (one message per field) for a 400. An unknown ID
is 404. Checks: `tests/unit/test_manage_api.cpp`, `scripts/e2e/manage_check.py`.

| Call | Does |
| --- | --- |
| `GET /api/v1/apps` | `apps` as stored in `apps.json` (each with `uuid`, and `running`), `running_app` (UUID or `""`) |
| `POST /api/v1/apps` | Add an app (the body as listed; a `uuid` in it is ignored). Answers its new `uuid` |
| `PUT /api/v1/apps/{uuid}` | Replace an app. Fields this host doesn't know are kept as they are |
| `DELETE /api/v1/apps/{uuid}` | Remove an app |
| `POST /api/v1/apps/order` | `{"order": [uuid, ...]}`: these first, the rest after in their current order |
| `POST /api/v1/apps/{uuid}/launch` | Start an app on the host without a stream |
| `POST /api/v1/apps/close` | Close the running app |
| `GET /api/v1/devices` | `devices`: paired devices as in the web UI (`uuid`, `name`, `perm`, `display_mode`, `do`, `undo`, `connected`, ...), and `this_device` for the one asking |
| `PATCH /api/v1/devices/{uuid}` | Change some of `name`, `display_mode`, `perm`, `enable_legacy_ordering`, `allow_client_commands`, `always_use_virtual_display`, `do`, `undo`. Answers the `device`. A device can't change its own `perm` (403) |
| `DELETE /api/v1/devices/{uuid}` | Unpair a device (it may be the one asking) |
| `POST /api/v1/devices/{uuid}/disconnect` | End the device's stream; `disconnected` says whether it had one |
| `GET /api/v1/displays` | `displays`: `id`, `name`, `friendly_name`, `active`, `selected` (the `output_name` setting), and for active ones `width`, `height`, `refresh_rate`, `scale`, `primary`, `hdr` (null if unknown) |
| `POST /api/v1/displays/reset` | Forget display changes Norishine would otherwise undo later |

Adding, changing, reordering or removing apps reloads the app list, which closes a
running app, as saving in the web UI does; the answer says so in `closed_running_app`.
Active streams are in `GET /api/v1/status` (`sessions`).

The web UI port also has `GET /api/v1/apps/{uuid}/image`: the app's picture as PNG, or 404
when it has none of its own (the web UI then draws a lettered tile). Paired clients get
pictures from `/appasset` as before.

## GET /api/v1/setup
@copydoc confighttp::getSetupV1()

`pending` is true from the first account until the wizard is finished or skipped; while it
is, `/` opens `/setup`. `other_host` is `{"name", "installed"}` or null.

## POST /api/v1/setup/encoder
@copydoc confighttp::checkEncoderV1()

Answers `{"status", "encoder", "h264", "hevc", "av1", "hdr"}`; 409 while a stream runs.

## POST /api/v1/setup/import
@copydoc confighttp::importSettingsV1()

Runs `scripts/other-hosts.ps1 -Action import-settings`. 409 once the wizard is finished.

## POST /api/v1/setup/done
@copydoc confighttp::finishSetupV1()

## POST /api/v1/reset
@copydoc confighttp::resetV1()

Web UI only (session cookie and CSRF token): it is not part of the management API on the
GameStream port, so no paired client can reset the host.

## POST /api/covers/upload
@copydoc confighttp::uploadCover()

## GET /api/logs
@copydoc confighttp::getLogs()

## POST /api/password
@copydoc confighttp::savePassword()

## POST /api/pin
@copydoc confighttp::savePin()

## POST /api/reset-display-device-persistence
@copydoc confighttp::resetDisplayDevicePersistence()

## POST /api/restart
@copydoc confighttp::restart()

<div class="section_buttons">

| Previous                                    |                                  Next |
|:--------------------------------------------|--------------------------------------:|
| [Performance Tuning](performance_tuning.md) | [Troubleshooting](troubleshooting.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>

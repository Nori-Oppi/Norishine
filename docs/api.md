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
this host), `default`, `restart_required`, `label` and `description` (keys into the web UI
locale files), and where it applies `min`, `max` and `choices` (an empty choice means
automatic). The table is `src/config_schema.cpp`.

Optional fields, added later (clients must not require them):

- `depends_on`: `{"key": ..., "values": [...]}`. The setting only has an effect while that
  other setting (always earlier in the list, in the same category) has one of these values.
  The web UI indents such settings and says when they have no effect.
- `value_titles`: readable names for values, keyed by the value as text (`"0"` for an
  integer, `""` for automatic). Choices always have them; integers with a few named values
  (`hevc_mode`, `min_log_level`, ...) have one per value and can be shown as a list.

`categories` lists `id` and `title` in display order; related categories also have `group`
and `group_title` (the encoder categories, under "Encoders"). Category ids and the order of
settings can change between versions, so render them from the schema rather than by name.

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
| `manage_read` (Read Host Settings) | `0x10000000` | every `GET` below: status, settings, apps, devices, displays |
| `manage_write` (Change Host Settings) | `0x20000000` | the above, and every call that changes something: settings, `POST /api/v1/restart` (settings apply after a restart), apps, devices, streams, displays |

`manage_write` is full control of the host: settings, app and device commands run
programs on it. Grant it only to devices you trust as much as the host's own account.

Without the permission the answer is 403 with a JSON error. `serverinfo` (HTTP and HTTPS)
carries `<Norishine><ApiVersion>1</ApiVersion><Features>status,config.schema,config.read,config.write,restart,apps,apps.write,devices,devices.write,sessions.write,displays</Features></Norishine>`
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
`AV1`), `hdr` and `state`. The same object (plus `device`) is what paired clients get from
`GET /api/v1/status` on the HTTPS port.

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

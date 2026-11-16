# API

Sunshine has a RESTful API which can be used to interact with the service.

Unless otherwise specified, every call needs a signed-in session: `POST /api/login` with
`{"username": ..., "password": ...}` sets two cookies, `auth` (HttpOnly, the session) and
`csrf` (readable by the pages). Every call that changes something (any method other than
GET) must also send the `csrf` value in an `X-CSRF-Token` header, and a browser `Origin`, if
present, must be this server; otherwise the answer is 403. `POST /api/logout` ends the
session. Several browsers can be signed in at once; sessions last 15 days and end when the
host restarts or the password changes.

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
| `manage_read` (Read Host Settings) | `0x10000000` | `GET /api/v1/status`, `GET /api/v1/config/schema`, `GET /api/v1/config` |
| `manage_write` (Change Host Settings) | `0x20000000` | the above, `PATCH /api/v1/config`, and `POST /api/v1/restart` (settings apply after a restart) |

Without the permission the answer is 403 with a JSON error. `serverinfo` (HTTP and HTTPS)
carries `<Norishine><ApiVersion>1</ApiVersion><Features>status,config.schema,config.read,config.write,restart</Features></Norishine>`,
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

# API

Sunshine has a RESTful API which can be used to interact with the service.

Unless otherwise specified, authentication is required for all API calls. You can authenticate using
basic authentication with the admin username and password.

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

Each setting has `key`, `category` (the web UI tab), `type` (`boolean`, `integer`,
`number`, `string`, `choice`, `path`, `prep_commands`, `server_commands`, `keybindings`,
`mode_remapping`), `tier` (`basic`, `advanced`, `dangerous`), `platforms`, `available` (on
this host), `default`, `restart_required`, `label` and `description` (keys into the web UI
locale files), and where it applies `min`, `max` and `choices` (an empty choice means
automatic). The table is `src/config_schema.cpp`.

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
| `manage_write` (Change Host Settings) | `0x20000000` | the above, and `PATCH /api/v1/config` |

Without the permission the answer is 403 with a JSON error. `serverinfo` (HTTP and HTTPS)
carries `<Norishine><ApiVersion>1</ApiVersion><Features>status,config.schema,config.read,config.write</Features></Norishine>`,
and `Permission` shows what the requesting device may do. `GET /api/v1/status` returns the
host name, version, platform, unique ID, running app, number of active sessions, and the
requesting device's name and permissions. `scripts/e2e/manage_check.py` checks all of it
against a dev host.

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

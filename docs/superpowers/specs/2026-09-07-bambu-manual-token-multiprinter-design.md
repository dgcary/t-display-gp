# Bambu Manual Token + Multi-Printer Design

## Goal

Replace device-side Bambu account/password/SMS/TFA login as the primary setup path with a simpler manual Cloud token flow, while supporting multiple locally saved printers and instant active-printer switching from the :8081 portal.

## Architecture

The T-Display stores only the Bambu region, access token, derived Cloud user ID, a bounded local printer list, and one active printer index/serial. The portal never needs the Bambu account password or one-time codes. On save, firmware validates the token locally by parsing its JWT user ID when possible, stores the token, persists the printer list, and reconnects the existing background Cloud MQTT service to the active printer.

Cloud HTTPS printer discovery becomes an explicit convenience action only. Normal boot, normal rendering, and switching between saved printers do not call Bambu HTTPS. The persistent read-only MQTT contract remains `username=cloudUserId`, `password=accessToken`, subscribe `device/<serial>/report`, with the sole `pushall` request publish.

## Configuration

Persist a schema-v2 Bambu config with:

- `enabled`
- `region`
- `accessToken`
- `cloudUserId`
- bounded printer list, initially max 4 entries, each `serial` + optional `name`
- active printer selection

Legacy schema-v1 data is accepted and migrated in memory: existing token/userId/single printer become the first printer entry where valid. Legacy account/password fields are not used by the new portal flow and should not be required for validation.

## Portal

The Bambu section exposes:

- region selector
- secret token input (`password`-style; blank preserves current token)
- four local printer rows (`name`, `serial`)
- active-printer selector populated from locally configured rows
- `Save and switch`
- optional `Fetch my printers with token` action
- clear credentials action

No account/password/SMS/TFA controls remain in the primary UI. Status exposes token presence, active printer metadata, MQTT/session state, and never token contents.

## Active printer switching

A portal config replacement increments the existing external config revision. The MQTT task notices the revision, disconnects any old broker session, clears connection attempt state, and reconnects using the newly active serial. Cached state for the old printer must not be treated as data for the new active printer; switching resets the visible Bambu state to disconnected/empty until new report data arrives.

## Cloud identity

On token save, first attempt `extractBambuUserIdFromJwt()` locally. If the token is not a parseable JWT, a profile HTTPS fallback may be used only when explicitly saving/testing the token. Normal boot should use persisted `cloudUserId` and avoid HTTP when both token and userId are present.

## Printer discovery

`Fetch my printers with token` is user-triggered only. It may call the existing Cloud device-list endpoint and replace/populate local printer rows, guarded by the config revision. `/api/bambu/printers` becomes local-cache/status only and must not implicitly fetch Cloud merely because its cache is empty.

## Security

- Never log/return/persist token outside Bambu config NVS.
- Never use `setInsecure()` in Bambu HTTPS or MQTT.
- Remove password/code/TFA challenge handling from the primary portal path.
- Preserve read-only MQTT behavior; no control commands.
- Local :8081 remains trusted-LAN HTTP, so the page warns that pasted token crosses the LAN in cleartext.

## Acceptance

- Manual token + serial can be saved without account/password/SMS.
- JWT token derives user ID locally and survives reboot.
- Up to four printers persist locally.
- Active printer can be changed in :8081 without re-entering token and without Cloud HTTP discovery.
- Switching reconnects MQTT to the selected printer and does not show stale old-printer data as the new printer.
- Explicit discovery may populate multiple printers but failure does not erase the saved local list.
- Native tests, validators, Bad Apple validation, and `pio run -e lilygo-t-display-s3` all pass on exact final HEAD.

# Persistent Multi-Printer MQTT Slots — Design Amendment

## Context

Physical acceptance of `5aba569ad08b13d20324f2585cb312f7ab0d179a` proved the BambuHelper-aligned loop-driven runtime removed the previous CPU0 watchdog failure, but active-printer switching remained slow every time. Root-cause comparison with upstream `Keralots/BambuHelper` showed the missing architectural behavior: upstream keeps one MQTT/TLS connection per active printer slot, while T-Display GP still had one global MQTT connection and rebuilt it on each `activePrinterIndex` change.

## Goal

Make printer switching a local presentation/NVS operation. Configured printers maintain independent background Cloud MQTT connections and independent state caches so A↔B does not pay TLS/MQTT/subscription/pushall latency when both slots are already online.

## Runtime model

Capacity remains `BambuConfigLimits::PRINTER_COUNT == 4`.

```text
BambuMqttService
  conns_[4]  -> per-slot WiFiClientSecure/PubSubClient/status/backoff/pushall
  states_[4] -> per-slot BambuState cache
```

`process(nowMs)` services every connected slot first. If connections are missing, it performs at most one potentially blocking connect attempt per Arduino loop pass, preferring the selected slot and then siblings.

Each slot keeps the existing BambuHelper-aligned connection contract: strict CA, TLS client timeout 15 s, PubSubClient 40960-byte buffer, keepalive 30 s, random `bblp_*` client ID, direct Cloud MQTT connect, per-serial report subscription, delayed read-only pushall, and 30/60/120 s per-slot reconnect backoff.

MQTT callback routing uses the report topic Serial to locate the slot; a report updates only `states_[slot]` and that slot's status.

## Switching semantics

`cycleActivePrinter(direction)` validates/wraps selection, persists the new `activePrinterIndex`, and updates `config_`. It must not disconnect a slot, clear cached states, or bump a connection-affecting revision.

`snapshot()` and `status()` return the currently selected slot's cached state/status. The UI may full-redraw presentation for a printer-name change, but network state remains untouched.

Portal config saves that change only active index and/or friendly names preserve all connections/caches. Changes to enablement, region, Token, Cloud user ID, printer count or serial set are connection-affecting and trigger runtime rebuild.

## Resource / scheduling constraints

Established slots do not hold `NetworkArbiter`. New/reconnect transactions acquire it, and only one connect attempt is started per loop pass. This keeps sibling MQTT loops serviced before another potentially slow Cloud handshake.

The implementation supports four configured slots, matching the project's schema and upstream PSRAM behavior, but initial physical acceptance is scoped to the user's two real printers. Four simultaneous Cloud MQTT/TLS connections are not considered hardware-validated until separately tested.

## Security / read-only boundary

No change: Manual Token only, strict CA, no `setInsecure()`, no password/SMS/TFA login, no automatic Token renewal, and only read-only `pushall` publish. No Token/User ID/Cookie/Authorization in logs.

## Verification

Static contracts require per-slot arrays, per-slot connect/disconnect/pushall, topic-to-slot routing, no singular global MQTT/TLS/state fields, and no network teardown inside `cycleActivePrinter()`.

Physical acceptance with two printers requires both slots online first, then >=10 A↔B switches with no fresh MQTT connect caused by switching, no state cross-contamination, persisted selection, >=10 minutes with no watchdog/panic/automatic reboot, stable heap, and Stock/Weather/HA coexistence under two persistent sockets.

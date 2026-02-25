# BrewOS Cloud Integration

The `espresso_machine_brewos` component connects your ESPHome espresso machine to the
[BrewOS cloud relay service](https://github.com/brewos-io/cloud), enabling remote access
from anywhere — even when Home Assistant is not reachable.

---

## Architecture

```
┌─────────────────┐   WebSocket (WSS)   ┌──────────────────┐   WebSocket   ┌────────────────┐
│   ESP32 device  │ ──────────────────► │  BrewOS Cloud    │ ────────────► │  Mobile / Web  │
│ (your machine)  │ ◄────────────────── │  (relay server)  │ ◄──────────── │      App       │
└─────────────────┘                     └──────────────────┘               └────────────────┘
        │
        │ calls
        ▼
┌─────────────────┐
│ EspressoMachine │  (brew_start / brew_stop / steam_start / … )
└─────────────────┘
```

The cloud service is a **pure WebSocket relay** — it never stores brew data, it only
forwards JSON messages between your device and the app.  Latency is typically < 100 ms.

---

## YAML Configuration

Add the following block to your ESPHome device configuration **after** the
`espresso_machine:` block:

```yaml
espresso_machine_brewos:
  url: https://cloud.brewos.io    # BrewOS cloud base URL (change for self-hosting)
  espresso_machine: my_espresso   # id: of your espresso_machine: block
```

That is all that is required.  Device credentials are managed automatically (see below).

---

## How It Works

### 1. Device Identity

On **first boot**, the connector:

1. Derives a **device ID** from the ESP32 chip MAC address in the form `BRW-XXXXXXXX`
   (8 hex chars — unique per device).
2. Generates a **cryptographically random 32-byte device key**, base64url-encoded.
3. Stores the key in NVS under the `brewos_sec` namespace (`devKey` key) so it
   persists across reboots and OTA updates.

On **subsequent boots**, the stored key is reloaded — the device always reconnects
with the same credentials.

### 2. Connection Flow

```
ESP32                              Cloud                            App
  │  WSS /ws/device?id=...&key=...   │                               │
  │ ──────────────────────────────►  │                               │
  │  { type: "connected" }           │                               │
  │ ◄──────────────────────────────  │                               │
  │                                  │  { type: "pico_status", … }   │
  │ ──────────────────────────────►  │ ──────────────────────────►   │
  │                                  │  { type: "brew_start" }       │
  │ ◄──────────────────────────────  │ ◄──────────────────────────   │
```

- The ESP32 sends a **status heartbeat** (`pico_status`) every **5 seconds**.
- The app sends **commands** that are forwarded to the ESP32.
- Disconnections are handled automatically with exponential back-off (5 s → 60 s).

### 3. Status Message

```json
{
  "type":        "pico_status",
  "mode":        "idle",
  "brew_state":  0,
  "steam_state": 0,
  "is_busy":     false,
  "powered_on":  false
}
```

| Field | Type | Values |
|---|---|---|
| `type` | string | always `"pico_status"` |
| `mode` | string | `"idle"`, `"brewing"`, `"steaming"`, `"flushing"` |
| `brew_state` | int | 0=idle, 1=heating, 2=pre_infusion, 3=brewing, 4=done, 5=cleanup |
| `steam_state` | int | 0=idle, 1=heating, 2=purging, 3=steaming, 4=cooling, 5=cleanup |
| `is_busy` | bool | `true` while brewing, steaming, or flushing |
| `powered_on` | bool | `true` after `machine_on()` |

### 4. Supported Commands

| `"type"` | Additional fields | Description |
|---|---|---|
| `brew_start` | — | Start an espresso shot |
| `brew_stop` | — | Stop the current shot immediately |
| `steam_start` | — | Start the steam sequence |
| `steam_stop` | — | Stop steaming |
| `machine_on` | — | Power the machine on |
| `machine_off` | — | Safely power the machine off |
| `flush` | `"volume_ml": <float>` | Pump N ml through the purge valve |

Example command JSON:
```json
{ "type": "flush", "volume_ml": 30 }
```

---

## Device Pairing

To link the device to your BrewOS account:

1. Flash the device with the `espresso_machine_brewos:` block configured.
2. The device connects to the cloud automatically once Wi-Fi is available.
3. Open the BrewOS app and tap **Add Device → Scan QR Code**.
4. Scan the QR code shown on the device's display (or at `http://<device_ip>/pairing`
   in the BrewOS web UI).
5. The app registers the device key with the cloud and the device is linked to your
   account.

See the full [Pairing & Sharing guide](https://github.com/brewos-io/cloud/blob/main/docs/Pairing_and_Sharing.md)
for user-to-user sharing and the token expiry details.

---

## Self-Hosting

You can run your own BrewOS cloud relay instead of using the managed service.  Simply
set `url:` to your own instance:

```yaml
espresso_machine_brewos:
  url: https://brewos.yourdomain.com
  espresso_machine: my_espresso
```

Follow the [Deployment Guide](https://github.com/brewos-io/cloud/blob/main/docs/Deployment.md)
to stand up the relay server on any VPS or home server.

---

## Offline / Local Operation

Cloud connectivity is **entirely optional**.  If the cloud is unreachable:

- The machine operates normally through Home Assistant (native ESPHome API).
- The connector logs a warning and retries with exponential back-off.
- No brew, steam, or safety function is affected.

To disable the cloud integration, comment out or remove the `espresso_machine_brewos:`
block from your YAML configuration.

---

## Security

| Mechanism | Detail |
|---|---|
| **Device key** | 32-byte random, base64url-encoded; stored in NVS; never transmitted in plaintext |
| **Transport** | WSS (TLS 1.2) for `https://` URLs; plain WS for `http://` |
| **Key rotation** | Supported via `key_rotate` cloud command (handled by BrewOS cloud) |
| **Offline access** | Cloud compromise does not affect local HA access |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `[W][brewos] Disconnected from BrewOS cloud` | Cloud unreachable / URL wrong | Check `url:` and network |
| Device never appears in app | Not yet paired | Follow the pairing steps above |
| Commands from app not reaching machine | WebSocket not connected | Check logs for connection status |
| Key lost after erase | NVS erased | Re-pair via QR code |

Enable `DEBUG` logging to see full WebSocket traffic:

```yaml
logger:
  level: DEBUG
```

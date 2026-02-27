# Meticulous Cloud API — Deep Exploration & ESP32 Integration Plan

> **Status: Research / Planning — no code written yet.**
> This document deeply explores the Meticulous espresso machine cloud/backend
> API, its architecture, and how our ESP32-based ESPHome espresso controller
> could integrate with it **directly from the device** — no relays or
> intermediate servers required.

---

## Table of Contents

1. [Why Meticulous?](#why-meticulous)
2. [Meticulous Platform Overview](#meticulous-platform-overview)
3. [Open-Source Ecosystem Map](#open-source-ecosystem-map)
4. [Backend Architecture Deep Dive](#backend-architecture-deep-dive)
5. [Profile Schema — Full Specification](#profile-schema--full-specification)
6. [Socket.IO Real-Time Protocol](#socketio-real-time-protocol)
7. [REST API Endpoints](#rest-api-endpoints)
8. [Client Libraries](#client-libraries)
9. [ESP32 Direct Integration — Architecture](#esp32-direct-integration--architecture)
10. [ESP32 Socket.IO Feasibility Analysis](#esp32-socketio-feasibility-analysis)
11. [Profile Translation Layer](#profile-translation-layer)
12. [Shot Telemetry — Streaming to Meticulous Cloud](#shot-telemetry--streaming-to-meticulous-cloud)
13. [Authentication & Device Registration](#authentication--device-registration)
14. [Integration Architecture Options](#integration-architecture-options)
15. [Recommended Architecture](#recommended-architecture)
16. [Implementation Roadmap](#implementation-roadmap)
17. [Risks & Challenges](#risks--challenges)
18. [Open Questions](#open-questions)

---

## Why Meticulous?

The Meticulous espresso machine has the **most complete and openly documented**
cloud API of any espresso platform we've surveyed:

- **Open-source backend** — full Python source on GitHub
- **Open profile schema** — JSON Schema + RFC document, community-driven
- **Open client libraries** — Python (`pyMeticulous`) and TypeScript
  (`@meticulous-home/espresso-api`)
- **Community OpenAPI documentation** — REST endpoints fully specified
- **Socket.IO real-time protocol** — bidirectional telemetry streaming
- **MCP server integrations** — AI assistant compatibility already built
- **Home Assistant integration** — proven HA add-on with MQTT auto-discovery

If we can make our ESP32 device speak the Meticulous protocol directly, our
users get access to the entire Meticulous ecosystem: profile library, shot
analytics, community features, and the growing third-party tooling — without
needing to buy a Meticulous machine.

---

## Meticulous Platform Overview

### The Machine

| Aspect | Details |
|---|---|
| **Hardware** | Robotic lever espresso machine with integrated scale |
| **Processor** | Dual-architecture: Linux SBC (runs Python backend) + ESP32 radio module (WiFi/BLE, FCC ID 2BRNQ-E01G) |
| **Connectivity** | Dual-band WiFi (2.4/5 GHz, 802.11 a/b/g/n/ac), Bluetooth 5.3 |
| **Sensors** | Pressure, flow meter, temperature (group + boiler), weight (built-in scale) |
| **Backend** | Python (Socket.IO + REST), runs locally on the machine's Linux SBC |
| **Cloud** | meticuloushome.com — cloud profiles, firmware updates, account management |

### Key Insight

The Meticulous machine runs its backend **locally on the device**. The
"Meticulous API" is not just a remote cloud — it's a local HTTP + Socket.IO
server running on the machine's own Linux SBC (the ESP32 handles WiFi/BLE
radio, while the Python backend runs on a more capable Linux processor). This
means:

1. The API was designed for **local network** communication (low latency).
2. Third-party integrations (HA add-on, MCP servers) connect to the machine
   directly on the LAN.
3. Cloud features (profile sync, account management) go through
   meticuloushome.com, but the real-time machine API is local.

**For our integration**, this means we need to understand two distinct layers:
- **Local API** (Socket.IO + REST on the machine) — the primary interface.
- **Cloud API** (meticuloushome.com) — profile storage, user accounts, community.

---

## Open-Source Ecosystem Map

All of these repositories are publicly accessible on GitHub:

| Repository | Purpose | Language |
|---|---|---|
| [`MeticulousHome/meticulous-backend`](https://github.com/MeticulousHome/meticulous-backend) | Main backend: API, machine control, profiles, telemetry | Python |
| [`MeticulousHome/espresso-profile-schema`](https://github.com/MeticulousHome/espresso-profile-schema) | JSON Schema + RFC for profile format | JSON/Markdown |
| [`MeticulousHome/pyMeticulous`](https://github.com/MeticulousHome/pyMeticulous) | Python API wrapper (pip: `pyMeticulous`) | Python |
| [`MeticulousHome/meticulous-typescript-api`](https://github.com/MeticulousHome/meticulous-typescript-api) | TypeScript/Axios API client (npm: `@meticulous-home/espresso-api`) | TypeScript |
| [`ohheyitsdave/Meticulous-OpenAPI-Documentation`](https://github.com/ohheyitsdave/Meticulous-OpenAPI-Documentation) | Community OpenAPI spec for the REST API | YAML |
| [`nickwilsonr/meticulous-addon`](https://github.com/nickwilsonr/meticulous-addon) | Home Assistant add-on (Socket.IO → MQTT bridge) | Python |
| [`twchad/meticulous-mcp`](https://github.com/twchad/meticulous-mcp) | MCP server for AI assistant integration | TypeScript |
| [`hessius/MeticAI`](https://github.com/hessius/MeticAI) | AI-powered shot analysis + MCP + web server | Python |

### What This Means for Us

The entire protocol is reverse-engineerable from source code. We don't need to
guess at packet formats or authentication flows — it's all in the open-source
repositories. This is **radically better** than trying to integrate with
Decent (proprietary) or most other platforms.

---

## Backend Architecture Deep Dive

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                  Meticulous Machine (local)                      │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              meticulous-backend (Python)                  │   │
│  │                                                          │   │
│  │  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐   │   │
│  │  │  Socket.IO   │  │   REST API   │  │   Machine     │   │   │
│  │  │   Server     │  │   (HTTP)     │  │   Control     │   │   │
│  │  │              │  │              │  │               │   │   │
│  │  │ • Telemetry  │  │ • Profiles   │  │ • PID loops   │   │   │
│  │  │ • Events     │  │ • Actions    │  │ • Actuators   │   │   │
│  │  │ • Commands   │  │ • Settings   │  │ • Sensors     │   │   │
│  │  └──────┬───────┘  └──────┬───────┘  └───────┬───────┘   │   │
│  │         │                 │                   │           │   │
│  │         └────────────┬────┘                   │           │   │
│  │                      │                        │           │   │
│  │              ┌───────▼────────┐    ┌──────────▼─────────┐│   │
│  │              │  Profile Store │    │  Hardware drivers   ││   │
│  │              │  (JSON files)  │    │  (heater, pump,    ││   │
│  │              │                │    │   valve, scale)     ││   │
│  │              └────────────────┘    └────────────────────┘│   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
└──────────────────┬───────────────────────────────────────────────┘
                   │ WiFi (LAN)
                   │
    ┌──────────────┼──────────────────────────┐
    │              │                          │
┌───▼───┐   ┌─────▼──────┐   ┌──────────────▼──────────┐
│ Phone │   │ HA Add-on  │   │  meticuloushome.com     │
│  App  │   │ (SIO→MQTT) │   │  (Cloud)                │
│       │   │            │   │  • Profile library       │
│       │   │            │   │  • User accounts         │
│       │   │            │   │  • OTA firmware           │
└───────┘   └────────────┘   └─────────────────────────┘
```

### Backend Modules (from source analysis)

| Module | Role |
|---|---|
| `api/api.py` | Main API router — Socket.IO + REST endpoint registration |
| `api/action.py` | Action dispatcher — brew start/stop/continue, preheat, tare |
| `api/machine.py` | Machine state management, sensor reads, control loops |
| `api/profiles.py` | Profile CRUD — load, save, list, validate |
| `api/notifications.py` | Push notifications and event broadcasting |
| `api/wifi.py` | WiFi configuration and network management |
| `ble_gatt.py` | Bluetooth GATT server for BLE scale/app connections |
| `heater_actuator.py` | Heater PID control and SSR driving |
| `backlight_controller.py` | Display/LED backlight management |

---

## Profile Schema — Full Specification

The Meticulous profile schema is the **most expressive** profile format in the
espresso ecosystem. Understanding it is critical for compatibility.

Source: [`MeticulousHome/espresso-profile-schema`](https://github.com/MeticulousHome/espresso-profile-schema)

### Top-Level Structure

```json
{
  "name": "My Profile",
  "id": "uuid-string",
  "author": "username",
  "author_id": "uuid",
  "display": {
    "accentColor": "#FF6B35",
    "image": "url-or-base64"
  },
  "variables": [ ... ],
  "temperature": 93.0,
  "final_weight": 36.0,
  "stages": [ ... ]
}
```

### Variables

Variables allow profiles to be parametric — users can tweak values without
editing the stage definitions:

```json
"variables": [
  {
    "name": "Pressure",
    "key": "pressure_1",
    "type": "pressure",
    "value": 9.0
  },
  {
    "name": "Preinfusion Flow",
    "key": "pi_flow",
    "type": "flow",
    "value": 4.0
  }
]
```

Variables are referenced in stage definitions with `$` prefix (e.g.,
`"$pressure_1"`).

### Stages

Each stage is a distinct phase of the extraction (preinfusion, infusion,
declining pressure, etc.):

```json
{
  "name": "Preinfusion",
  "key": "stage_1",
  "type": "flow",
  "dynamics": {
    "points": [[0, 4.0]],
    "over": "time",
    "interpolation": "linear"
  },
  "exit_triggers": [
    { "type": "time", "value": 30, "relative": true, "comparison": ">=" },
    { "type": "weight", "value": 0.3, "relative": true, "comparison": ">=" },
    { "type": "pressure", "value": "$pressure_1", "relative": false, "comparison": ">=" }
  ],
  "limits": [
    { "type": "flow", "value": 3.0 }
  ]
}
```

### Stage Fields Explained

| Field | Type | Description |
|---|---|---|
| `name` | string | Human-readable stage name |
| `key` | string | Unique identifier within the profile |
| `type` | enum | Control mode: `"flow"`, `"pressure"` |
| `dynamics` | object | How the controlled variable changes over time |
| `dynamics.points` | `[[x, y], ...]` | Setpoint curve: x = time/weight, y = target value |
| `dynamics.over` | enum | X-axis: `"time"` (seconds), `"weight"` (grams) |
| `dynamics.interpolation` | enum | `"linear"`, `"catmull_rom"`, `"bezier"` |
| `exit_triggers` | array | Conditions that end this stage (first match wins) |
| `exit_triggers[].type` | enum | `"time"`, `"weight"`, `"pressure"`, `"flow"` |
| `exit_triggers[].value` | number/string | Target value (or `"$variable_key"` reference) |
| `exit_triggers[].relative` | boolean | `true` = relative to stage start; `false` = absolute |
| `exit_triggers[].comparison` | enum | `">="`, `"<="`, `"=="` |
| `limits` | array | Safety constraints on other variables during this stage |

### What This Schema Can Express

- ✅ Classic 9-bar flat pressure profile
- ✅ Lever-style declining pressure (ramping `dynamics.points`)
- ✅ Flow profiling (Slayer-style, blooming, turbo)
- ✅ Pressure-then-flow transitions (multi-stage)
- ✅ Weight-based exit (gravimetric stop)
- ✅ Time-based preinfusion with pressure trigger exit
- ✅ Parametric profiles (adjust via variables, share same structure)
- ✅ Multi-point curves with interpolation (pressure ramps, S-curves)

### Profile Compatibility Assessment

| Our Feature | Meticulous Equivalent | Gap? |
|---|---|---|
| Brew temperature | `temperature` field | ✅ Direct mapping |
| Target weight/volume | `final_weight` field | ✅ Direct mapping |
| Pressure setpoint | `type: "pressure"` stage with `dynamics` | ✅ Direct mapping |
| Flow setpoint | `type: "flow"` stage with `dynamics` | ✅ Direct mapping |
| Preinfusion time | `exit_triggers` with `type: "time"` | ✅ Direct mapping |
| Preinfusion pressure trigger | `exit_triggers` with `type: "pressure"` | ✅ Direct mapping |
| Weight-based stop | `final_weight` + `exit_triggers` | ✅ Direct mapping |
| PID tuning parameters | Not in profile schema (machine-level) | ⚠️ We handle PID separately |
| Grinder settings | Not in profile schema (recipe-level) | ⚠️ Need recipe wrapper |
| Valve control | Not in profile schema (hardware-level) | ⚠️ Our abstraction is different |

**Conclusion:** The Meticulous profile schema covers ~80% of our brew profile
needs. The gaps (grinder, valve, PID) are hardware-specific and shouldn't be
in a portable profile format anyway. We can adopt Meticulous profiles as a
**portable interchange format** while keeping our own hardware-specific
extensions.

---

## Socket.IO Real-Time Protocol

The Meticulous backend uses **Socket.IO** (not raw WebSocket) for all
real-time communication. Socket.IO adds event naming, automatic reconnection,
room-based broadcasting, and acknowledgments on top of WebSocket transport.

### Connection

```
ws://<machine_ip>:8080/socket.io/?EIO=4&transport=websocket
```

The Socket.IO handshake negotiates transport (WebSocket preferred, HTTP
long-polling fallback).

### Event Categories

Based on analysis of the backend source, HA add-on, and client libraries:

#### Telemetry Events (Machine → Client)

| Event | Payload | Frequency | Description |
|---|---|---|---|
| `sensor_update` | `{ pressure_brew, pressure_boiler, temperature_brew, temperature_boiler, flow_rate, weight, shot_time, machine_status }` | 5–10 Hz during brew | Live sensor readings |
| `brew_start` | `{ profile_id, timestamp }` | Once per shot | Shot started |
| `brew_end` | `{ profile_id, timestamp, total_time, final_weight }` | Once per shot | Shot completed |
| `status_update` | `{ state, ... }` | On change | Machine state transitions (idle, heating, brewing, steaming) |
| `profile_change` | `{ profile }` | On change | Active profile changed |
| `machine_info` | `{ firmware, serial, ... }` | On connect | Device identity |

#### Command Events (Client → Machine)

| Event | Payload | Description |
|---|---|---|
| `action:start` | `{}` | Start brewing with loaded profile |
| `action:stop` | `{}` | Stop current brew |
| `action:tare` | `{}` | Tare the scale |
| `action:preheat` | `{ temperature }` | Start preheating |
| `profile:load` | `{ profile_json }` | Load a profile for execution |
| `profile:list` | `{}` | List available profiles |
| `settings:update` | `{ key, value }` | Update a machine setting |

#### Delta Filtering

The Home Assistant add-on implements delta thresholds to reduce message volume:

| Sensor | Default Delta | Description |
|---|---|---|
| Temperature | 0.5 °C | Skip updates smaller than 0.5°C change |
| Pressure | 0.2 bar | Skip updates smaller than 0.2 bar change |
| Weight | 0.1 g | Skip updates smaller than 0.1g change |
| Flow | 0.1 ml/s | Skip updates smaller than 0.1 ml/s change |

---

## REST API Endpoints

From community OpenAPI documentation
([`ohheyitsdave/Meticulous-OpenAPI-Documentation`](https://github.com/ohheyitsdave/Meticulous-OpenAPI-Documentation)):

### Core Endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/profiles` | List all stored profiles |
| `POST` | `/api/v1/profiles` | Upload a new profile (JSON body) |
| `GET` | `/api/v1/profiles/{id}` | Get a specific profile by ID |
| `DELETE` | `/api/v1/profiles/{id}` | Delete a profile |
| `POST` | `/api/v1/brew/start` | Start brewing with current profile |
| `POST` | `/api/v1/brew/stop` | Stop current brew |
| `GET` | `/api/v1/status` | Get current machine state + sensor readings |
| `POST` | `/api/v1/settings` | Update device or brewing settings |
| `POST` | `/api/v1/auth/login` | Authenticate (get session token) |

### Authentication

- **Local:** No authentication required on LAN (machine trusts local network).
- **Cloud (meticuloushome.com):** OAuth2/JWT token-based authentication. Users
  authenticate with their Meticulous account; tokens are scoped to device and
  profile operations.

---

## Client Libraries

### pyMeticulous (Python)

```python
from meticulous.api import Api, Profile

api = Api(base_url="http://192.168.1.100:8080/")

# List profiles
profiles = api.list_profiles()

# Load a profile from JSON
profile = Profile(**profile_json)
api.load_profile_from_json(profile)

# Start brewing
api.execute_action('start')

# Get last profile
last = api.get_last_profile()
```

### meticulous-typescript-api (TypeScript)

```typescript
import { MeticulousApi } from '@meticulous-home/espresso-api';

const api = new MeticulousApi({ baseUrl: 'http://192.168.1.100:8080' });

const profiles = await api.listProfiles();
await api.loadProfile(profileJson);
await api.executeAction('start');
```

---

## ESP32 Direct Integration — Architecture

The user's requirement is clear: **reach the Meticulous cloud/API directly
from the ESP32 device, without relays**. Let's analyze how this would work.

### What "Direct Integration" Means

Our ESP32 device would:

1. **Speak the Meticulous profile format** — import/export profiles compatible
   with the Meticulous ecosystem.
2. **Stream telemetry in Meticulous format** — so Meticulous-compatible tools
   (HA add-on, MeticAI, MCP servers) can connect to our machine as if it were
   a real Meticulous.
3. **Connect to meticuloushome.com cloud** — for profile sync, shot logging,
   and community features.
4. **Accept commands via Socket.IO** — so Meticulous-compatible apps can
   control our machine.

### Two Integration Directions

```
Direction A: Our ESP32 ACTS AS a Meticulous machine
──────────────────────────────────────────────────

┌──────────────────────┐
│    Our ESP32          │
│  (ESPHome firmware)   │
│                       │
│  ┌─────────────────┐  │      Meticulous-compatible
│  │ Meticulous      │  │      apps/tools connect to us
│  │ Compatibility   │──┼──────────────────────────┐
│  │ Layer           │  │                          │
│  │                 │  │                          │
│  │ • Socket.IO srv │  │      ┌───────────────────▼──────┐
│  │ • REST endpoints│  │      │  HA Add-on              │
│  │ • Profile parser│  │      │  MeticAI                │
│  └─────────────────┘  │      │  MCP Servers            │
│                       │      │  Meticulous Phone App?  │
└──────────────────────┘      └──────────────────────────┘


Direction B: Our ESP32 CONNECTS TO Meticulous Cloud
───────────────────────────────────────────────────

┌──────────────────────┐       ┌───────────────────────┐
│    Our ESP32          │       │  meticuloushome.com   │
│  (ESPHome firmware)   │       │  (Cloud)              │
│                       │  HTTP │                       │
│  ┌─────────────────┐  │ ───► │  • Profile library    │
│  │ Meticulous      │  │      │  • Shot storage       │
│  │ Cloud Client    │  │ ◄─── │  • Community          │
│  │                 │  │      │  • Account mgmt       │
│  └─────────────────┘  │      └───────────────────────┘
│                       │
└──────────────────────┘
```

---

## ESP32 Socket.IO Feasibility Analysis

### The Challenge

The Meticulous API uses **Socket.IO**, not raw WebSocket. Socket.IO has its
own handshake, packet framing, and event system on top of WebSocket. The
ESP32 Arduino ecosystem has limited Socket.IO support.

### Available ESP32 Socket.IO Libraries

| Library | Status | Socket.IO Version | Notes |
|---|---|---|---|
| `SocketIoClient` (Arduino) | Maintained | v2.x (partial) | Basic emit/on; missing v4 features |
| `ArduinoWebSockets` (Links2004) | Maintained | WebSocket only | No Socket.IO framing |
| `esp_websocket_client` (ESP-IDF) | Official | WebSocket only | Low-level, no Socket.IO |

### Socket.IO v4 Protocol Requirements

The Meticulous backend likely uses Socket.IO v4 (Engine.IO v4). Key protocol
features we need:

| Feature | ESP32 Support | Notes |
|---|---|---|
| WebSocket transport | ✅ | All libraries support this |
| Engine.IO handshake | ⚠️ | Must implement HTTP polling → WS upgrade |
| Packet framing (`0`, `2["event",data]`, `42["event",data]`) | ⚠️ | Need manual implementation |
| Ping/pong keep-alive | ⚠️ | Need to handle `2` (ping) / `3` (pong) packets |
| Event emission | ⚠️ | Need to format `42["event_name",{payload}]` |
| Acknowledgments | ❌ | Complex; may need to skip |
| Binary events | ❌ | Not needed for our use case |
| Namespaces | ⚠️ | Default namespace (`/`) should suffice |

### RAM and Performance Constraints

| Concern | Assessment |
|---|---|
| WebSocket connection memory | ~2–5 KB per connection (headers, buffers) |
| JSON parsing overhead | ~1–3 KB heap for typical sensor payloads |
| Concurrent connections | ESP32 can handle 3–5 WebSocket connections safely |
| TLS overhead | +15–20 KB for HTTPS/WSS (needed for cloud) |
| Total estimated overhead | ~10–25 KB additional heap for Meticulous compatibility layer |

**Verdict:** Feasible. We need both roles:

- **Socket.IO client** (Direction B) — for connecting to Meticulous cloud or
  a local Meticulous backend instance if available.
- **Socket.IO server** (Direction A) — for emulating a Meticulous machine on
  LAN so that Meticulous-compatible tools can connect to our ESP32.

The protocol is simple enough to implement the subset we need for both roles,
rather than relying on existing Arduino libraries (which are outdated or
incomplete).

### Recommended Approach: Minimal Socket.IO v4 Implementation

**Client mode** (connecting to Meticulous cloud/backend):

```
1. HTTP GET /socket.io/?EIO=4&transport=polling  → get session ID (sid)
2. WebSocket connect to /socket.io/?EIO=4&transport=websocket&sid=...
3. Send "2probe" → receive "3probe" (transport upgrade)
4. Send "5" (upgrade complete)
5. Handle ping ("2") → respond pong ("3")
6. Emit events: send '42["event_name",{json_payload}]'
7. Receive events: parse '42["event_name",{json_payload}]'
```

Client implementation is ~200–300 lines of C++ wrapping the ESP-IDF WebSocket
client.

**Server mode** (accepting connections from HA add-on, MeticAI, etc.):

```
1. Serve HTTP GET /socket.io/?EIO=4&transport=polling → return sid + handshake
2. Accept WebSocket upgrade on /socket.io/?EIO=4&transport=websocket&sid=...
3. Respond to "2probe" with "3probe", then expect "5" (upgrade complete)
4. Send ping ("2") periodically → expect pong ("3")
5. Broadcast events: send '42["event_name",{json_payload}]' to all connected clients
6. Receive commands: parse '42["event_name",{json_payload}]' from clients
7. Manage client connection lifecycle (connect, disconnect, timeout)
```

Server implementation is more complex (~500–800 lines of C++) due to
multi-client management, connection lifecycle, and HTTP upgrade handling.
Both roles share the same packet framing code.

---

## Profile Translation Layer

To be fully compatible, we need to translate between our internal profile
format and the Meticulous JSON schema.

### Meticulous → Our Format

```
Meticulous Profile                    Our Internal Profile
─────────────────                    ────────────────────

temperature: 93.0        ──────►    target_temperature: 93°C
final_weight: 36.0       ──────►    flow_max: 36ml (weight-based stop)

stages[0]:                           brew phase:
  type: "flow"           ──────►      mode: FLOW_CONTROL
  dynamics.points[[0,4]] ──────►      flow_target: 4.0 ml/s
  exit_triggers:
    time >= 30           ──────►      preinfusion_time: 30s
    pressure >= $p1      ──────►      preinfusion_pressure: {from variable}

stages[1]:                           brew phase:
  type: "pressure"       ──────►      mode: PRESSURE_CONTROL
  dynamics.points[[0,9]] ──────►      pressure_target: 9.0 bar
  limits.flow <= 3       ──────►      flow_limit: 3.0 ml/s
```

### Our Format → Meticulous

The reverse translation packages our brew settings into the Meticulous JSON:

```cpp
String to_meticulous_profile(const BrewProfile& profile) {
  // Build stages array from our brew phases
  // Map temperature, weight targets to Meticulous fields
  // Generate exit_triggers from our preinfusion/stop conditions
  // Return valid Meticulous JSON
}
```

### Limitations

Not all Meticulous features have equivalents in our system:

| Meticulous Feature | Our Equivalent | Status |
|---|---|---|
| Multi-point dynamics curves | Single setpoint per phase | ⚠️ Simplify to nearest setpoint |
| Catmull-Rom/Bezier interpolation | Linear only | ⚠️ Approximate with linear |
| Parametric variables (`$key`) | Hardcoded values | ⚠️ Resolve variables on import |
| Profile display metadata | Not applicable | ❌ Ignore (visual-only) |
| Stage key identifiers | Auto-generated | ✅ Map 1:1 |

Conversely, some of our features don't map to Meticulous:

| Our Feature | Meticulous Equivalent | Handling |
|---|---|---|
| Grinder time/RPM | Not in profile schema | Store in recipe wrapper (not profile) |
| Valve selection (brew/steam/purge) | Hardware-specific | Don't export |
| PID tuning parameters | Machine-level setting | Don't export |
| Temperature cooldown mode | Not supported | Don't export |
| Flow offset | Not supported | Don't export |

---

## Shot Telemetry — Streaming to Meticulous Cloud

### What Meticulous Tools Expect

Any Meticulous-compatible client expects to receive `sensor_update` events via
Socket.IO at 5–10 Hz during a brew:

```json
{
  "pressure_brew": 9.1,
  "pressure_boiler": 1.2,
  "temperature_brew": 93.2,
  "temperature_boiler": 120.5,
  "flow_rate": 2.3,
  "weight": 18.7,
  "shot_time": 15.2,
  "machine_status": "brewing"
}
```

### Our ESP32 as Telemetry Source

During a brew, our ESP32 already has all this data from its sensors. We would
emit it in Meticulous format:

```cpp
void emit_sensor_update() {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "42[\"sensor_update\","
    "{\"pressure_brew\":%.1f,"
    "\"temperature_brew\":%.1f,"
    "\"flow_rate\":%.1f,"
    "\"weight\":%.1f,"
    "\"shot_time\":%.1f,"
    "\"machine_status\":\"%s\"}]",
    current_pressure,
    current_temp,
    current_flow,
    current_weight,
    shot_timer,
    machine_state_str());
  ws_client.sendText(buf);
}
```

### Mapping Our Sensors to Meticulous Fields

| Meticulous Field | Our Source | Notes |
|---|---|---|
| `pressure_brew` | Pressure sensor (brew line) | Direct mapping |
| `pressure_boiler` | Not available (single boiler) | Send `0.0` or omit |
| `temperature_brew` | Thermoblock temperature | Direct mapping |
| `temperature_boiler` | Same sensor (single boiler) | Same value as brew temp |
| `flow_rate` | Flow meter (pulses → ml/s) | Direct mapping |
| `weight` | Scale sensor (if connected) | `0.0` if no scale |
| `shot_time` | Brew timer | Direct mapping |
| `machine_status` | State machine state | Map to Meticulous enum |

---

## Authentication & Device Registration

### For Local API (Direction A: Acting as Meticulous)

No authentication needed. The Meticulous machine trusts LAN clients. If we
emulate a Meticulous machine on the LAN, any compatible tool can connect
without credentials.

### For Cloud API (Direction B: Connecting to Cloud)

This is the harder challenge. To connect to meticuloushome.com:

1. **Account required** — user needs a Meticulous account.
2. **Device registration** — the cloud expects a Meticulous serial number and
   hardware ID. Our ESP32 is not a Meticulous machine.
3. **Token exchange** — OAuth2/JWT flow for API access.

### The Third-Party Device Problem

The Meticulous cloud was designed for Meticulous hardware only. There is
currently **no official pathway** for third-party devices to register with
meticuloushome.com. This is the biggest blocker for direct cloud integration.

Possible approaches:

| Approach | Feasibility | Risk |
|---|---|---|
| **A. Ask Meticulous for API access** | Best option | They may not grant it |
| **B. Emulate a Meticulous device ID** | Technically possible | Violates ToS, ethically questionable |
| **C. Use their cloud API without device registration** | Unlikely to work | Most endpoints require device auth |
| **D. Skip their cloud; host our own** | Always works | Lose Meticulous community features |
| **E. Use open-source backend as relay** | Works locally | No cloud features, but full local API compat |

**Recommendation:** Approach A first (reach out to Meticulous team). If that
fails, Approach E (local API compatibility only) combined with D (our own
cloud) is the pragmatic path.

---

## Integration Architecture Options

### Option 1: Full Meticulous Emulation (Local Only)

Our ESP32 runs a Socket.IO server that emulates the Meticulous API. Any
Meticulous-compatible tool on the LAN works with our machine.

```
┌─────────────────────┐       ┌────────────────────────┐
│    Our ESP32         │       │   Meticulous HA Add-on │
│  (ESPHome firmware)  │  ◄──► │   MeticAI              │
│                      │  SIO  │   MCP Servers           │
│  Meticulous API      │       │   Custom tools          │
│  emulation layer     │       └────────────────────────┘
│                      │
│  + ESPHome Native API│ ◄───► Home Assistant (direct)
│  + Our MQTT client   │ ◄───► Our Cloud
└─────────────────────┘
```

- **Pros:** Works today, no Meticulous permission needed, full local tool compat.
- **Cons:** No cloud features, no community profiles from Meticulous. RAM cost
  of running Socket.IO server on ESP32.
- **Complexity:** Medium — need Socket.IO server implementation + API endpoints.

### Option 2: Meticulous Cloud Client (Direct)

Our ESP32 connects to meticuloushome.com as if it were a Meticulous machine.

- **Pros:** Full cloud features, community access.
- **Cons:** Requires Meticulous cooperation (or device emulation). May violate
  ToS. Cloud API not publicly documented for third parties.
- **Complexity:** High — auth flow, device registration, ongoing compatibility.

### Option 3: Hybrid — Our Cloud + Meticulous Profile Compatibility

Our ESP32 connects to our own cloud (MQTT) for device management. Our cloud
backend has a Meticulous adapter that:

1. Imports/exports profiles in Meticulous JSON format.
2. Translates our shot data to Meticulous-compatible format.
3. Optionally pushes shots to Visualizer.coffee (which also accepts Meticulous data).

Locally, the ESP32 runs a Meticulous-compatible Socket.IO endpoint for LAN
tools.

```
┌──────────────────────┐
│    Our ESP32          │
│                       │
│  ┌─────────────────┐  │  MQTT  ┌───────────────────┐
│  │ Our MQTT client │──┼───────►│   Our Cloud       │
│  └─────────────────┘  │        │                   │
│                       │        │  Meticulous profile│
│  ┌─────────────────┐  │        │  adapter (import/  │
│  │ Meticulous API  │  │        │  export JSON)      │
│  │ compat layer    │  │        │                   │
│  │ (Socket.IO srv) │  │        │  Visualizer.coffee │
│  └────────┬────────┘  │        │  adapter (shots)   │
│           │           │        └───────────────────┘
└───────────┼───────────┘
            │ LAN
    ┌───────▼────────┐
    │ HA Add-on      │
    │ MeticAI        │
    │ MCP Servers    │
    └────────────────┘
```

- **Pros:** Full local compat + our own cloud + Meticulous profile ecosystem.
  No Meticulous permission needed. Users can share profiles between Meticulous
  owners and our machine owners via JSON export/import.
- **Cons:** No direct Meticulous cloud integration (no community features on
  their platform). More components to build.
- **Complexity:** Medium-high, but modular.

### Option 4: Community Cloud Bridge

Run the Meticulous open-source backend (`meticulous-backend`) as a cloud
service. Our ESP32 connects to this self-hosted Meticulous backend instead of
meticuloushome.com.

- **Pros:** Full API compatibility, self-hosted, community-driven.
- **Cons:** Must maintain a running instance of meticulous-backend (Python).
  Users need to self-host or we need to host it. License concerns (check
  meticulous-backend license).
- **Complexity:** High — running someone else's backend as a service.

---

## Recommended Architecture

**Option 3 (Hybrid) is recommended.** Here's why:

### For the ESP32 firmware:

1. **Implement a minimal Socket.IO v4 server** (~500–800 lines of C++) that
   exposes the Meticulous telemetry and command events on the LAN. This
   lets all Meticulous-compatible tools work with our machine. The server
   handles multi-client connections, Engine.IO handshake, packet framing,
   and event broadcasting.

2. **Implement a Meticulous profile parser** that can read Meticulous JSON
   profiles and convert them to our internal brew profile format.

3. **Implement a Meticulous telemetry emitter** that formats our sensor
   data as Meticulous-compatible `sensor_update` events.

4. **Keep our MQTT client** as the primary cloud connection (to our own
   cloud backend, as designed in `cloud_ideas.md`).

### For our cloud backend:

5. **Meticulous Profile Adapter** — import/export profiles in Meticulous JSON
   format. This enables cross-platform profile sharing.

6. **Meticulous Telemetry Translator** — convert our MQTT telemetry to
   Meticulous-compatible format for tools that expect it.

### Future (requires Meticulous cooperation):

7. **Direct cloud integration** — if Meticulous offers a third-party device
   API, connect to meticuloushome.com directly.

### What This Gives Users

| Feature | Available? | Via |
|---|---|---|
| Meticulous profiles on our machine | ✅ | Profile JSON import/export |
| HA Add-on works with our machine | ✅ | Local Socket.IO emulation |
| MeticAI shot analysis | ✅ | Local Socket.IO emulation |
| MCP server for AI assistants | ✅ | Local Socket.IO emulation |
| Our own cloud (device mgmt, recipes) | ✅ | Our MQTT + REST backend |
| Meticulous community profiles | ⚠️ | Manual JSON download/import |
| Meticulous cloud shot storage | ❌ | Requires their cooperation |
| Meticulous phone app | ❌ | App checks device serial/model |

---

## Implementation Roadmap

### Phase 1: Profile Compatibility (Week 1–2)

- Implement Meticulous JSON profile parser in C++.
- Implement profile translation layer (Meticulous ↔ internal format).
- Validate with example profiles from `espresso-profile-schema` repo.

### Phase 2: Socket.IO Server on ESP32 (Week 3–4)

- Implement minimal Engine.IO v4 + Socket.IO v4 server using
  `esp_websocket_client` (or AsyncWebSocket server).
- Expose `sensor_update` telemetry during brew.
- Expose `brew_start`, `brew_end`, `status_update` events.

### Phase 3: Meticulous REST Endpoints (Week 5)

- Implement `/api/v1/profiles` (list, get, upload).
- Implement `/api/v1/status` (machine state).
- Implement `/api/v1/brew/start`, `/api/v1/brew/stop`.

### Phase 4: HA Add-on Compatibility Testing (Week 6)

- Test with `nickwilsonr/meticulous-addon` against our ESP32.
- Fix any protocol discrepancies.
- Document setup guide for users.

### Phase 5: Cloud Profile Adapter (Week 7–8)

- Implement Meticulous JSON import/export in our cloud backend.
- Enable users to import profiles from Meticulous JSON files.
- Enable shot export in Meticulous-compatible format.

### Phase 6: Community & Outreach (Ongoing)

- Reach out to Meticulous team about third-party device API.
- Contribute compatibility improvements to meticulous-addon.
- Publish our Meticulous compatibility layer as reusable component.

---

## Risks & Challenges

### 1. Protocol Drift

Meticulous may change their Socket.IO events or REST API without notice. Since
their API is not versioned for third parties, updates could break our
compatibility layer.

**Mitigation:** Pin to a known working protocol version. Monitor their backend
repo for breaking changes. Implement graceful degradation.

### 2. ESP32 Resource Constraints

Running a Socket.IO server alongside ESPHome, MQTT, and our machine control
loop adds memory pressure.

**Mitigation:** The Socket.IO server is optional — users enable it only if
they need Meticulous tool compatibility. Implement lazy initialization.
Estimated additional RAM: 10–25 KB.

### 3. Legal/ToS Concerns

Emulating a Meticulous device to connect to their cloud would violate their
Terms of Service. We must NOT do this without explicit permission.

**Mitigation:** Local emulation only (no cloud impersonation). Profile
format compatibility is fine (it's an open schema). Reach out to Meticulous
for official third-party API.

### 4. Profile Fidelity

Complex Meticulous profiles (multi-point Bezier curves, 5+ stages) may not
translate perfectly to our simpler brew system.

**Mitigation:** Implement a "best effort" translation with clear warnings
when features are simplified. Let users review the translated profile before
execution.

### 5. Incomplete API Documentation

The community OpenAPI docs may not cover all endpoints. Some Socket.IO events
may be undocumented.

**Mitigation:** Reference the backend source code directly. Run the backend
in Docker emulation mode to test against.

---

## Open Questions

1. **Should we reach out to Meticulous Home Inc. about a third-party device
   API?** This would unlock direct cloud integration. They might be interested
   in expanding their ecosystem to more hardware.

2. **Should the Socket.IO server on ESP32 be always-on or opt-in?** Running
   it always-on adds ~10–25 KB RAM overhead. Opt-in means users who don't need
   Meticulous compat don't pay the cost.

3. **Should we adopt the Meticulous profile schema as our primary profile
   format?** This would simplify compatibility but tie our format to their
   schema evolution. Alternatively, keep our own format and translate.

4. **Can we contribute to the `espresso-profile-schema` repo?** For example,
   adding support for additional interpolation modes or stage transition
   types would benefit the whole espresso community. (Note: grinder and
   valve settings are hardware-specific and belong in a recipe wrapper,
   not the portable profile schema — see the compatibility assessment above.)

5. **Should we run the Meticulous backend as an optional Docker add-on?** For
   advanced users who want full API compatibility, we could provide a
   Docker Compose file that runs `meticulous-backend` alongside our cloud.

6. **How do we handle profiles that exceed our hardware capabilities?** A
   Meticulous profile might specify 12 bar pressure — our machine may only
   reach 9 bar. Need a validation/warning system.

7. **Should we support the Meticulous phone app?** The app likely checks
   device model/serial on connection. Supporting it would require deep
   reverse engineering and ongoing compatibility work. Probably not worth it.

8. **What is the license of `meticulous-backend`?** We need to check before
   referencing or adapting any of their code. Using their protocol and schema
   (factual information) is fine; copying code requires license compliance.

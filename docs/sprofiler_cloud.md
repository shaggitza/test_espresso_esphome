# Sprofiler Cloud — Deep Exploration & ESP32 Integration Plan

> **Status: Research / Planning — no code written yet.**
> This document deeply explores the Sprofiler espresso platform, its cloud
> backend, the Gaggiuino ecosystem it serves, and how our ESP32-based ESPHome
> espresso controller could integrate with it — both through the cloud API
> and directly from the device.

---

## Table of Contents

1. [Why Sprofiler?](#why-sprofiler)
2. [Sprofiler Platform Overview](#sprofiler-platform-overview)
3. [Ecosystem Map](#ecosystem-map)
4. [Architecture Deep Dive](#architecture-deep-dive)
5. [Cloud API Analysis](#cloud-api-analysis)
6. [Gaggiuino Profile Schema — Full Specification](#gaggiuino-profile-schema--full-specification)
7. [Shot Data Format](#shot-data-format)
8. [Gaggiuino Local REST API & WebSocket](#gaggiuino-local-rest-api--websocket)
9. [BLE Communication Protocol](#ble-communication-protocol)
10. [ESP32 Direct Integration — Architecture](#esp32-direct-integration--architecture)
11. [Profile Translation Layer](#profile-translation-layer)
12. [Shot Upload — Streaming to Sprofiler Cloud](#shot-upload--streaming-to-sprofiler-cloud)
13. [Authentication & Device Registration](#authentication--device-registration)
14. [Integration Architecture Options](#integration-architecture-options)
15. [Recommended Architecture](#recommended-architecture)
16. [Implementation Roadmap](#implementation-roadmap)
17. [Effort Estimation](#effort-estimation)
18. [Risks & Challenges](#risks--challenges)
19. [Open Questions](#open-questions)

---

## Why Sprofiler?

Sprofiler is the **primary community cloud platform** for the Gaggiuino
espresso machine mod ecosystem — the largest open-source espresso hardware
community. Integration gives our users:

- **Access to the Gaggiuino community** — the biggest open-source espresso
  profile library and shot database
- **Profile sharing** — download community profiles, share our own
- **Shot analytics** — cloud-based shot tracking, history, and comparison
- **Cross-firmware community** — connect with Gaggiuino, GaggiMate, and
  future firmware users through a shared platform
- **Existing integration path** — Gaggiuino firmware already has built-in
  Sprofiler upload, giving us a proven protocol to follow

Unlike Meticulous (proprietary hardware) or BrewOS (different firmware),
Sprofiler is **firmware-agnostic in principle** — it works with any device
that can produce shot data in the Gaggiuino JSON format. Our ESP32 can speak
this format directly.

---

## Sprofiler Platform Overview

### The Platform

| Aspect | Details |
|---|---|
| **Operator** | Community / TesseraSkye (open-source) |
| **Website** | [sprofiler.io](https://sprofiler.io/) (production), [dev.sprofiler.io](https://dev.sprofiler.io/) (development) |
| **Source** | [github.com/TesseraSkye/sprofiler](https://github.com/TesseraSkye/sprofiler) — Vue.js frontend + backend (RESTful) |
| **Mobile** | Android app via CapacitorJS (sideload), iOS planned |
| **Languages** | Vue.js (66%), JavaScript (26%), C++ (embedded), HTML |
| **Auth** | Account-based (email/password), API token for machine integration |
| **Primary device** | Gaggiuino-modded Gaggia Classic (STM32 + ESP32) |
| **Community** | Discord-based, active profile sharing and troubleshooting |

### Key Features

| Feature | Status | Description |
|---|---|---|
| Shot tracking | ✅ | Record pressure, temperature, flow, weight, duration |
| Cloud storage | ✅ | Shots synced to cloud for backup and analysis |
| Shot analytics | ✅ | Visualize and compare shots over time |
| Profile download | ✅ | Download community-shared brewing profiles |
| Profile sharing | ✅ | Upload and share profiles with the community |
| Community browse | ✅ | Browse shots and profiles from other users |
| API token auth | ✅ | Machine-to-cloud authentication via token |
| Multi-firmware | ⚠️ | Primarily Gaggiuino; other firmwares not officially supported |
| Device management | ❌ | No OTA, no fleet management, no remote commands |
| Real-time telemetry | ❌ | No live streaming to cloud (shots uploaded after completion) |

---

## Ecosystem Map

### Sprofiler Ecosystem

| Component | Repository / Resource | Purpose |
|---|---|---|
| **Sprofiler Web/App** | [github.com/TesseraSkye/sprofiler](https://github.com/TesseraSkye/sprofiler) | Vue.js web app + Capacitor mobile app |
| **Sprofiler Backend** | Backend within the same repo (`sprofilerApp/`) | RESTful API server |
| **BLE Bridge Code** | `esp32_code/BLE_Spro_2/` in the sprofiler repo | ESP32 BLE GATT communication with Gaggiuino |
| **Sprofiler.io** | [sprofiler.io](https://sprofiler.io/) | Production cloud instance |
| **Dev instance** | [dev.sprofiler.io](https://dev.sprofiler.io/) | Development/staging cloud |

### Gaggiuino Ecosystem (Sprofiler's primary device)

| Component | Repository / Resource | Purpose |
|---|---|---|
| **Gaggiuino Firmware** | [github.com/Zer0-bit/gaggiuino](https://github.com/Zer0-bit/gaggiuino) | STM32 + ESP32 espresso machine firmware |
| **Gaggiuino ESP Companion** | [github.com/kstam/gaggiuino-esp](https://github.com/kstam/gaggiuino-esp) | ESP32 WiFi/BLE bridge for Gaggiuino |
| **Gaggiuino REST API** | [github.com/ALERTua/gaggiuino_api](https://github.com/ALERTua/gaggiuino_api) | Python API wrapper (`pip install gaggiuino_api`) |
| **Gaggiuino MCP Server** | [github.com/sgerlach/grr-gaggiuino-mcp](https://github.com/sgerlach/grr-gaggiuino-mcp) | MCP server for AI assistant integration |
| **GaggiMate** | [docs.gaggimate.eu](https://docs.gaggimate.eu/) | Alternative frontend/companion for Gaggiuino |
| **ShotProfiles.com** | [shotprofiles.com](https://shotprofiles.com/) | Community profile database (Gaggiuino-compatible) |

---

## Architecture Deep Dive

### Sprofiler Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                   sprofiler.io (Cloud)                       │
│                                                              │
│  ┌────────────────────┐  ┌──────────────────────────────┐   │
│  │   Vue.js Frontend  │  │     RESTful Backend           │   │
│  │                    │  │                               │   │
│  │ • Shot browser     │  │ • User auth (email/password)  │   │
│  │ • Profile library  │  │ • Shot CRUD endpoints         │   │
│  │ • Shot analytics   │  │ • Profile CRUD endpoints      │   │
│  │ • Community hub    │  │ • Community browse/search      │   │
│  │ • User dashboard   │  │ • API token management        │   │
│  └────────┬───────────┘  └──────────┬────────────────────┘   │
│           │                         │                        │
│           └────────────┬────────────┘                        │
│                        │ REST API                            │
│                   ┌────▼─────┐                               │
│                   │ Database │                               │
│                   │ (shots,  │                               │
│                   │ profiles,│                               │
│                   │ users)   │                               │
│                   └──────────┘                               │
│                                                              │
└────────────────────────┬────────────────────────────────────┘
                         │ HTTPS (REST API)
                         │
              ┌──────────┼──────────────────────────┐
              │          │                          │
      ┌───────▼──┐  ┌───▼───────────┐  ┌───────────▼──────────┐
      │ Sprofiler│  │  Gaggiuino    │  │  Mobile App          │
      │ Web UI   │  │  Firmware     │  │  (Capacitor/Android) │
      │ (browser)│  │  (STM32+ESP32)│  │                      │
      └──────────┘  └───────────────┘  └──────────────────────┘
```

### Gaggiuino Device Architecture (Sprofiler's primary client)

```
┌─────────────────────────────────────────────────────────────┐
│              Gaggiuino Device (Gaggia Classic mod)           │
│                                                              │
│  ┌──────────────────────┐    ┌──────────────────────────┐   │
│  │    STM32 MCU          │    │    ESP32 Companion        │   │
│  │  (Real-time control)  │    │  (Network + UI bridge)    │   │
│  │                       │    │                           │   │
│  │  • PID temperature    │◄──►│  • WiFi AP/station        │   │
│  │  • Pressure profiling │UART│  • Web dashboard (HTTP)   │   │
│  │  • Flow control       │    │  • REST API server        │   │
│  │  • Sensor reading     │    │  • WebSocket telemetry    │   │
│  │  • Shot execution     │    │  • BLE server (GATT)      │   │
│  │                       │    │  • Sprofiler upload        │   │
│  └──────────────────────┘    └──────────────────────────┘   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Key Architectural Insights

1. **Two-chip design:** Gaggiuino uses STM32 for real-time control + ESP32 as
   a WiFi/BLE bridge and web server. Our ESP32-based ESPHome device is a
   single-chip architecture — simpler, but we need to handle both control
   and networking on one MCU.

2. **Local-first:** The Gaggiuino device runs a full local REST API + WebSocket
   server. Sprofiler cloud is an **optional upload target**, not required for
   operation. This matches our design philosophy perfectly.

3. **BLE bridge:** The Sprofiler mobile app can connect to Gaggiuino via BLE
   for local data access without WiFi. The `esp32_code/BLE_Spro_2/` code in
   the Sprofiler repo handles this.

4. **Post-shot upload:** Shots are uploaded to Sprofiler **after completion**,
   not streamed in real-time. This is simpler to implement (single HTTP POST
   per shot) compared to Meticulous's real-time Socket.IO streaming.

---

## Cloud API Analysis

### Authentication

Sprofiler uses **API token authentication** for machine-to-cloud communication:

1. User creates account on sprofiler.io (email/password).
2. User generates an API token from their account settings.
3. Token is entered into the machine's web interface (Gaggiuino Settings →
   "Experimental" or "Integration" section).
4. Machine includes token in API requests as a Bearer token header.

```
Authorization: Bearer <api_token>
```

### Known API Endpoints

The Sprofiler API is **not publicly documented**. Based on analysis of the
Gaggiuino firmware integration, community issue reports, and network traffic
patterns, the following endpoints are inferred:

| Method | Endpoint (inferred) | Purpose | Auth |
|---|---|---|---|
| `POST` | `/api/shots/upload` | Upload completed shot data | Bearer token |
| `GET` | `/api/profiles` | List available profiles | Bearer token |
| `GET` | `/api/profiles/{id}` | Download a specific profile | Bearer token |
| `POST` | `/api/profiles` | Upload/share a profile | Bearer token |
| `GET` | `/api/community/shots` | Browse community shots | Optional |
| `GET` | `/api/community/profiles` | Browse community profiles | Optional |
| `POST` | `/backend/login` | User authentication | Email/password |

### API Characteristics

| Aspect | Details |
|---|---|
| **Protocol** | HTTPS (REST) |
| **Data format** | JSON |
| **Auth method** | Bearer token (API key from user account) |
| **Rate limiting** | Unknown (not documented) |
| **Versioning** | No explicit API versioning observed |
| **CORS** | Likely enabled for web app access |
| **Public docs** | **None** — integration requires reverse-engineering or maintainer cooperation |

### API Documentation Gap

This is the **biggest integration challenge**. Unlike Meticulous (open-source
backend, documented OpenAPI) or Visualizer.coffee (public API with docs),
Sprofiler's API is:

- Not publicly documented
- Not versioned for third-party consumers
- Could change without notice
- Would require Sprofiler maintainer cooperation for stable integration

---

## Gaggiuino Profile Schema — Full Specification

Sprofiler uses the **Gaggiuino profile JSON format**, which is the de facto
standard across the Gaggiuino ecosystem (Gaggiuino firmware, GaggiMate,
ShotProfiles.com, Sprofiler).

### Top-Level Structure

```json
{
  "id": "classic_flat_9bar",
  "name": "Classic Flat 9 Bar",
  "waterTemperature": 93,
  "description": "Simple three-phase profile with classic preinfusion.",
  "author": "community_user",
  "phases": [ ... ]
}
```

### Top-Level Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | Yes | Unique identifier for the profile |
| `name` | string | Yes | Human-readable profile name |
| `waterTemperature` | number | Yes | Brew temperature in °C (applies to all phases) |
| `description` | string | No | Freeform description |
| `author` | string | No | Profile creator's name/handle |
| `created_at` | string | No | ISO 8601 timestamp |
| `updated_at` | string | No | ISO 8601 timestamp |
| `phases` | array | Yes | Ordered list of extraction phases |

### Phase Schema

Each phase controls one aspect of the extraction (pressure, flow, fill):

```json
{
  "type": "pressure",
  "value": 9,
  "duration": 30000,
  "name": "Extraction",
  "restriction": {
    "targetWeight": 36
  },
  "pump": true,
  "valve": true,
  "maxDuration": 45000
}
```

### Phase Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `type` | enum | Yes | Phase control mode: `"fill"`, `"pressure"`, `"flow"`, `"wait"` |
| `value` | number | Yes | Target value (bar for pressure, ml/s for flow) |
| `duration` | number | Yes | Phase duration in milliseconds |
| `name` | string | No | Human-readable phase label |
| `restriction` | object | No | End condition (weight, pressure, flow threshold) |
| `restriction.targetWeight` | number | No | Stop phase when scale reads this weight (grams) |
| `restriction.maxPressure` | number | No | Pressure ceiling during this phase (bar) |
| `restriction.maxFlow` | number | No | Flow ceiling during this phase (ml/s) |
| `pump` | boolean | No | Whether pump is active (default: true) |
| `valve` | boolean | No | Whether solenoid valve is open (default: true) |
| `maxDuration` | number | No | Safety timeout in milliseconds |

### Phase Types

| Type | Description | Value Unit |
|---|---|---|
| `fill` | Initial group fill — pump runs until group is saturated | N/A (duration-based) |
| `pressure` | Pressure-controlled phase — maintain target pressure | Bar |
| `flow` | Flow-controlled phase — maintain target flow rate | ml/s |
| `wait` | Pause — no pump activity, soak time | N/A (duration-based) |

### Example: Classic Italian Espresso Profile

```json
{
  "id": "classic_italian",
  "name": "Classic Italian Espresso",
  "waterTemperature": 93,
  "phases": [
    {
      "type": "fill",
      "value": 3,
      "duration": 3000,
      "name": "Group Fill"
    },
    {
      "type": "pressure",
      "value": 2,
      "duration": 8000,
      "name": "Preinfusion",
      "restriction": {
        "maxFlow": 4
      }
    },
    {
      "type": "pressure",
      "value": 9,
      "duration": 30000,
      "name": "Extraction",
      "restriction": {
        "targetWeight": 36
      }
    }
  ],
  "description": "Traditional 9-bar flat profile with 2-bar preinfusion."
}
```

### Example: Declining Pressure (Lever-Style)

```json
{
  "id": "lever_decline",
  "name": "Lever-Style Decline",
  "waterTemperature": 92,
  "phases": [
    {
      "type": "fill",
      "value": 0,
      "duration": 5000,
      "name": "Fill"
    },
    {
      "type": "pressure",
      "value": 6,
      "duration": 10000,
      "name": "Peak Pressure"
    },
    {
      "type": "pressure",
      "value": 3,
      "duration": 20000,
      "name": "Declining Pressure",
      "restriction": {
        "targetWeight": 40
      }
    }
  ]
}
```

---

## Shot Data Format

### Shot Record Structure

Shot data uploaded to Sprofiler follows the Gaggiuino telemetry format:

```json
{
  "id": 187,
  "timestamp": 1713340562,
  "profile": {
    "name": "Leva 9 LR v0.5"
  },
  "duration": 26500,
  "datapoints": [
    {
      "time": 0.00,
      "pressure": 0.1,
      "temperature": 92.5,
      "flow": 7.0,
      "weight": 0
    },
    {
      "time": 0.10,
      "pressure": 0.2,
      "temperature": 92.7,
      "flow": 6.8,
      "weight": 0.5
    },
    {
      "time": 0.20,
      "pressure": 0.5,
      "temperature": 92.9,
      "flow": 5.2,
      "weight": 1.2
    }
  ]
}
```

### Shot Data Fields

| Field | Type | Description |
|---|---|---|
| `id` | number | Shot identifier (device-local sequence number) |
| `timestamp` | number | Unix timestamp of shot start |
| `profile.name` | string | Name of profile used for this shot |
| `duration` | number | Total shot duration in milliseconds |
| `datapoints` | array | Time-series array of sensor readings |

### Datapoint Fields

| Field | Type | Unit | Description |
|---|---|---|---|
| `time` | number | seconds | Time since shot start |
| `pressure` | number | bar | Brew pressure at this moment |
| `temperature` | number | °C | Brew water temperature |
| `flow` | number | ml/s | Water flow rate |
| `weight` | number | grams | Cup weight (if scale connected) |

### Telemetry Characteristics

| Aspect | Details |
|---|---|
| **Sample rate** | ~10 Hz (100ms intervals) typical |
| **Upload timing** | After shot completion (not real-time) |
| **Upload method** | Single HTTP POST with full shot JSON |
| **Payload size** | ~5–50 KB per shot (depending on duration and sample rate) |
| **Compression** | None observed (raw JSON) |

---

## Gaggiuino Local REST API & WebSocket

Understanding the Gaggiuino local API is important because we may want to
emulate it for tool compatibility (same approach as our Meticulous emulation
strategy).

### REST Endpoints (Gaggiuino device, local network)

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/status` | Current machine state (temperature, pressure, water level, active profile) |
| `GET` | `/profiles` | List all available brewing profiles |
| `POST` | `/profile/select` | Activate a profile by ID |
| `GET` | `/shots` | List shot history |
| `GET` | `/shot/{id}` | Full telemetry for a specific shot |
| `GET` | `/shot/latest` | Most recent completed shot |

### WebSocket (Real-Time Telemetry)

The Gaggiuino ESP32 companion also exposes a **WebSocket endpoint** for live
shot data streaming during extraction:

```javascript
const socket = new WebSocket('ws://gaggiuino.local/api/shot-telemetry');
socket.onmessage = (event) => {
  const data = JSON.parse(event.data);
  // data = { time, pressure, temperature, flow, weight }
};
```

- **Protocol:** Standard WebSocket (not Socket.IO)
- **Data format:** JSON datapoints at ~10 Hz during active shot
- **Use case:** Live shot graphing in the web dashboard

### Python API Wrapper

```python
from gaggiuino_api import GaggiuinoAPI

async with GaggiuinoAPI() as client:
    status = await client.get_status()
    profiles = await client.get_profiles()
    await client.select_profile(profiles[0].id)
    shot = await client.get_shot(1)
    # shot.datapoints.pressure, shot.datapoints.pumpFlow, etc.
```

---

## BLE Communication Protocol

The Sprofiler mobile app communicates with Gaggiuino via **BLE (Bluetooth Low
Energy)** using a custom GATT profile. The ESP32 in the Gaggiuino device acts
as a BLE server (peripheral), and the Sprofiler app acts as a BLE client
(central).

### BLE Architecture

```
┌──────────────────┐     BLE GATT      ┌──────────────────┐
│  Sprofiler App   │◄──────────────────►│  Gaggiuino ESP32 │
│  (BLE Central)   │                    │  (BLE Peripheral) │
│                  │  Custom Service    │                   │
│  • Connect       │  • Pressure char   │  • Advertise      │
│  • Discover      │  • Temperature     │  • Serve chars    │
│  • Subscribe     │  • Flow char       │  • Notify updates │
│  • Read data     │  • Weight char     │  • Accept writes  │
│                  │  • Shot timer      │                   │
│                  │  • Status char     │                   │
└──────────────────┘                    └──────────────────┘
```

### BLE Implementation Details

| Aspect | Details |
|---|---|
| **Source code** | `esp32_code/BLE_Spro_2/` in [TesseraSkye/sprofiler](https://github.com/TesseraSkye/sprofiler) |
| **Protocol** | BLE GATT with custom 128-bit UUIDs |
| **Service** | Custom primary service for espresso data |
| **Characteristics** | Pressure, temperature, flow, weight, shot timer, machine status |
| **Properties** | Read + Notify (sensor data), Write (commands) |
| **Data format** | Binary-encoded sensor values (compact for BLE MTU constraints) |
| **Update rate** | Notify-based (~10 Hz during shot) |
| **Range** | ~10m typical BLE range |

### BLE Relevance for Our Integration

BLE is a **secondary communication path** — primarily for the mobile app when
WiFi is unavailable. For our ESP32 device:

- **We could implement a BLE GATT server** matching the Sprofiler BLE profile,
  allowing the Sprofiler mobile app to connect to our machine directly.
- **Lower priority** than HTTP/REST integration, since WiFi is our primary
  connectivity method and BLE adds significant firmware complexity.
- **Memory cost:** BLE stack consumes ~50–80 KB of RAM on ESP32.

---

## ESP32 Direct Integration — Architecture

### Three Integration Directions

```
Direction A: Our ESP32 → Sprofiler Cloud
─────────────────────────────────────────
Our device uploads shots and syncs profiles directly with sprofiler.io
via HTTPS REST API. Simplest path, highest value.

Direction B: Our ESP32 emulates Gaggiuino local API
────────────────────────────────────────────────────
Our device runs a REST API + WebSocket server matching the Gaggiuino
endpoint schema. Gaggiuino-compatible tools (Python API, MCP server,
GaggiMate) work with our machine on LAN.

Direction C: Our ESP32 implements Sprofiler BLE profile
───────────────────────────────────────────────────────
Our device runs a BLE GATT server matching the Sprofiler BLE protocol.
The Sprofiler mobile app connects to our machine via BLE. Lower priority.
```

### Direction A: ESP32 → Sprofiler Cloud (Primary)

```
┌──────────────────────┐       HTTPS        ┌───────────────────┐
│    Our ESP32          │  ────────────────► │  sprofiler.io     │
│  (ESPHome firmware)   │                    │  (Cloud)          │
│                       │  POST /api/shots   │                   │
│  After each shot:     │  ────────────────► │  • Store shot     │
│  • Format shot JSON   │                    │  • Analytics      │
│  • Include API token  │  GET /api/profiles │  • Community      │
│  • Upload to cloud    │  ◄──────────────── │  • Profile library│
│                       │                    │                   │
│  On profile sync:     │                    │                   │
│  • Fetch profile list │                    │                   │
│  • Download profiles  │                    │                   │
│  • Store in LittleFS  │                    │                   │
└──────────────────────┘                    └───────────────────┘
```

**Implementation requirements:**
- HTTP client (already in ESPHome via `esphome::http_request`)
- JSON serialization of shot data in Gaggiuino format
- API token storage (configurable via HA or web UI)
- Profile JSON parser for Gaggiuino format
- LittleFS storage for downloaded profiles

### Direction B: Gaggiuino Local API Emulation

```
┌──────────────────────┐                    ┌───────────────────┐
│    Our ESP32          │  ◄── HTTP/WS ───  │  Gaggiuino tools  │
│  (ESPHome firmware)   │                    │                   │
│                       │  GET /status       │  • gaggiuino_api  │
│  Emulates:            │  GET /profiles     │  • MCP server     │
│  • /status            │  GET /shot/{id}    │  • GaggiMate      │
│  • /profiles          │  WS telemetry      │  • Custom scripts │
│  • /shot/{id}         │                    │                   │
│  • WebSocket stream   │                    │                   │
└──────────────────────┘                    └───────────────────┘
```

**Implementation requirements:**
- HTTP server on ESP32 (ESPHome `web_server` component or custom)
- WebSocket server for live telemetry streaming
- Gaggiuino-compatible JSON response format
- Translation between our internal state and Gaggiuino status format

---

## Profile Translation Layer

### Gaggiuino → Our Format

The Gaggiuino phase-based profile maps well to our brew profile system:

```
Gaggiuino Profile                    Our Internal Profile
─────────────────                    ────────────────────

waterTemperature: 93     ──────►    target_temperature: 93°C

phases[0]:                           brew phase 0:
  type: "fill"           ──────►      mode: FILL
  duration: 3000         ──────►      duration: 3s

phases[1]:                           brew phase 1:
  type: "pressure"       ──────►      mode: PRESSURE_CONTROL
  value: 2               ──────►      pressure_target: 2.0 bar
  duration: 8000         ──────►      duration: 8s (preinfusion)
  restriction.maxFlow: 4 ──────►      flow_limit: 4.0 ml/s

phases[2]:                           brew phase 2:
  type: "pressure"       ──────►      mode: PRESSURE_CONTROL
  value: 9               ──────►      pressure_target: 9.0 bar
  duration: 30000        ──────►      duration: 30s (max)
  restriction.            ──────►      weight_target: 36g (stop condition)
    targetWeight: 36
```

### Our Format → Gaggiuino

The reverse translation packages our brew settings into Gaggiuino JSON:

```cpp
String to_gaggiuino_profile(const BrewProfile& profile) {
  // Map our brew phases to Gaggiuino phases array
  // Map temperature, duration, pressure/flow targets
  // Generate restriction objects from our stop conditions
  // Return valid Gaggiuino JSON
}
```

### Profile Compatibility Assessment

| Our Feature | Gaggiuino Equivalent | Gap? |
|---|---|---|
| Brew temperature | `waterTemperature` | ✅ Direct mapping |
| Target weight | `restriction.targetWeight` | ✅ Direct mapping |
| Pressure setpoint | `type: "pressure"`, `value` | ✅ Direct mapping |
| Flow setpoint | `type: "flow"`, `value` | ✅ Direct mapping |
| Preinfusion time | Phase with lower pressure + `duration` | ✅ Direct mapping |
| Fill phase | `type: "fill"` | ✅ Direct mapping |
| Soak/bloom | `type: "wait"` | ✅ Direct mapping |
| Flow limit during pressure | `restriction.maxFlow` | ✅ Direct mapping |
| Pressure limit during flow | `restriction.maxPressure` | ✅ Direct mapping |
| PID parameters | Not in profile (machine-level) | ⚠️ We handle PID separately |
| Grinder settings | Not in profile (recipe-level) | ⚠️ Need recipe wrapper |
| Valve control | `valve` field in phase | ✅ Direct mapping |
| Multi-point curves | Not supported (single setpoint per phase) | ⚠️ Gaggiuino is simpler |

**Conclusion:** The Gaggiuino profile schema has **~90% compatibility** with
our brew profile needs. The format is simpler than Meticulous (no multi-point
dynamics curves or parametric variables), making translation straightforward.
Multi-point curves would need to be approximated as multiple discrete phases.

---

## Shot Upload — Streaming to Sprofiler Cloud

### Upload Flow

```
1. User pulls a shot on our machine
2. ESP32 collects telemetry at ~10 Hz (pressure, temp, flow, weight, time)
3. Shot completes (weight target, manual stop, or timeout)
4. ESP32 formats the shot as Gaggiuino-compatible JSON
5. ESP32 sends HTTP POST to sprofiler.io with Bearer token
6. Sprofiler stores the shot and makes it available in the user's dashboard
```

### C++ Pseudocode for Shot Upload

```cpp
void upload_shot_to_sprofiler(const ShotRecord& shot) {
  // Build Gaggiuino-format JSON
  DynamicJsonDocument doc(8192);  // ~8KB for typical shot
  doc["id"] = shot.id;
  doc["timestamp"] = shot.start_time;
  doc["duration"] = shot.duration_ms;

  JsonObject profile = doc.createNestedObject("profile");
  profile["name"] = shot.profile_name;

  JsonArray datapoints = doc.createNestedArray("datapoints");
  for (const auto& dp : shot.datapoints) {
    JsonObject point = datapoints.createNestedObject();
    point["time"] = dp.time_sec;
    point["pressure"] = dp.pressure_bar;
    point["temperature"] = dp.temperature_c;
    point["flow"] = dp.flow_ml_s;
    point["weight"] = dp.weight_g;
  }

  // Upload via HTTPS
  String json;
  serializeJson(doc, json);

  HTTPClient http;
  http.begin("https://sprofiler.io/api/shots/upload");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + sprofiler_api_token_);
  int code = http.POST(json);

  if (code == 200 || code == 201) {
    ESP_LOGI(TAG, "Shot uploaded to Sprofiler successfully");
  } else {
    ESP_LOGW(TAG, "Sprofiler upload failed: %d", code);
    // Queue for retry
  }
}
```

### Memory Considerations

| Concern | Assessment |
|---|---|
| Shot JSON size | ~5–50 KB depending on duration (25s shot ≈ 250 datapoints ≈ 15 KB) |
| JSON building | Need ~16 KB heap for DynamicJsonDocument |
| HTTP upload | Single POST, no persistent connection needed |
| TLS overhead | ~15–20 KB for HTTPS handshake |
| Total peak RAM | ~40–70 KB during upload (can be released immediately after) |

### Retry Strategy

If upload fails (network error, server error):
1. Store shot locally (LittleFS or SPIFFS)
2. Retry on next connectivity window
3. Queue up to 10 failed uploads
4. Discard oldest if queue exceeds limit

---

## Authentication & Device Registration

### How Gaggiuino Does It

1. User creates a Sprofiler account on sprofiler.io.
2. User generates an API token in their Sprofiler account settings.
3. User copies token into Gaggiuino's web interface (Settings → Integration).
4. Gaggiuino firmware stores the token and includes it in all API requests.

### How We Would Do It

Same flow, adapted for ESPHome:

```yaml
# Example ESPHome configuration for Sprofiler
espresso_machine:
  sprofiler:
    enabled: true
    api_token: !secret sprofiler_token
    upload_shots: true
    sync_profiles: true
    sync_interval: 1h
```

Or via Home Assistant:
1. User adds Sprofiler API token in HA integration settings.
2. HA passes token to ESP32 via ESPHome native API.
3. ESP32 stores token in NVS (non-volatile storage).

### No Device Registration Needed

Unlike Meticulous (which ties devices to hardware serials), Sprofiler uses
**user-level authentication** only. Any device with a valid API token can
upload shots. This means:

- **No hardware registration barrier** — our ESP32 doesn't need to pretend
  to be a Gaggiuino.
- **No serial number spoofing** — just a valid user account token.
- **Lower risk** — we're a legitimate API client, not impersonating hardware.

---

## Integration Architecture Options

### Option 1: Cloud API Only (Simplest)

Our ESP32 uploads shots and downloads profiles from sprofiler.io via HTTPS.

```
┌──────────────────────┐       HTTPS        ┌───────────────────┐
│    Our ESP32          │  ◄──────────────► │  sprofiler.io     │
│                       │  shots/profiles   │                   │
│  + ESPHome Native API │  ◄──────────────► │  Home Assistant   │
│  + Our MQTT cloud     │  ◄──────────────► │  Our Cloud        │
└──────────────────────┘                    └───────────────────┘
```

- **Pros:** Simplest implementation. No local API server needed. Lowest RAM.
- **Cons:** No Gaggiuino tool compatibility. Limited to Sprofiler features.
- **Effort:** ~2–3 weeks
- **RAM overhead:** ~5 KB (HTTP client is shared with other components)

### Option 2: Cloud API + Gaggiuino Local Emulation

Our ESP32 uploads to Sprofiler AND emulates the Gaggiuino REST API locally.

```
┌──────────────────────┐
│    Our ESP32          │
│                       │
│  ┌─────────────────┐  │  HTTPS  ┌───────────────────┐
│  │ Sprofiler client│──┼────────►│  sprofiler.io     │
│  └─────────────────┘  │         └───────────────────┘
│                       │
│  ┌─────────────────┐  │  HTTP   ┌───────────────────┐
│  │ Gaggiuino API   │◄─┼────────│  gaggiuino_api    │
│  │ emulation layer │  │  WS    │  MCP servers       │
│  └─────────────────┘  │         │  GaggiMate         │
│                       │         └───────────────────┘
│  + ESPHome Native API │ ◄────── Home Assistant
│  + Our MQTT cloud     │ ◄────── Our Cloud
└──────────────────────┘
```

- **Pros:** Full Sprofiler + Gaggiuino ecosystem access. Python API, MCP
  server, GaggiMate all work with our machine.
- **Cons:** More firmware complexity. HTTP server + WebSocket add RAM.
- **Effort:** ~5–6 weeks
- **RAM overhead:** ~15–25 KB (HTTP server + WebSocket)

### Option 3: Full Stack (Cloud + Local + BLE)

Everything in Option 2, plus BLE GATT server for Sprofiler mobile app.

- **Pros:** Maximum compatibility. Mobile app works via BLE.
- **Cons:** BLE stack is expensive (~50–80 KB RAM). Significant complexity.
  BLE is less useful if WiFi is available.
- **Effort:** ~8–10 weeks
- **RAM overhead:** ~65–105 KB (HTTP + WS + BLE stack)

### Option 4: Adapter Pattern via Our Cloud

Our ESP32 talks to our own MQTT cloud only. Our cloud backend has a Sprofiler
adapter that forwards shots and syncs profiles on behalf of the device.

```
┌──────────────────────┐  MQTT   ┌───────────────────┐  HTTPS  ┌────────────┐
│    Our ESP32          │────────►│   Our Cloud       │────────►│ sprofiler  │
│                       │         │                   │         │   .io      │
│  (No Sprofiler code   │         │  Sprofiler adapter│◄────────│            │
│   on the device)      │         │  translates MQTT  │         │            │
│                       │         │  → Sprofiler API  │         │            │
└──────────────────────┘         └───────────────────┘         └────────────┘
```

- **Pros:** Zero firmware overhead on ESP32. All translation happens in cloud.
  Easy to add/remove without firmware updates.
- **Cons:** Requires our cloud to be running. No direct Sprofiler access when
  offline. Extra latency (ESP32 → our cloud → Sprofiler).
- **Effort:** ~3–4 weeks (cloud-side only)
- **ESP32 RAM overhead:** 0 KB (no Sprofiler-specific code on device)

---

## Recommended Architecture

**Option 2 (Cloud API + Gaggiuino Local Emulation) is recommended** as the
primary approach, with **Option 4 available as a fallback**.

### Rationale

1. **Direct ESP32 → Sprofiler** is the user's explicit requirement ("reach to
   it from inside the ESP32 device, without relays").
2. **Gaggiuino local API emulation** gives users access to the entire
   Gaggiuino tooling ecosystem — Python API, MCP servers for AI assistants,
   GaggiMate dashboard — without needing Gaggiuino hardware.
3. **Combined with our MQTT cloud** (see `cloud_ideas.md`), users get the
   best of both worlds: our device management + Sprofiler community.
4. **BLE (Option 3) is deferred** — WiFi covers the primary use cases, and
   BLE's RAM cost is high. Can be added later if demand warrants it.

### Implementation Priorities

| Priority | Component | Effort | Dependency |
|---|---|---|---|
| **P0** | Shot upload to Sprofiler (HTTPS POST) | 1–2 weeks | API token, shot data format |
| **P1** | Profile download from Sprofiler | 1 week | API endpoint discovery |
| **P2** | Gaggiuino REST API emulation (`/status`, `/profiles`, `/shot/{id}`) | 2 weeks | HTTP server on ESP32 |
| **P3** | WebSocket live telemetry (Gaggiuino-compatible) | 1 week | WebSocket server |
| **P4** | Profile translation layer (Gaggiuino ↔ our format) | 1 week | Profile schema mapping |
| **P5** | BLE GATT server (Sprofiler mobile app compat) | 3–4 weeks | BLE stack integration |

---

## Implementation Roadmap

### Phase 1: Shot Upload (Weeks 1–2)

- Implement Gaggiuino-compatible shot JSON serialization on ESP32.
- Implement HTTPS POST to Sprofiler with Bearer token authentication.
- Add retry queue for failed uploads (LittleFS-backed).
- Add ESPHome config option for Sprofiler API token.
- Test with a real Sprofiler account.

### Phase 2: Profile Sync (Weeks 3–4)

- Implement profile list fetch from Sprofiler API.
- Implement profile download and Gaggiuino JSON parsing.
- Implement profile translation from Gaggiuino → our internal format.
- Store translated profiles in LittleFS for offline use.
- Add periodic sync (configurable interval, default 1 hour).

### Phase 3: Gaggiuino Local API (Weeks 5–6)

- Implement HTTP server with Gaggiuino-compatible endpoints.
- Map our machine state to Gaggiuino `/status` response format.
- Serve profiles in Gaggiuino JSON format via `/profiles`.
- Serve shot history via `/shot/{id}` and `/shot/latest`.
- Test with `gaggiuino_api` Python wrapper and MCP server.

### Phase 4: WebSocket Telemetry (Week 7)

- Implement WebSocket server for live shot telemetry.
- Stream datapoints at ~10 Hz during active shot.
- Format as Gaggiuino-compatible JSON.
- Test with GaggiMate dashboard and custom JavaScript clients.

### Phase 5: Bidirectional Profile Sharing (Week 8)

- Implement profile upload to Sprofiler (share with community).
- Implement our format → Gaggiuino translation for export.
- Add community profile browsing (if API supports it).

### Phase 6: BLE Integration — Deferred

- Implement BLE GATT server matching Sprofiler BLE protocol.
- Advertise custom service with pressure/temp/flow characteristics.
- Test with Sprofiler Android app.
- **Deferred until demand warrants the ~60 KB RAM cost.**

---

## Effort Estimation

### Total Effort by Component

| Component | C++ Lines (est.) | Weeks | RAM Cost |
|---|---|---|---|
| Shot JSON serializer | ~150–200 | 0.5 | ~2 KB (shared buffers) |
| HTTPS upload client | ~100–150 | 0.5 | ~5 KB (during upload) |
| Retry queue | ~100–150 | 0.5 | ~2 KB |
| Profile JSON parser | ~200–300 | 1 | ~4 KB (during parse) |
| Profile translator | ~200–300 | 1 | ~2 KB |
| Gaggiuino REST API server | ~400–600 | 2 | ~10 KB |
| WebSocket telemetry server | ~200–300 | 1 | ~5 KB |
| BLE GATT server | ~500–800 | 3 | ~60 KB |
| **Total (without BLE)** | **~1,350–2,000** | **~6.5** | **~25 KB** |
| **Total (with BLE)** | **~1,850–2,800** | **~9.5** | **~85 KB** |

### Comparison with Meticulous Integration

| Aspect | Sprofiler | Meticulous |
|---|---|---|
| API documentation | ❌ Undocumented | ✅ Open-source + OpenAPI |
| Auth complexity | ✅ Simple (Bearer token) | ⚠️ OAuth2/JWT |
| Profile format | ✅ Simple phases (JSON) | ⚠️ Complex stages + dynamics |
| Real-time protocol | ✅ WebSocket (standard) | ⚠️ Socket.IO (custom framing) |
| Upload model | ✅ Post-shot (simple POST) | ⚠️ Real-time streaming |
| Device registration | ✅ None needed (user token) | ❌ Hardware-locked |
| Community size | ✅ Large (Gaggiuino is biggest OSS espresso community) | ⚠️ Growing (newer) |
| Firmware effort | ~1,350–2,000 LOC | ~500–800 LOC (server only) |
| **Overall difficulty** | **Medium** | **Medium-High** |

---

## Risks & Challenges

### 1. Undocumented API

**Risk: HIGH.** Sprofiler's API is not publicly documented. We're relying on
reverse-engineering and community knowledge.

**Mitigation:**
- Contact Sprofiler maintainers (TesseraSkye) for API partnership.
- Monitor Gaggiuino firmware source for API calls (reference implementation).
- Implement graceful degradation — if API changes, uploads fail silently and
  shots are queued locally for retry.

### 2. API Stability

**Risk: MEDIUM.** Without versioning or public commitment, Sprofiler could
change endpoints or data formats without notice.

**Mitigation:**
- Pin to known-working API behavior observed in Gaggiuino firmware.
- Implement response validation and version detection.
- Log API errors for diagnostics.

### 3. Rate Limiting / Abuse Prevention

**Risk: LOW-MEDIUM.** If Sprofiler implements rate limiting, our devices might
get blocked during high-usage periods.

**Mitigation:**
- Respect reasonable upload frequency (one shot per minute max).
- Implement exponential backoff on errors.
- Request official API access with agreed rate limits.

### 4. Gaggiuino Format Evolution

**Risk: LOW.** The Gaggiuino profile format could evolve as the firmware adds
features (e.g., per-phase temperature, advanced flow curves).

**Mitigation:**
- Profile parser should be forward-compatible (ignore unknown fields).
- Monitor Gaggiuino firmware releases for format changes.
- Version our translation layer.

### 5. ESP32 Memory Pressure

**Risk: LOW.** The Sprofiler integration adds ~25 KB RAM (without BLE).

**Mitigation:**
- Make Sprofiler integration opt-in (compile flag or runtime config).
- Share HTTP client/server with other components.
- Upload shots asynchronously (don't block brew control loop).

### 6. Legal / Terms of Service

**Risk: LOW.** Sprofiler is open-source and community-oriented. Using their
API with a valid user account is a legitimate use case.

**Mitigation:**
- Reach out to maintainers for blessing.
- Don't scrape community data — only access user's own shots/profiles.
- Credit Sprofiler in documentation.

---

## Open Questions

1. **Should we contact TesseraSkye (Sprofiler maintainer) for API docs?**
   This would dramatically reduce integration risk and effort. We could
   propose a partnership: our firmware brings more users to their platform.

2. **Should Gaggiuino local API emulation be always-on or opt-in?**
   Running an HTTP server adds ~10 KB RAM. Opt-in means only users who
   need Gaggiuino tool compatibility pay the cost.

3. **Should we use the Gaggiuino profile format as our native format?**
   The format is simple, well-understood, and shared across the ecosystem.
   Alternatively, keep our own format and translate (more flexible but
   more code).

4. **How do we handle profiles that use features we don't support?**
   If a Gaggiuino profile uses per-phase temperature (future feature),
   we need a graceful fallback strategy.

5. **Should we support uploading to both Sprofiler AND Visualizer.coffee?**
   Many users may want shots in both platforms. This is straightforward
   with the adapter pattern (parallel uploads after each shot).

6. **What is Sprofiler's data retention policy?** Do shots expire?
   Is there a storage limit per user? This affects whether we can rely
   on Sprofiler as a long-term shot archive.

7. **Should we implement the Gaggiuino MCP server protocol?**
   The MCP (Model Context Protocol) server allows AI assistants to query
   machine status and shot data. If we emulate the Gaggiuino API, the
   existing MCP server should work with our machine automatically.

8. **Can we publish profiles to ShotProfiles.com too?**
   ShotProfiles uses the same Gaggiuino JSON format. If we implement
   Gaggiuino format support, ShotProfiles compatibility comes for free
   (manual import/export at minimum).

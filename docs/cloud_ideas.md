# Cloud Service Architecture — Ideas & Research

> **Status: Research / Planning — no code written yet.**
> This document captures protocol analysis, architecture options, and a recommended
> roadmap for adding cloud connectivity to the ESPHome Espresso Machine Controller.

---

## Table of Contents

1. [Motivation & Goals](#motivation--goals)
2. [Existing Espresso Cloud Services (Landscape)](#existing-espresso-cloud-services-landscape)
3. [Communication Protocol Analysis](#communication-protocol-analysis)
4. [Cloud Backend Options](#cloud-backend-options)
5. [Recommended Architecture](#recommended-architecture)
6. [MQTT Topic Design](#mqtt-topic-design)
7. [Cloud-First Profiles with Local Sync](#cloud-first-profiles-with-local-sync)
8. [Shot Logging & Graph Visualisation](#shot-logging--graph-visualisation)
9. [Recipe Storage (Grinder + Brew Settings)](#recipe-storage-grinder--brew-settings)
10. [Real-Time Communication & Machine Interaction](#real-time-communication--machine-interaction)
11. [Security Considerations](#security-considerations)
12. [Phased Implementation Roadmap](#phased-implementation-roadmap)
13. [Open Questions](#open-questions)

---

## Motivation & Goals

The cloud layer adds four capabilities that cannot be achieved with a purely local
ESPHome + Home Assistant setup:

| Capability | Why Cloud? |
|---|---|
| **Profile library** | Share, browse, and import community brew profiles without reflashing firmware |
| **Shot history & analytics** | Long-term storage, graphing, and comparison of every shot pulled — beyond HA's recorder retention |
| **Multi-device recipe sync** | A user with multiple machines (or multiple users in a café) keeps recipes in sync automatically |
| **Remote monitoring** | Check machine status, shot stats, and receive alerts from anywhere — not just the local network |

### Design Principles

1. **Local-first:** The machine must operate fully without cloud connectivity.
   Cloud is additive — never a dependency for brewing.
2. **Privacy-respecting:** No telemetry is sent unless the user explicitly opts in.
   Self-hosting must always be an option.
3. **Open protocol:** Use standard, well-documented protocols (MQTT, REST) so that
   third-party tools can integrate without proprietary SDKs.
4. **Compatibility:** Interoperate with the existing espresso community ecosystem
   (Visualizer.coffee, Sprofiler, ShotProfiles.com, Gaggiuino profiles).

---

## Existing Espresso Cloud Services (Landscape)

Before designing from scratch, we surveyed the existing ecosystem:

### Visualizer.coffee (Decent Espresso)

- **What:** Cloud platform for tracking, visualising, and sharing espresso shot data.
- **Tech:** Ruby on Rails + PostgreSQL; RESTful JSON API with OAuth 2.0.
- **Features:** Interactive pressure/flow/temperature charts; shot comparison; profile
  remix & download; metadata (beans, grinder, yield, TDS).
- **API:** Public OpenAPI spec — `POST /api/shots/upload`, `GET /api/shots`, etc.
- **Compatibility:** Supports Decent DE1, Beanconqueror, Gaggiuino, Meticulous.
- **Takeaway:** The gold standard for shot visualisation. We should aim for
  Visualizer-compatible shot export (JSON) so users can push shots there too.

### Sprofiler (Gaggiuino)

- **What:** Open-source shot tracking and community profile sharing for Gaggiuino machines.
- **Tech:** Vue.js frontend; RESTful backend; Android app (Capacitor JS); ESP32 C++ integration.
- **Features:** Shot data tracking (weight, temperature, duration); cloud sync;
  community profile download/upload.
- **Takeaway:** Close to our use case. Proves that an ESP32 device can push shot
  data to a lightweight cloud service in real-time.

### ShotProfiles.com

- **What:** Community-curated profile library for Gaggiuino/GaggiaMate.
- **Features:** Browse, rate, and download extraction profiles as JSON files.
- **Takeaway:** A profile-marketplace model worth supporting as an import source.

### Gaggiuino MCP Server

- **What:** Local/cloud API server exposing machine telemetry, shot history, and
  profile management to AI assistants and third-party clients.
- **Takeaway:** Interesting pattern — exposing machine data via a lightweight API
  server that can run locally or in the cloud.

---

## Communication Protocol Analysis

We evaluated five protocols for the ESP32 ↔ Cloud link:

### Protocol Comparison Matrix

| Criterion | MQTT | WebSocket | gRPC | CoAP | ESPHome Native API |
|---|---|---|---|---|---|
| **Transport** | TCP | TCP | HTTP/2 (TCP) | UDP | TCP (protobuf) |
| **Model** | Pub/Sub (broker) | Full-duplex | RPC (streaming) | Request/Response | Client/Server |
| **ESP32 support** | Excellent | Good | Poor (heavy) | Good | Excellent (local only) |
| **Offline resilience** | Excellent (QoS, retained msgs, broker queue) | None | None | Moderate | None |
| **Browser support** | Via MQTT-over-WebSocket | Native | gRPC-Web (limited) | No | No |
| **Payload efficiency** | Binary (small header, any payload) | Text/Binary (no framing) | Protobuf (very compact) | Binary (very compact) | Protobuf (compact) |
| **Scalability** | Excellent (broker handles fan-out) | Manual | Excellent (backend) | Moderate | Poor (1:1) |
| **Cloud ecosystem** | AWS IoT Core, HiveMQ, EMQX, Mosquitto | Any web server | Cloud-native backends | Niche | Home Assistant only |
| **QoS guarantees** | 0 (fire-forget), 1 (at-least-once), 2 (exactly-once) | None built-in | Streaming ACK | Confirmable/Non-conf | None |

### Protocol Verdicts

**MQTT — Primary recommendation for ESP32 ↔ Cloud**

MQTT is purpose-built for IoT. Key advantages for this project:
- Lightweight enough for ESP32 (ESPHome has built-in `mqtt:` component).
- Publish/subscribe decouples the machine from the cloud — the machine publishes
  regardless of whether anyone is listening.
- QoS 1 ensures shot data is delivered at least once, even over flaky Wi-Fi.
- Retained messages provide instant state on reconnection (current profile,
  machine status).
- The broker queues messages while the device is offline — no data loss.
- Mature ESP32 libraries: `ESP-MQTT` (ESP-IDF), `PubSubClient` (Arduino),
  ESPHome's built-in MQTT client.
- MQTT-over-WebSocket allows browser dashboards to subscribe directly to live
  shot data without a custom backend.

**WebSocket — Secondary, for real-time browser dashboards**

WebSocket is ideal for the browser-side of the cloud dashboard (live shot
graphs, remote control panel). The recommended pattern:
- ESP32 → MQTT broker → Cloud backend → WebSocket → Browser.
- Most cloud MQTT brokers (HiveMQ, EMQX) natively support MQTT-over-WebSocket,
  so the browser can subscribe to MQTT topics directly — no custom WebSocket
  server needed.

**CoAP — Not recommended**

CoAP (over UDP) is lighter than MQTT but lacks pub/sub, broker-based queuing,
and has poor cloud ecosystem support. It is better suited for battery-powered
sensor mesh networks, not a mains-powered espresso machine with persistent
Wi-Fi.

**gRPC — Not recommended for device-side**

gRPC is excellent for backend microservices but too heavy for ESP32 firmware.
Could be used cloud-side (backend API) but adds unnecessary complexity when
MQTT + REST cover all needs.

**ESPHome Native API — Keep for Home Assistant, not for cloud**

The native API uses protobuf over TCP and is highly optimised for local
Home Assistant communication. However, it is:
- Not cloud-scalable (1:1 client-server, no broker).
- Not compatible with third-party cloud services.
- Not designed for intermittent connectivity.

**Recommendation:** Keep the ESPHome native API for local HA integration (it's
superior for that). Add MQTT as a parallel channel for cloud connectivity. This
is a supported ESPHome pattern — both `api:` and `mqtt:` can coexist.

---

## Cloud Backend Options

### Option A: Managed MQTT Broker + Serverless Backend (Recommended)

```
ESP32 ──MQTT──► Cloud MQTT Broker ──► Serverless Functions ──► Database
                     │                                            │
                     └──MQTT/WS──► Browser Dashboard ◄───REST────┘
```

| Component | Options |
|---|---|
| MQTT Broker | HiveMQ Cloud (free: 100 devices), EMQX Cloud, AWS IoT Core, self-hosted Mosquitto |
| Serverless Functions | AWS Lambda, Cloudflare Workers, Supabase Edge Functions, Vercel |
| Database | PostgreSQL (Supabase), DynamoDB, InfluxDB (time-series), TimescaleDB |
| Frontend | React/Vue SPA subscribed to MQTT-over-WebSocket |
| Auth | Supabase Auth, Auth0, Firebase Auth |

**Pros:** Scalable, low ops burden, pay-per-use, community-familiar.
**Cons:** Vendor lock-in risk (mitigated by using standard MQTT + PostgreSQL).

### Option B: Self-Hosted Open-Source Stack

```
ESP32 ──MQTT──► Mosquitto (local/VPS) ──► Node.js/Python backend ──► PostgreSQL
                     │                                                    │
                     └──MQTT/WS──► Browser Dashboard ◄──────REST──────────┘
```

| Component | Options |
|---|---|
| MQTT Broker | Mosquitto, NanoMQ, VerneMQ |
| Backend | Node.js (Express/Fastify), Python (FastAPI), Go |
| Database | PostgreSQL + TimescaleDB, InfluxDB |
| Frontend | Same as Option A |
| Auth | Keycloak, self-managed JWT |

**Pros:** Full control, no vendor lock-in, privacy-friendly, Docker-composable.
**Cons:** Requires user to maintain infrastructure.

### Option C: Backend-as-a-Service (BaaS)

| Service | Database | Real-time | Self-host? | Free Tier |
|---|---|---|---|---|
| **Supabase** | PostgreSQL | Row-level subscriptions | Yes | Generous |
| **Firebase** | Firestore (NoSQL) | Excellent | No | Moderate |
| **Appwrite** | MariaDB | WebSocket | Yes | Generous |

**Supabase is the strongest BaaS candidate** for this project:
- PostgreSQL allows complex SQL queries on shot history (aggregations, comparisons).
- Built-in real-time subscriptions (via Postgres logical replication) for live dashboards.
- Row-level security policies for multi-user recipe sharing.
- Self-hostable (Docker) — respects our privacy principle.
- Edge Functions for MQTT ingestion (bridge MQTT → Supabase via a thin function).

**Firebase** is strong for rapid prototyping but is proprietary and NoSQL — less
ideal for relational recipe/profile data and long-term analytics queries.

### Managed MQTT Broker Comparison

| Feature | HiveMQ Cloud | EMQX Cloud | AWS IoT Core | Mosquitto (self-hosted) |
|---|---|---|---|---|
| MQTT version | 3.x, 5.0 full | 3.x, 5.0 full | 3.1.1, partial 5.0 | 3.x, 5.0 |
| Free tier | 100 devices | 14-day trial | None | Free (self-hosted) |
| WebSocket support | Yes | Yes | Yes | Yes (with config) |
| Device shadows | No (use retained msgs) | No | Yes (native) | No |
| ESP32 TLS | Yes | Yes | Yes (mTLS) | Yes |
| Best for | Small-medium, easy setup | Large-scale, enterprise | AWS-native stacks | Self-hosters |

**Recommendation:** HiveMQ Cloud for hosted (generous free tier, full MQTT 5.0);
Mosquitto for self-hosted.

---

## Recommended Architecture

Based on the analysis above, we recommend a **hybrid MQTT + REST architecture**:

```
┌─────────────────────────────────────────────────────────────────────┐
│                        ESP32 (ESPHome)                              │
│                                                                     │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────────┐ │
│  │ Native API  │  │ MQTT Client  │  │  espresso_machine (C++)    │ │
│  │ (→ HA)      │  │ (→ Cloud)    │  │  brew/steam state machines │ │
│  └──────┬──────┘  └──────┬───────┘  └────────────────────────────┘ │
│         │                │                                          │
└─────────┼────────────────┼──────────────────────────────────────────┘
          │                │
    ┌─────▼─────┐   ┌─────▼──────────────────────────────────┐
    │   Home    │   │          Cloud MQTT Broker              │
    │ Assistant │   │   (HiveMQ Cloud / Mosquitto)            │
    │  (local)  │   │                                         │
    └───────────┘   │  Topics:                                │
                    │    espresso/{device_id}/shot/live        │
                    │    espresso/{device_id}/shot/complete    │
                    │    espresso/{device_id}/status           │
                    │    espresso/{device_id}/profile/active   │
                    │    espresso/{device_id}/profile/sync     │
                    │    espresso/{device_id}/recipe/sync      │
                    │    espresso/{device_id}/command/#        │
                    └──────────┬──────────────────────────────┘
                               │
                    ┌──────────▼──────────────────────────────┐
                    │        Cloud Backend                     │
                    │  (Supabase / custom Node.js/Python)      │
                    │                                          │
                    │  ┌────────────┐  ┌────────────────────┐ │
                    │  │ MQTT       │  │  REST API           │ │
                    │  │ Subscriber │  │  /api/shots         │ │
                    │  │ (ingestion)│  │  /api/profiles      │ │
                    │  └─────┬──────┘  │  /api/recipes       │ │
                    │        │         │  /api/devices        │ │
                    │        ▼         └─────────┬────────────┘ │
                    │  ┌─────────────┐           │             │
                    │  │ PostgreSQL  │◄──────────┘             │
                    │  │ (+ Timescale│                          │
                    │  │  extension) │                          │
                    │  └─────────────┘                          │
                    └──────────┬───────────────────────────────┘
                               │
                    ┌──────────▼──────────────────────────────┐
                    │       Web Dashboard / Mobile App         │
                    │                                          │
                    │  • Live shot graph (MQTT-over-WebSocket) │
                    │  • Shot history & comparison (REST)      │
                    │  • Profile library (REST + MQTT sync)    │
                    │  • Recipe management (REST)              │
                    │  • Remote machine status (MQTT/WS)       │
                    └──────────────────────────────────────────┘
```

### Data Flow Summary

| Flow | Protocol | Direction | Latency | Payload |
|---|---|---|---|---|
| Live shot telemetry | MQTT QoS 0 | Device → Cloud | ~50–200 ms | JSON (temp, pressure, flow, weight) at 2–10 Hz |
| Completed shot upload | MQTT QoS 1 | Device → Cloud | Best-effort | JSON (full shot time-series + metadata) |
| Machine status | MQTT QoS 1 retained | Device → Cloud | On change | JSON (state, active profile, temps) |
| Profile sync (cloud→device) | MQTT QoS 1 | Cloud → Device | On change | JSON (profile definition) |
| Recipe sync (cloud→device) | MQTT QoS 1 | Cloud → Device | On change | JSON (recipe: grinder + brew params) |
| Commands (remote brew start) | MQTT QoS 1 | Cloud → Device | ~100–500 ms | JSON (action + params) |
| Shot history query | REST (HTTPS) | Dashboard → Backend | ~200–500 ms | JSON (paginated list) |
| Profile library browse | REST (HTTPS) | Dashboard → Backend | ~200–500 ms | JSON (profile list) |

---

## MQTT Topic Design

### Namespace Convention

```
espresso/{device_id}/{category}/{subcategory}
```

Where `{device_id}` is a unique identifier for each machine (e.g., the ESPHome
device name or a UUID assigned at first cloud registration).

### Topic Hierarchy

```
espresso/{device_id}/
├── status                      # Retained: machine state (idle, brewing, steaming, off)
├── status/temperatures         # Retained: current boiler/group temps
├── status/config               # Retained: current device configuration summary
│
├── shot/
│   ├── live                    # Streaming: real-time shot telemetry (2-10 Hz during brew)
│   │                           #   payload: { "t_ms": 1234, "temp_c": 93.2, "flow_ml_s": 2.1,
│   │                           #              "pressure_bar": 9.0, "volume_ml": 15.3,
│   │                           #              "weight_g": 14.8 }
│   ├── complete                # QoS 1: full shot record published at end of brew
│   │                           #   payload: { "id": "uuid", "profile": "Classic 9-Bar",
│   │                           #              "duration_s": 28, "volume_ml": 36,
│   │                           #              "timeseries": [...], "metadata": {...} }
│   └── stats                   # Retained: last shot statistics for quick dashboard display
│
├── profile/
│   ├── active                  # Retained: currently active profile (name + full definition)
│   ├── list                    # Retained: array of all profile names on device
│   ├── sync/request            # Cloud → Device: push a new/updated profile
│   ├── sync/ack                # Device → Cloud: confirm profile received and stored
│   └── delete                  # Cloud → Device: remove a profile by name
│
├── recipe/
│   ├── active                  # Retained: current recipe (grinder + brew settings)
│   ├── list                    # Retained: array of all recipe names on device
│   ├── sync/request            # Cloud → Device: push a new/updated recipe
│   ├── sync/ack                # Device → Cloud: confirm recipe received
│   └── delete                  # Cloud → Device: remove a recipe by name
│
└── command/
    ├── brew/start              # Cloud → Device: start brew (optional profile override)
    ├── brew/stop               # Cloud → Device: stop brew
    ├── steam/start             # Cloud → Device: start steam
    ├── steam/stop              # Cloud → Device: stop steam
    ├── grind/start             # Cloud → Device: start grind (optional duration)
    └── flush                   # Cloud → Device: run flush cycle
```

### Topic Design Rationale

- **Retained messages** on `status/*`, `profile/active`, `recipe/active`:
  ensures any new subscriber (dashboard, mobile app) immediately gets the
  current state without waiting for the next publish cycle.
- **QoS 0** for live shot telemetry: high-frequency data where losing an
  occasional sample is acceptable; keeps bandwidth low.
- **QoS 1** for completed shots, profile/recipe sync, and commands: ensures
  delivery at least once.
- **Hierarchical structure** enables wildcard subscriptions:
  `espresso/+/shot/#` subscribes to all shot data from all devices;
  `espresso/my_machine/profile/#` subscribes to all profile events for one machine.

---

## Cloud-First Profiles with Local Sync

The issue specifically requests that profiles live in the cloud with local sync,
so the user never needs to push/import profiles into the machine directly.

### Architecture: Device Shadow Pattern

Inspired by AWS IoT Device Shadows, we use a **desired/reported state** model
over MQTT:

```
┌──────────────┐     MQTT       ┌──────────────────┐      REST     ┌──────────┐
│   ESP32      │ ◄────────────► │  Cloud Backend    │ ◄───────────► │ Web App  │
│              │                │                    │               │          │
│  reported:   │  ──publish──►  │  shadow:           │               │  User    │
│  {profiles}  │                │  {desired,reported}│  ◄──edit───── │  edits   │
│              │  ◄─subscribe─  │                    │               │ profiles │
│  desired:    │                │  delta = desired   │               │          │
│  (from cloud)│                │         - reported │               │          │
└──────────────┘                └──────────────────┘               └──────────┘
```

### Sync Flow

1. **User creates/edits a profile** on the web dashboard or mobile app.
2. **Cloud stores the profile** in PostgreSQL and publishes the updated profile
   set as the "desired" state to `espresso/{device_id}/profile/sync/request`.
3. **ESP32 receives the profile** via MQTT subscription, validates it, and stores
   it in LittleFS/NVS flash.
4. **ESP32 acknowledges** by publishing its current profile set (the "reported"
   state) to `espresso/{device_id}/profile/sync/ack`.
5. **Cloud computes delta**: if desired ≠ reported, it re-publishes missing
   profiles. This handles missed messages, reboots, and partial updates.
6. **On ESP32 boot**: the device publishes its current profiles to
   `profile/sync/ack`. The cloud detects any delta and pushes missing profiles.

### Key Design Decisions

- **Flash storage on ESP32:** Profiles are stored in LittleFS (or NVS for small
  profiles). This ensures the machine works without cloud after initial sync.
- **Version vector:** Each profile carries a `version: <integer>` field. The
  cloud increments version on every edit. The device always accepts the higher
  version. This prevents stale overwrites.
- **Maximum profiles on device:** ESP32 flash is limited. We cap at ~20 profiles
  on-device. The cloud stores unlimited; the user selects which 20 are synced to
  the machine via a "favourites" mechanism.
- **Offline operation:** If the cloud is unreachable, the device uses whatever
  profiles are in flash. No functionality is lost — only sync pauses.

### Profile JSON Schema (Cloud ↔ Device)

```json
{
  "id": "uuid-v4",
  "name": "Classic 9-Bar",
  "version": 3,
  "temperature_c": 93.0,
  "phases": [
    {
      "name": "Pre-infusion",
      "type": "pressure",
      "target_bar": 2.0,
      "duration_s": 7,
      "ramp": "linear"
    },
    {
      "name": "Extraction",
      "type": "pressure",
      "target_bar": 9.0,
      "exit": { "volume_ml": 36 }
    }
  ],
  "metadata": {
    "author": "user@example.com",
    "created_at": "2025-03-15T10:30:00Z",
    "tags": ["classic", "medium-roast"],
    "source": "visualizer"
  }
}
```

This schema is compatible with the planned `espresso_machine_profile:` platform
(see `docs/profiles.md`) and is a superset of the Gaggiuino/GaggiaMate JSON format.

---

## Shot Logging & Graph Visualisation

### What Gets Logged

Every shot produces a time-series record plus summary metadata:

| Field | Type | Source | Rate |
|---|---|---|---|
| `timestamp_ms` | uint32 | ESP32 `millis()` | Per sample |
| `temperature_c` | float | Thermocouple sensor | 2–10 Hz |
| `flow_ml_s` | float | Flow meter rate sensor | 2–10 Hz |
| `volume_ml` | float | Flow meter total sensor | 2–10 Hz |
| `pressure_bar` | float | Pressure transducer (future) | 2–10 Hz |
| `weight_g` | float | Scale (future) | 2–10 Hz |
| `pump_duty_pct` | float | Pump controller | 2–10 Hz |

Plus summary metadata:
- Profile used, recipe used, total duration, total volume, total weight.
- Bean info (optional, from cloud/app metadata — not on ESP32).
- Grinder setting, dose weight (from recipe).

### Data Path

```
ESP32 (during brew)                    Cloud
─────────────────                      ─────
Brew loop @ 5 Hz  ──MQTT QoS 0──►     Live topic: espresso/{id}/shot/live
   (streaming)                              │
                                            ▼
                                       WebSocket → Browser (live graph)

Brew completes     ──MQTT QoS 1──►     Complete topic: espresso/{id}/shot/complete
   (full record)                            │
                                            ▼
                                       Cloud backend → PostgreSQL
                                            │
                                            ▼
                                       REST API → Dashboard (history, analytics)
```

### Live Graphing

The browser dashboard subscribes to `espresso/{device_id}/shot/live` via
MQTT-over-WebSocket. This gives sub-second real-time graphing with no custom
server-side logic — the MQTT broker handles fan-out to all connected dashboards.

Libraries for browser-side MQTT + charting:
- **MQTT.js** — browser MQTT client (works over WebSocket).
- **Chart.js** or **Lightweight Charts (TradingView)** — real-time line charts.
- **uPlot** — high-performance time-series charting (minimal DOM overhead).

### Shot Storage Schema (PostgreSQL)

```sql
CREATE TABLE shots (
  id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id     TEXT NOT NULL,
  user_id       UUID REFERENCES users(id),
  profile_name  TEXT,
  recipe_name   TEXT,
  started_at    TIMESTAMPTZ NOT NULL,
  duration_s    REAL,
  volume_ml     REAL,
  weight_g      REAL,
  yield_ml      REAL,
  avg_temp_c    REAL,
  avg_flow_ml_s REAL,
  metadata      JSONB,          -- beans, grinder, notes, TDS, EY
  created_at    TIMESTAMPTZ DEFAULT now()
);

-- Time-series data (TimescaleDB hypertable for efficient range queries)
CREATE TABLE shot_samples (
  shot_id       UUID REFERENCES shots(id) ON DELETE CASCADE,
  t_ms          INTEGER NOT NULL,     -- ms since shot start
  temp_c        REAL,
  flow_ml_s     REAL,
  volume_ml     REAL,
  pressure_bar  REAL,
  weight_g      REAL,
  pump_duty_pct REAL
);
-- With TimescaleDB: SELECT create_hypertable('shot_samples', 't_ms');
```

### Visualizer.coffee Compatibility

To support exporting shots to Visualizer.coffee, we generate a JSON payload
matching Visualizer's upload format:

```
POST https://visualizer.coffee/api/shots/upload
Content-Type: application/json
Authorization: Bearer <user_token>

{
  "espresso_elapsed": [0.0, 0.5, 1.0, ...],
  "espresso_pressure": [0.0, 2.1, 4.5, ...],
  "espresso_flow": [0.0, 0.8, 1.5, ...],
  "espresso_temperature_mix": [93.0, 93.1, 92.9, ...],
  "espresso_weight": [0.0, 0.5, 1.2, ...],
  ...
}
```

This can be done either:
- **On-device:** ESP32 formats and POSTs directly (uses more RAM).
- **Via cloud backend:** Cloud backend receives our native shot JSON, transforms
  to Visualizer format, and forwards — more efficient and allows retry.

---

## Recipe Storage (Grinder + Brew Settings)

A **recipe** bundles everything needed to reproduce a specific coffee:

```json
{
  "id": "uuid-v4",
  "name": "Ethiopia Yirgacheffe — Light",
  "version": 2,
  "profile_id": "uuid-of-linked-profile",
  "grinder": {
    "setting": 15,
    "dose_g": 18.0,
    "grind_time_s": 7.5
  },
  "brew": {
    "target_temperature_c": 93.0,
    "flow_max_ml": 36.0,
    "flow_offset_ml": 20.0,
    "pre_infusion": {
      "volume_ml": 5.0,
      "hold_time_s": 3.0
    }
  },
  "bean": {
    "name": "Ethiopia Yirgacheffe",
    "roaster": "Square Mile",
    "roast_date": "2025-02-01",
    "roast_level": "light"
  },
  "notes": "18g in, 36g out, 28s. Sweet and floral.",
  "metadata": {
    "author": "user@example.com",
    "created_at": "2025-02-10T08:15:00Z",
    "tags": ["light-roast", "single-origin", "fruity"]
  }
}
```

### Recipe ↔ Profile Relationship

- A recipe **references** a profile by `profile_id`.
- When a recipe is activated, the ESP32 loads the linked profile and applies
  the grinder + brew settings.
- Multiple recipes can reference the same profile (e.g. same pressure curve but
  different beans and doses).

### Recipe Sync

Recipes follow the same desired/reported sync pattern as profiles (see above).
The MQTT topics are:
- `espresso/{device_id}/recipe/sync/request` — cloud pushes new/updated recipes.
- `espresso/{device_id}/recipe/sync/ack` — device confirms.
- `espresso/{device_id}/recipe/active` — retained: the currently active recipe.

### Recipe Selection from Home Assistant

When synced, recipes appear as options in a `select` entity:

```
Home Assistant entity: select.philips_barista_brew_active_recipe
Options: ["Ethiopia Yirgacheffe — Light", "Colombia Huila — Medium", "Espresso Blend — Dark"]
```

Changing the selection applies the recipe's grinder + brew settings and activates
the linked profile — a single tap from preparation to execution.

---

## Real-Time Communication & Machine Interaction

### Is Real-Time Appropriate?

**Yes — for monitoring.** Users want to watch a live shot graph from their phone
or a wall-mounted tablet. Sub-second latency (achievable with MQTT QoS 0) is
sufficient — humans perceive 200 ms as "instant."

**Partially — for remote control.** Starting a brew remotely requires:
1. Confirming the machine is in a ready state (portafilter locked, water tank full).
2. Acknowledging the safety implications (hot water under pressure, unattended).

Remote brew start is useful for café workflows (barista starts grind from the
counter) but should be **gated behind explicit user confirmation** on the app side.

**No — for safety-critical control.** The PID loop, over-temperature cutoff,
and valve interlocks must never depend on cloud connectivity. These run locally
on the ESP32 at all times.

### Interaction Model

```
┌────────────────────┐          MQTT           ┌────────────────────┐
│    Cloud / App     │ ───command/brew/start──► │      ESP32         │
│                    │                          │                    │
│                    │ ◄──status (retained)──── │  State machine     │
│                    │ ◄──shot/live (stream)─── │  reports state     │
│                    │ ◄──shot/complete──────── │  on every change   │
│                    │                          │                    │
│  Dashboard shows:  │                          │  Validates command │
│  - Machine state   │                          │  before executing  │
│  - Live graph      │                          │  (e.g. must be     │
│  - Shot history    │                          │   idle + heated)   │
└────────────────────┘                          └────────────────────┘
```

### Command Acknowledgment Protocol

Commands sent to `espresso/{device_id}/command/*` are acknowledged via the
`status` topic. The pattern:

1. App publishes command:
   ```json
   {"action": "brew_start", "profile": "Classic 9-Bar", "request_id": "abc123"}
   ```
2. ESP32 validates and either:
   - Transitions to HEATING and publishes status update:
     ```json
     {"state": "heating", "request_id": "abc123", "profile": "Classic 9-Bar"}
     ```
   - Rejects and publishes error:
     ```json
     {"state": "idle", "error": "Machine not ready: temp too low", "request_id": "abc123"}
     ```
3. App correlates by `request_id` to show success/failure in the UI.

### Latency Budget

| Path | Target Latency | Protocol |
|---|---|---|
| Shot telemetry → Live graph | < 500 ms | MQTT QoS 0 + WS |
| Remote command → State change | < 2 s | MQTT QoS 1 |
| Profile sync → Flash write | < 5 s | MQTT QoS 1 |
| Shot upload → Database | < 10 s | MQTT QoS 1 |

---

## Security Considerations

### Transport Security

- **MQTT over TLS:** All MQTT connections from ESP32 to the cloud broker use
  TLS 1.2+ (port 8883). ESPHome's MQTT component supports TLS natively.
- **Certificate pinning:** For managed brokers, pin the CA certificate in ESP32
  firmware to prevent MITM attacks.
- **Authentication:** Each device authenticates to the broker with a unique
  username/password pair or X.509 client certificate (for AWS IoT Core).

### Application Security

- **Device registration:** Devices must be registered with the cloud backend
  before they can publish. A one-time registration flow generates a device token.
- **Topic ACLs:** The MQTT broker enforces that a device can only publish to
  its own `espresso/{device_id}/*` topics and subscribe to its own
  `espresso/{device_id}/command/*` and `espresso/{device_id}/*/sync/*` topics.
- **User authentication:** The web dashboard and API require user login
  (OAuth 2.0 / email+password). Users can only access their own devices and data.
- **Rate limiting:** The cloud backend rate-limits shot uploads and profile syncs
  to prevent abuse (e.g., max 100 shots/day, max 50 profile syncs/day).

### Privacy

- **Opt-in only:** Cloud connectivity is disabled by default. The user enables it
  by adding an `mqtt:` block to their ESPHome YAML.
- **Data minimisation:** Only shot data and machine state are sent — no personal
  information, no location, no usage analytics.
- **Self-hosting:** The entire cloud stack (Mosquitto + backend + PostgreSQL) can
  be self-hosted. We provide Docker Compose manifests and documentation.
- **Data export/delete:** The REST API supports full data export (JSON) and
  account deletion (GDPR compliance).

---

## Phased Implementation Roadmap

### Phase A — MQTT Foundation (Firmware)

- [ ] Add `mqtt:` component to example YAML configs (alongside existing `api:`)
- [ ] Implement `cloud_status_publisher` in C++ — publishes machine state, temperatures,
      and active profile as retained messages on state change
- [ ] Implement `cloud_shot_publisher` — publishes live telemetry during brew (QoS 0)
      and full shot record on completion (QoS 1)
- [ ] Test with local Mosquitto broker + MQTT Explorer

### Phase B — Cloud Backend MVP

- [ ] Set up cloud MQTT broker (HiveMQ Cloud free tier or Mosquitto on VPS)
- [ ] Build MQTT ingestion service (Node.js or Python) that subscribes to
      `espresso/+/shot/complete` and writes to PostgreSQL
- [ ] Build REST API: `GET /api/shots`, `GET /api/shots/:id`, `GET /api/devices`
- [ ] Implement user authentication (Supabase Auth or simple JWT)
- [ ] Deploy to cloud (Fly.io / Railway / self-hosted Docker)

### Phase C — Web Dashboard MVP

- [ ] Build SPA (React or Vue) with:
  - Shot history list with search/filter
  - Shot detail page with interactive chart (temperature, flow, pressure vs time)
  - Device status page (live via MQTT-over-WebSocket)
- [ ] MQTT-over-WebSocket for live shot graphing

### Phase D — Profile & Recipe Cloud Sync

- [ ] Implement desired/reported profile sync over MQTT (firmware + backend)
- [ ] Implement LittleFS profile storage on ESP32
- [ ] Build profile editor in web dashboard
- [ ] Build recipe CRUD in REST API + dashboard
- [ ] Implement recipe sync over MQTT

### Phase E — Community Features

- [ ] Public profile library (browse, rate, fork community profiles)
- [ ] Visualizer.coffee shot export integration
- [ ] ShotProfiles.com and Gaggiuino JSON profile import
- [ ] Sharing: share a shot graph or recipe via public URL

### Phase F — Mobile App (Optional)

- [ ] React Native or Flutter app wrapping the same MQTT + REST APIs
- [ ] Push notifications for shot completion, machine alerts

---

## Open Questions

1. **MQTT 3.1.1 vs 5.0?** ESPHome's built-in MQTT uses 3.1.1. MQTT 5.0 adds
   topic aliases (bandwidth savings), user properties (metadata on messages),
   and response topics (request/reply pattern). Should we require 5.0, or
   design for 3.1.1 compatibility with optional 5.0 enhancements?

2. **Shot data sampling rate?** 2 Hz is sufficient for most shot graphs. 10 Hz
   gives smoother curves but 5× the data. Should we make this configurable, or
   fix at 5 Hz as a compromise?

3. **Profile storage format on ESP32?** Options:
   - LittleFS file per profile (flexible, easy to manage, but slower reads).
   - NVS key-value entries (fast, but limited to ~15 KB per entry).
   - Compiled into firmware at build time (current `espresso_machine_profile:`
     approach — no runtime edits possible).
   Recommendation: LittleFS for cloud-synced profiles; compiled-in for
   user-defined YAML profiles. Both should coexist.

4. **Community profile moderation?** If we host a public profile library, do we
   need moderation (flag inappropriate content, verify profile safety)?
   Initially, yes — a simple upvote/report system.

5. **Multi-user access?** In a café setting, multiple baristas may need access to
   the same machine's data. The cloud backend should support team/organisation
   accounts with role-based access (admin, barista, viewer).

6. **Visualizer.coffee as the primary cloud, or our own?** Some users may prefer
   to use Visualizer.coffee exclusively. We should support "forward-only" mode
   where shots are pushed to Visualizer's API without our own backend.

7. **InfluxDB vs TimescaleDB for time-series?** Both are excellent. TimescaleDB
   extends PostgreSQL (single database for everything); InfluxDB is purpose-built
   for time-series but adds a second database to manage. Recommendation:
   TimescaleDB for simplicity.

8. **Home Assistant as cloud bridge?** An alternative to direct ESP32 → cloud MQTT
   is to let HA act as the bridge (HA receives data via native API, then forwards
   to cloud MQTT or REST). This avoids adding MQTT to the ESP32 firmware but makes
   HA a required intermediary. Recommend against this — it violates the
   "local-first, cloud-additive" principle and adds latency.

# Alternative Cloud Services & Integration Exploration

> **Status: Research / Planning — no code written yet.**
> This document evaluates existing espresso cloud platforms for device management
> capabilities and explores how we can integrate with them — either individually
> or as a unified multi-platform strategy.

---

## Table of Contents

1. [Background & Motivation](#background--motivation)
2. [Platform-by-Platform Device Management Analysis](#platform-by-platform-device-management-analysis)
3. [Additional Platforms Discovered](#additional-platforms-discovered)
4. [Device Management Feature Comparison Matrix](#device-management-feature-comparison-matrix)
5. [Integration Strategy Options](#integration-strategy-options)
6. [Recommended Strategy: Multi-Backend Adapter Pattern](#recommended-strategy-multi-backend-adapter-pattern)
7. [Integration Architecture](#integration-architecture)
8. [Per-Platform Integration Details](#per-platform-integration-details)
9. [What Each Platform Would Give Our Users](#what-each-platform-would-give-our-users)
10. [Risks & Considerations](#risks--considerations)
11. [Open Questions](#open-questions)

---

## Background & Motivation

In `docs/cloud_ideas.md` we designed a standalone cloud architecture (MQTT +
REST backend + PostgreSQL). Before building from scratch, we should investigate
whether any **existing** espresso cloud platform already provides the device
management, shot logging, profile sync, or community features our users need —
and whether we can integrate with them to give our users those capabilities
without re-inventing the wheel.

The key question from the issue:

> _"Do any of those [surveyed platforms] allow for device management somehow?
> Can we explore integrating with a device management cloud — or with all of
> them — and have the features they provide to other firmwares also?"_

---

## Platform-by-Platform Device Management Analysis

### 1. Visualizer.coffee

| Aspect | Details |
|---|---|
| **Operator** | Miha Rekar (independent, open-source) |
| **Source** | [github.com/miharekar/visualizer](https://github.com/miharekar/visualizer) — Ruby on Rails + PostgreSQL |
| **Device management?** | **No direct device management.** Visualizer is a shot-data and profile platform, not a device control plane. It does not register machines, push firmware, or send commands. |
| **What it does** | Shot upload/download (JSON/TCL), profile visualisation, community sharing, coffee/roaster metadata, comparison tools. |
| **Auth** | OAuth 2.0 (scopes: `read`, `upload`, `write`) + HTTP Basic (deprecated). |
| **API** | RESTful, OpenAPI 3.1.0 spec — fully documented at [apidocs.visualizer.coffee](https://apidocs.visualizer.coffee/reference). |
| **Key endpoints** | `GET /me`, `POST /shots/upload`, `GET /shots`, `GET /shots/{id}/profile`, `GET /roasters`, `GET /coffee_bags`. |
| **Multi-device** | Supports Decent DE1, Gaggiuino, GaggiaMate, Beanconqueror, Meticulous, Smart Espresso Profiler. |
| **Integration value** | **High.** Visualizer is the community standard for shot data. Uploading shots here gives users access to the largest shot comparison community. |
| **Device management gap** | No machine registration, no OTA, no real-time control, no profile push-to-device. |

### 2. Sprofiler

| Aspect | Details |
|---|---|
| **Operator** | Community / TesseraSkye (open-source) |
| **Source** | [github.com/TesseraSkye/sprofiler](https://github.com/TesseraSkye/sprofiler) — Vue.js + backend (RESTful) + Android (Capacitor) |
| **Device management?** | **Partial.** Sprofiler syncs profiles between the cloud and Gaggiuino devices, but does not provide full device lifecycle management (no OTA, no command channel, no fleet management). |
| **What it does** | Shot tracking (weight, temp, duration), cloud profile storage, community profile sharing, analytics. |
| **Auth** | Account-based (email/password); details not fully documented publicly. |
| **API** | RESTful backend — endpoints for shot upload, profile CRUD, community browse. Public API docs are sparse; integration is primarily through the companion app. |
| **Multi-device** | Primarily Gaggiuino. No documented support for other firmwares. |
| **Integration value** | **Medium.** Useful as a profile marketplace and shot archive for Gaggiuino-compatible profiles. Less value if users are not in the Gaggiuino ecosystem. |
| **Device management gap** | No fleet management, no OTA, no real-time device status, no command channel. |

### 3. ShotProfiles.com

| Aspect | Details |
|---|---|
| **Operator** | Community-curated |
| **Device management?** | **No.** ShotProfiles is a static profile library — a marketplace of downloadable JSON profiles, not a device management platform. |
| **What it does** | Browse, rate, search, and download Gaggiuino/GaggiaMate extraction profiles. AI-driven profile generation. |
| **Auth** | Free/premium tiers; no public API docs for programmatic access. |
| **API** | **Not publicly documented.** Integration would require reverse-engineering or reaching out for API access. |
| **Multi-device** | Gaggiuino / GaggiaMate profiles (JSON format). Compatible with any firmware that reads that JSON schema. |
| **Integration value** | **Medium.** As a profile import source — our cloud or device could import profiles from ShotProfiles. No device management features to integrate with. |
| **Device management gap** | Everything — it's a content library, not a device platform. |

### 4. Gaggiuino REST API / MCP Server

| Aspect | Details |
|---|---|
| **Operator** | Community (multiple contributors) |
| **Source** | [github.com/AndrewKlement/gaggiuino-mcp](https://github.com/AndrewKlement/gaggiuino-mcp), [pypi.org/project/gaggiuino_api](https://pypi.org/project/gaggiuino_api/) |
| **Device management?** | **Yes — local.** The Gaggiuino firmware exposes a local REST API for real-time device status, profile management, and shot retrieval. The MCP server wraps this for AI/automation clients. |
| **What it does** | `GET /status` (live machine state), `GET /profiles` (list), `POST /profiles/select` (apply profile), `GET /shot/{id}` (telemetry), `GET /shot/latest`. |
| **Auth** | Local network only — no auth required (LAN trust model). |
| **API** | RESTful (Python `gaggiuino_api` wrapper) + MCP (Model Context Protocol for AI assistants). |
| **Multi-device** | Gaggiuino firmware only. Not designed for multi-machine fleet management. |
| **Integration value** | **High for protocol reference.** The API design and endpoint patterns are well-thought-out and could inform our own local API. |
| **Device management gap** | Local-only (no cloud), no fleet management, no OTA, no multi-user auth. |

---

## Additional Platforms Discovered

During research we identified several additional platforms not covered in the
original `cloud_ideas.md` survey that have relevant device management features:

### 5. BrewOS

| Aspect | Details |
|---|---|
| **Operator** | BrewOS project (open-source, Apache 2.0 + Commons Clause) |
| **Source** | [github.com/brewos-io](https://github.com/brewos-io/) — firmware + web app + cloud relay + HA integration |
| **Device management?** | **Yes — the most complete.** BrewOS Cloud provides remote access, device pairing, user accounts, admin dashboard, push notifications, and OTA updates. |
| **What it does** | Full espresso machine firmware (PID, profiling, safety) + cloud dashboard + MQTT + REST API + Home Assistant integration + webhooks. |
| **Auth** | Google OAuth; device pairing via cloud dashboard. |
| **API** | REST (HTTP) for machine control + MQTT with HA auto-discovery + webhooks for events. |
| **Multi-device** | Designed for multi-machine (dual boiler, single boiler, HX). Fleet-aware cloud. |
| **Integration value** | **Very high.** BrewOS is the closest to what we're building and already has a working cloud with device management. |
| **Considerations** | Different firmware (not ESPHome); we could integrate at the cloud API level but not at the firmware level. Apache 2.0 + Commons Clause license may restrict commercial reuse of their cloud code. |

### 6. Decent Espresso (DE1 App Ecosystem)

| Aspect | Details |
|---|---|
| **Operator** | Decent Espresso (commercial) |
| **Device management?** | **Yes — partial.** DE1app v1.43+ links machines to user accounts (serial number registration). Cloud features include profile sync, shot upload to Visualizer, in-app tech support, configuration backup. |
| **What it does** | Machine registration, firmware version tracking, profile management, shot logging, Bluetooth + WiFi control. |
| **Auth** | Decent account (email/password); device links to account via serial number. |
| **API** | No public cloud API. Local control via Bluetooth (proprietary) or community MQTT plugin. pyDE1 library provides programmatic control. |
| **Multi-device** | Decent machines only (proprietary hardware). |
| **Integration value** | **Low for direct integration** (proprietary, closed ecosystem). **High as design reference** — their cloud features (registration, backup, sync, support) define the user experience gold standard. |

### 7. Meticulous Espresso

| Aspect | Details |
|---|---|
| **Operator** | Meticulous Home Inc. (commercial) |
| **Device management?** | **Yes — full cloud.** Open backend API for device control, settings management, firmware updates, profile CRUD, shot history, real-time sensor streaming. |
| **What it does** | Machine control (start/stop/purge), device settings, profile management, shot analytics, real-time telemetry, HA integration (MQTT auto-discovery). |
| **Auth** | API key / token-based. |
| **API** | OpenAPI documented REST API — [github.com/ohheyitsdave/Meticulous-OpenAPI-Documentation](https://github.com/ohheyitsdave/Meticulous-OpenAPI-Documentation). |
| **Multi-device** | Meticulous machines only, but API pattern is hardware-agnostic. |
| **Integration value** | **High as API design reference.** The open API documentation is excellent and could serve as a template for our cloud API. Shot data could potentially be pushed to Meticulous-compatible dashboards. |

### 8. Beanconqueror

| Aspect | Details |
|---|---|
| **Operator** | graphefruit (open-source) |
| **Source** | [github.com/graphefruit/Beanconqueror](https://github.com/graphefruit/Beanconqueror) — Angular + Ionic + Capacitor |
| **Device management?** | **No cloud management.** Beanconqueror is a local-first mobile app with BLE device integration (scales, pressure sensors). No cloud backend, no device fleet management. |
| **What it does** | Coffee inventory, brew logging, BLE scale/sensor integration, export/import via files. |
| **Integration value** | **Medium.** We could export shots in Beanconqueror-compatible format for users who prefer that app for bean/recipe management. No cloud API to integrate with. |

---

## Device Management Feature Comparison Matrix

| Feature | Visualizer | Sprofiler | ShotProfiles | Gaggiuino API | BrewOS | Decent DE1 | Meticulous | Beanconqueror |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Device registration** | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ❌ |
| **OTA firmware updates** | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ❌ |
| **Remote machine control** | ❌ | ❌ | ❌ | ✅ (local) | ✅ | ✅ (local) | ✅ | ❌ |
| **Real-time telemetry** | ❌ | ❌ | ❌ | ✅ (local) | ✅ | ✅ (local) | ✅ | ❌ |
| **Profile management** | ✅ (view) | ✅ | ✅ (browse) | ✅ | ✅ | ✅ | ✅ | ❌ |
| **Profile push to device** | ❌ | ✅ (Gaggiuino) | ❌ | ✅ (local) | ✅ | ✅ | ✅ | ❌ |
| **Shot logging** | ✅ | ✅ | ❌ | ✅ (local) | ✅ | ✅ | ✅ | ✅ (local) |
| **Shot visualisation** | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ (local) |
| **Community sharing** | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | ❌ | ❌ |
| **Multi-firmware support** | ✅ | ❌ | ✅ (Gaggiuino/GM) | ❌ | ❌ | ❌ | ❌ | ✅ (BLE devices) |
| **Open API** | ✅ | ⚠️ | ❌ | ✅ | ✅ | ❌ | ✅ | ❌ |
| **Self-hostable** | ✅ | ⚠️ | ❌ | N/A (local) | ✅ | ❌ | ❌ | N/A (local app) |
| **Fleet management** | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ✅ | ❌ |

Legend: ✅ = Yes, ❌ = No, ⚠️ = Partial/unclear

---

## Integration Strategy Options

### Option 1: Build Our Own Cloud, Integrate Nothing

Build the standalone cloud described in `cloud_ideas.md`. No integration with
existing platforms.

- **Pros:** Full control; no third-party dependencies; custom-fit.
- **Cons:** No community features (shot sharing, profile library) from day one;
  users must choose between our cloud and Visualizer/Sprofiler; more work.
- **Verdict:** Too isolated. Users expect to participate in the existing community.

### Option 2: Use BrewOS Cloud Directly

BrewOS has the most complete cloud and is open-source. We could adapt our
firmware to speak its API.

- **Pros:** Full device management, OTA, cloud dashboard out of the box.
- **Cons:** BrewOS is a different firmware — not ESPHome. Tight coupling to
  someone else's cloud. Commons Clause license may restrict our use. Would
  require rewriting our firmware interfaces to match BrewOS's expectations.
- **Verdict:** Not a good fit as the primary cloud, but we should study their
  API design and consider federation.

### Option 3: Integrate with Visualizer.coffee as Primary Shot Backend

Use Visualizer's API for shot storage and community features. Build our own
thin layer for device management and profile sync.

- **Pros:** Instant access to the largest espresso shot community. Proven
  infrastructure. Open API and open source.
- **Cons:** No device management features from Visualizer (we still need to
  build those). Visualizer is a one-man project — availability risk.
- **Verdict:** Excellent for shot data; insufficient for device management.

### Option 4: Multi-Backend Adapter Pattern (Recommended)

Build our own cloud for device management and profile sync (as per
`cloud_ideas.md`), but implement **pluggable adapters** that forward data to
and import from multiple existing platforms:

```
                        ┌──────────────────┐
                        │  Our Cloud       │
                        │  (MQTT + REST +  │
                        │   PostgreSQL)    │
                        │                  │
                        │  Device mgmt ✅  │
                        │  Profile sync ✅ │
                        │  Shot logging ✅ │
                        │  Recipes ✅      │
                        └───────┬──────────┘
                                │
                    ┌───────────┼───────────┐
                    │           │           │
              ┌─────▼────┐ ┌───▼──────┐ ┌──▼──────────┐
              │Visualizer│ │Sprofiler │ │ShotProfiles │
              │ Adapter  │ │ Adapter  │ │  Adapter    │
              └─────┬────┘ └───┬──────┘ └──┬──────────┘
                    │          │           │
              ┌─────▼────┐ ┌───▼──────┐ ┌──▼──────────┐
              │Visualizer│ │Sprofiler │ │ShotProfiles │
              │ .coffee  │ │  Cloud   │ │   .com      │
              │  (shots) │ │ (shots + │ │ (profiles)  │
              │          │ │ profiles)│ │             │
              └──────────┘ └──────────┘ └─────────────┘
```

- **Pros:** Users get our device management + community features from all major
  platforms. No vendor lock-in. Each adapter is optional (user configures which
  services to connect). We control the core; adapters are thin translation layers.
- **Cons:** More initial development. Must maintain adapters as external APIs evolve.
- **Verdict:** **Recommended.** This is the most user-friendly and future-proof approach.

---

## Recommended Strategy: Multi-Backend Adapter Pattern

### Core Principle

Our cloud is the **primary control plane** for:
- Device registration and management
- Profile and recipe storage (source of truth)
- Real-time telemetry via MQTT
- Command channel (remote brew/steam/grind)

External platforms are **secondary data sinks and sources**:
- **Visualizer.coffee** — shot export (push completed shots to Visualizer)
- **Sprofiler** — bidirectional profile sync (import community profiles, push our shots)
- **ShotProfiles.com** — profile import (download community profiles into our library)
- **BrewOS Cloud** — future federation (if they publish an API for cross-platform data exchange)
- **Meticulous** — API design reference (their OpenAPI spec informs our REST API design)

### Adapter Architecture

Each adapter implements a common interface:

```
interface CloudAdapter {
  // Shot data
  push_shot(shot: Shot) → Result
  pull_shots(since: DateTime) → Shot[]

  // Profiles
  push_profile(profile: Profile) → Result
  pull_profiles(query: string) → Profile[]

  // Status (optional — not all platforms support this)
  push_status(status: MachineStatus) → Result
}
```

Adapters are configured per-user in the cloud backend:

```json
{
  "user_id": "uuid",
  "integrations": [
    {
      "platform": "visualizer",
      "enabled": true,
      "credentials": { "token": "..." },
      "auto_push_shots": true,
      "auto_pull_profiles": false
    },
    {
      "platform": "sprofiler",
      "enabled": true,
      "credentials": { "token": "..." },
      "auto_push_shots": true,
      "auto_pull_profiles": true
    },
    {
      "platform": "shotprofiles",
      "enabled": true,
      "credentials": null,
      "auto_push_shots": false,
      "auto_pull_profiles": true
    }
  ]
}
```

---

## Integration Architecture

### Full System View

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         ESP32 (ESPHome)                                 │
│                                                                         │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────────────┐ │
│  │ Native API  │  │ MQTT Client  │  │  espresso_machine (C++)        │ │
│  │ (→ HA)      │  │ (→ Cloud)    │  │  brew/steam + local profiles   │ │
│  └──────┬──────┘  └──────┬───────┘  └────────────────────────────────┘ │
└─────────┼────────────────┼─────────────────────────────────────────────┘
          │                │
    ┌─────▼─────┐   ┌─────▼──────────────────────────────────────────┐
    │   Home    │   │              Our Cloud Backend                  │
    │ Assistant │   │                                                 │
    │  (local)  │   │  ┌───────────────────────────────────────────┐ │
    └───────────┘   │  │           Core Services                   │ │
                    │  │                                           │ │
                    │  │  • Device Registry (register, pair, OTA)  │ │
                    │  │  • Profile Store (CRUD + version control) │ │
                    │  │  • Recipe Store (CRUD + sync)             │ │
                    │  │  • Shot Archive (time-series + metadata)  │ │
                    │  │  • MQTT Ingestion (telemetry, commands)   │ │
                    │  │  • REST API (dashboard, mobile app)       │ │
                    │  └──────────────────┬────────────────────────┘ │
                    │                     │                           │
                    │  ┌──────────────────▼────────────────────────┐ │
                    │  │         Adapter Layer                     │ │
                    │  │                                           │ │
                    │  │  ┌────────────┐ ┌──────────┐ ┌─────────┐│ │
                    │  │  │ Visualizer │ │Sprofiler │ │ShotProf.││ │
                    │  │  │  Adapter   │ │ Adapter  │ │ Adapter ││ │
                    │  │  └─────┬──────┘ └────┬─────┘ └────┬────┘│ │
                    │  └────────┼─────────────┼────────────┼──────┘ │
                    └───────────┼─────────────┼────────────┼────────┘
                                │             │            │
                    ┌───────────▼──┐ ┌────────▼───┐ ┌─────▼──────────┐
                    │ Visualizer   │ │  Sprofiler │ │ ShotProfiles   │
                    │ .coffee      │ │            │ │ .com           │
                    │              │ │            │ │                │
                    │ Shot archive │ │ Shots +    │ │ Profile        │
                    │ Community    │ │ Profiles   │ │ library        │
                    │ Comparison   │ │ Community  │ │ AI generation  │
                    └──────────────┘ └────────────┘ └────────────────┘
```

### Data Flow for Key Operations

#### Shot Completion → Multi-Platform Push

```
1. ESP32 completes brew
2. ESP32 publishes full shot JSON to MQTT: espresso/{id}/shot/complete
3. Our Cloud Backend receives shot via MQTT subscriber
4. Backend stores shot in PostgreSQL
5. Backend checks user's integration config:
   a. Visualizer enabled + auto_push? → Visualizer Adapter transforms to
      Visualizer JSON format, POSTs to /api/shots/upload with user's OAuth token
   b. Sprofiler enabled + auto_push? → Sprofiler Adapter transforms and pushes
6. User sees shot in our dashboard AND in Visualizer AND in Sprofiler
```

#### Profile Import from Community Library

```
1. User browses ShotProfiles.com catalog in our web dashboard
2. Dashboard calls our REST API: POST /api/profiles/import
   { "source": "shotprofiles", "profile_id": "abc123" }
3. Backend's ShotProfiles Adapter fetches the profile JSON
4. Backend normalises the JSON to our internal schema
5. Backend stores profile in PostgreSQL
6. Backend publishes profile to MQTT: espresso/{device_id}/profile/sync/request
7. ESP32 receives profile, stores in LittleFS, acknowledges
8. Profile is now available on the machine — sourced from ShotProfiles, stored
   in our cloud, synced to device, all without manual file transfer
```

---

## Per-Platform Integration Details

### Visualizer.coffee Integration

**Direction:** Our Cloud → Visualizer (push shots) + Visualizer → Our Cloud (import profiles)

**Shot Push Implementation:**
```
1. On shot completion, transform our shot JSON to Visualizer's TCL/JSON format.
2. POST to https://visualizer.coffee/api/shots/upload
   Headers: Authorization: Bearer {user_oauth_token}
   Body: Shot file (JSON)
3. Store the returned Visualizer shot URL in our shot record for cross-linking.
```

**Profile Import Implementation:**
```
1. User pastes a Visualizer shot URL in our dashboard.
2. Backend calls GET https://visualizer.coffee/api/shots/{id}/profile
3. Transform Visualizer profile format to our internal schema.
4. Store and sync to device.
```

**Auth Requirements:**
- User must link their Visualizer.coffee account via OAuth 2.0 in our dashboard.
- We request the `upload` scope (minimum privilege for shot push).
- Tokens are stored encrypted in our database.

### Sprofiler Integration

**Direction:** Bidirectional (push shots, pull/push profiles)

**Implementation:**
- Sprofiler's API is less publicly documented than Visualizer's.
- Integration would likely require coordination with the Sprofiler team.
- Profile format is Gaggiuino JSON — already compatible with our planned
  profile schema (see `docs/profiles.md`).

**Approach:**
1. Start with manual profile import (user uploads a downloaded Sprofiler JSON).
2. Request API access from Sprofiler maintainers for automated sync.
3. Implement bidirectional adapter once API is available.

### ShotProfiles.com Integration

**Direction:** ShotProfiles → Our Cloud (pull profiles only)

**Implementation:**
- ShotProfiles does not have a documented public API.
- Two approaches:
  1. **Manual import:** User downloads a profile JSON from ShotProfiles.com and
     uploads it to our cloud dashboard. We parse the Gaggiuino/GaggiaMate JSON
     format (already supported by our profile schema).
  2. **API partnership:** Reach out to ShotProfiles.com for API access to enable
     in-app browsing and one-click import.

### BrewOS Federation (Future)

**Direction:** Bidirectional data exchange (federation)

**Implementation:**
- BrewOS has a cloud API (REST + MQTT) but targets its own firmware.
- Federation would mean: a user with both a BrewOS machine and our machine sees
  unified shot history and profiles across both.
- This requires a shared data schema and mutual API access — realistically a
  future phase requiring BrewOS team cooperation.

---

## What Each Platform Would Give Our Users

| Integration | User Benefit |
|---|---|
| **Visualizer.coffee** | Auto-upload every shot to the largest espresso community. Compare shots with Decent, Gaggiuino, Meticulous users. Share shot graphs via URL. |
| **Sprofiler** | Access Gaggiuino community profiles. Share our profiles with Gaggiuino users. Cross-firmware community participation. |
| **ShotProfiles.com** | Browse and import curated extraction profiles (including AI-generated ones). Try proven recipes from the community without manual JSON editing. |
| **BrewOS** (future) | Unified dashboard for multi-firmware households. Shot history across different machines. |
| **Beanconqueror** | Export shots in Beanconqueror-compatible format for users who track beans and brew methods in that app. |

### Combined Value Proposition

A user of our ESPHome espresso controller would get:

1. **Our cloud** handles device management, profile/recipe sync, real-time MQTT.
2. **Visualizer** handles community shot sharing and analytics.
3. **ShotProfiles** provides a library of community-curated profiles.
4. **Sprofiler** enables cross-firmware community participation.

This combination gives our users **feature parity with Gaggiuino and BrewOS
users** without building a full community platform from scratch.

---

## Risks & Considerations

### Third-Party API Stability

- **Visualizer.coffee** is maintained by a single developer. If the service
  goes down or the API changes, our adapter breaks. Mitigation: our cloud
  stores all shots locally — Visualizer is a copy, not the source of truth.
- **Sprofiler and ShotProfiles** have sparse API documentation. We may need
  to reverse-engineer or negotiate access. Mitigation: start with manual
  import/export; automate later.

### Authentication Token Management

- Storing user OAuth tokens for third-party services requires careful
  encryption and lifecycle management (token refresh, revocation on
  account deletion).
- Must comply with OAuth 2.0 best practices and each platform's ToS.

### Profile Format Normalisation

- Different platforms use slightly different JSON schemas for profiles.
- Our adapter layer must handle format differences gracefully:
  - Missing fields → sensible defaults.
  - Extra fields → preserve in metadata (don't lose data).
  - Unit differences → explicit conversion (bar vs psi, °C vs °F).

### Rate Limiting

- Visualizer.coffee and other platforms may rate-limit API calls.
- Our adapters must implement exponential backoff and queuing.
- For shot push: queue and retry in the background; don't block the user.

### License Considerations

- **Visualizer** — MIT license (open source, no restrictions).
- **BrewOS** — Apache 2.0 + Commons Clause (personal use OK; commercial
  hosting restricted). We can study their code and API design but should
  not directly host their cloud service commercially.
- **Sprofiler** — License unclear; verify before deep integration.
- **ShotProfiles** — Proprietary; API access may require partnership agreement.

---

## Open Questions

1. **Should we contribute to Visualizer.coffee directly?** Since it's open
   source, we could submit PRs to improve its API or add endpoints we need
   (e.g., bulk shot upload, profile push). This benefits the whole community.

2. **Should we adopt Visualizer's shot JSON format as our canonical format?**
   If we use the same format internally, the Visualizer adapter becomes trivial
   (just forward the JSON). Other adapters would transform from Visualizer
   format to their platform's format.

3. **Should we offer a "Visualizer-only" mode?** For users who don't want our
   cloud at all — they just want their ESPHome machine to push shots directly
   to Visualizer.coffee. This could be a firmware-only feature (ESP32 POSTs to
   Visualizer's API directly, no intermediary cloud).

4. **Should we create a common "Open Espresso Telemetry" schema?** An open JSON
   schema and MQTT topic standard that any espresso firmware can adopt. If
   Gaggiuino, BrewOS, and our project agree on a common schema, all platforms
   become interoperable automatically. This is ambitious but high-impact.

5. **How do we handle profile ownership in a multi-source world?** If a user
   imports a profile from ShotProfiles, edits it in our cloud, and pushes it
   to Sprofiler — who "owns" that profile? We need clear versioning and
   attribution rules.

6. **Should adapters run on the ESP32 or in the cloud backend?** Running on
   ESP32 (e.g., direct HTTP POST to Visualizer) avoids needing our cloud
   backend, but uses more ESP32 RAM and requires TLS. Running in the cloud
   backend is more efficient but requires our cloud to be available.
   **Recommendation:** Cloud backend for full integration; ESP32-direct for
   "Visualizer-only" lightweight mode.

7. **Can we partner with BrewOS for a shared cloud?** BrewOS Cloud is the
   most complete existing solution. If we could federate with them (share
   device data, profiles), both projects benefit. Worth exploring.

8. **What about Meticulous's OpenAPI spec?** Meticulous has a well-documented
   REST API (OpenAPI). While their machines are proprietary, their API design
   patterns (device control, profile CRUD, shot history) are an excellent
   reference for our own REST API design.

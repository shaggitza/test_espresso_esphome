# Sprofiler Cloud — Shot Upload

> **Status: ✅ Implemented** (data collection, JSON serialisation, configurable upload).
> See `FEATURES.md` for the authoritative feature-status table.

---

## Overview

The `espresso_machine_sprofiler` component uploads completed shot data to the
[Sprofiler](https://sprofiler.io/) cloud platform in the **Gaggiuino JSON
format** — the de facto standard across the Gaggiuino espresso community.

After each brew the component:

1. Serialises the recorded telemetry (time, pressure, temperature, flow,
   weight) into a Gaggiuino-compatible JSON document.
2. POSTs it to the configured Sprofiler server using Bearer token
   authentication.
3. Retries automatically on the next `loop()` tick if the upload fails.

### Architecture

```
┌──────────────────────┐       HTTPS POST       ┌───────────────────┐
│    Our ESP32          │  ───────────────────►  │  sprofiler.io     │
│                       │  /api/shots/upload     │  (or custom host) │
│  espresso_machine_    │                        │                   │
│  sprofiler component  │  Bearer <api_token>    │  Stores shot      │
│                       │  Content-Type: json    │  for analytics    │
└──────────────────────┘                        └───────────────────┘
```

---

## YAML Configuration

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine_sprofiler

espresso_machine_sprofiler:
  id: sprofiler_upload
  server: "https://sprofiler.io"          # optional — default: https://sprofiler.io
  api_token: !secret sprofiler_token      # required — Bearer token from your account
  profile_name: "Manual"                  # optional — default: "Manual"
```

### Configuration Keys

| Key | Type | Required | Default | Description |
|-----|------|----------|---------|-------------|
| `id` | ID | yes | — | ESPHome entity ID |
| `server` | URL string | no | `https://sprofiler.io` | Sprofiler server base URL. Set this to point to a self-hosted instance or the Sprofiler dev server (`https://dev.sprofiler.io`). |
| `api_token` | string | **yes** | — | Bearer token obtained from your Sprofiler account settings. Store it in `secrets.yaml`. |
| `profile_name` | string | no | `"Manual"` | Human-readable profile name included in every uploaded shot record. |

### Secrets

Add the following to your `secrets.yaml`:

```yaml
sprofiler_token: "paste-your-api-token-here"
```

---

## How It Works

### Shot Recording

The component exposes three C++ methods that should be called during a brew:

| Method | When to call | Purpose |
|--------|-------------|---------|
| `begin_shot()` | At brew start | Clears previous data, increments shot ID, starts recording |
| `add_datapoint(time, pressure, temp, flow, weight)` | Every ~100 ms during brew | Records one telemetry sample |
| `end_shot(duration_ms)` | At brew end | Finalises the shot and marks it for upload |

These can be called from C++ (e.g. wired into the orchestrator) or triggered
via ESPHome lambda automations.

### Upload Flow

```
1. User pulls a shot
2. Orchestrator / automation calls begin_shot()
3. Telemetry samples collected at ~10 Hz via add_datapoint()
4. Shot ends → end_shot(duration_ms)
5. Next loop() tick → serialize_shot_json() → HTTP POST to server
6. HTTP 2xx → shot cleared; HTTP error → retry next tick
```

### Gaggiuino JSON Format

The uploaded JSON follows the Gaggiuino shot data specification:

```json
{
  "id": 1,
  "timestamp": 1713340562,
  "duration": 26500,
  "profile": {
    "name": "Manual"
  },
  "datapoints": [
    {
      "time": 0.00,
      "pressure": 0.10,
      "temperature": 92.50,
      "flow": 7.00,
      "weight": 0.00
    },
    {
      "time": 0.10,
      "pressure": 0.20,
      "temperature": 92.70,
      "flow": 6.80,
      "weight": 0.50
    }
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | number | Device-local shot sequence number |
| `timestamp` | number | Unix timestamp of shot start |
| `duration` | number | Total shot duration in milliseconds |
| `profile.name` | string | Profile name from config |
| `datapoints[].time` | float | Seconds since shot start |
| `datapoints[].pressure` | float | Brew pressure in bar |
| `datapoints[].temperature` | float | Water temperature in °C |
| `datapoints[].flow` | float | Water flow rate in ml/s |
| `datapoints[].weight` | float | Cup weight in grams (0 if no scale) |

---

## Testing

The component is fully tested with **33 GoogleTest unit tests**
(`tests/cpp/test_sprofiler.cpp`). All HTTP transport is mocked — **no real
network calls are ever made during testing**.

### Test Coverage

| Suite | Tests | What is verified |
|-------|-------|-----------------|
| `SprofilerConfig` | 5 | Default values, custom server/token/profile |
| `SprofilerRecording` | 8 | Begin/end shot, datapoint accumulation, state transitions |
| `SprofilerJson` | 5 | JSON structure, datapoint formatting, quote escaping |
| `SprofilerUpload` | 10 | Success/failure codes, URL construction, auth header, retry |
| `SprofilerLoop` | 2 | Automatic upload in loop(), no-op when idle |
| `SprofilerEdge` | 3 | Large shots (300 datapoints), empty token, trailing slash |

### Running Tests

```bash
# Build and run all tests (including sprofiler)
cmake -B build/cpp -S tests/cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/cpp -j$(nproc)
./build/cpp/all_tests --gtest_filter="Sprofiler*"
```

### Mock Strategy

The `SprofilerShotUpload` class has a `virtual int http_post(...)` method.
Tests override it in `MockSprofilerUpload` to:

- Return configurable HTTP status codes (200, 201, 400, 401, 500, -1).
- Capture the URL, auth header, and body for assertion.
- Count the number of POST attempts.

This ensures the real Sprofiler API is **never contacted** during testing.

---

## Memory Footprint

| Resource | Estimate |
|----------|----------|
| Shot JSON (25 s @ 10 Hz) | ~15 KB (transient, freed after upload) |
| Shot JSON (60 s @ 10 Hz) | ~36 KB (transient) |
| Persistent overhead | ~5 KB (config strings, state) |

---

## Configuring a Custom Server

Point to any Gaggiuino-compatible API server:

```yaml
espresso_machine_sprofiler:
  server: "https://dev.sprofiler.io"   # Sprofiler dev instance
  api_token: !secret sprofiler_token
```

Or a local/self-hosted server:

```yaml
espresso_machine_sprofiler:
  server: "http://192.168.1.100:8080"  # local dev server
  api_token: "local-dev-token"
```

---

## Limitations

- **Upload only** — this component does not download or sync profiles from
  Sprofiler. Profile sync is planned for a future phase.
- **No retry queue to flash** — failed uploads are retried in RAM on each
  `loop()` tick. If the device reboots before a successful upload the shot is
  lost. Flash-backed retry queue is planned.
- **Undocumented API** — the Sprofiler REST API is not publicly documented.
  The endpoint path (`/api/shots/upload`) and JSON format are inferred from
  Gaggiuino firmware behaviour. See `docs/sprofiler_cloud.md` for the full
  API analysis.

---

## Related Documentation

- `docs/sprofiler_cloud.md` — Full research and architecture plan
- `FEATURES.md` — Feature status table
- `examples/philips_barista_brew.yaml` — Reference config with Sprofiler
- `examples/philips_barista_brew_mock.yaml` — Mock config with Sprofiler

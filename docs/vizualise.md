# Vizualise.coffee — Shot Upload

> **Status: 🚧 BETA** — data collection and upload implemented; not fully verified
> end-to-end.  Opt-in only.  See `FEATURES.md` for the authoritative status table.

---

## Overview

The `espresso_machine_vizualise` component uploads completed shot data to
[visualizer.coffee](https://visualizer.coffee/) — a community platform for
espresso shot analysis and sharing, compatible with Decent Espresso, Gaggiuino,
and other machines.

After each brew the component:

1. Serialises the recorded telemetry (time, pressure, temperature, flow,
   weight) into the **visualizer.coffee parallel-arrays JSON format** — the
   format understood by the Visualizer API.
2. POSTs it to the configured server using Bearer token authentication.
3. Retries automatically with exponential backoff on transient failures.

### Architecture

```
┌──────────────────────┐       HTTPS POST          ┌─────────────────────┐
│    Our ESP32          │  ──────────────────────►  │  visualizer.coffee  │
│                       │  /api/shots/upload        │  (or custom host)   │
│  espresso_machine_    │                           │                     │
│  vizualise component  │  Bearer <api_token>       │  Stores shot for    │
│                       │  Content-Type: json       │  analysis + sharing │
└──────────────────────┘                           └─────────────────────┘
```

---

## YAML Configuration

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine
      - espresso_machine_vizualise    # opt-in

espresso_machine:
  id: my_espresso
  # ... brew/steam config ...

espresso_machine_vizualise:
  id: vizualise_upload
  espresso_machine: my_espresso            # auto-records shots on brew end
  api_token: !secret visualizer_token      # required — Bearer token
  profile_name: "Manual"                   # optional — default: "Manual"
  machine_name: "ESPHome Espresso Machine" # optional — shown on visualizer.coffee
  server: "https://visualizer.coffee"      # optional — default: https://visualizer.coffee
```

### Configuration Keys

| Key | Type | Required | Default | Description |
|-----|------|----------|---------|-------------|
| `id` | ID | yes | — | ESPHome entity ID |
| `api_token` | string | **yes** | — | Bearer token from your visualizer.coffee account settings. Store it in `secrets.yaml`. |
| `espresso_machine` | ID reference | no | — | Reference to the `espresso_machine` orchestrator. When set, the component automatically records shot telemetry during brew and uploads on brew end. |
| `profile_name` | string | no | `"Manual"` | Human-readable profile name included in every uploaded shot record. |
| `machine_name` | string | no | `"ESPHome Espresso Machine"` | Machine identifier shown on visualizer.coffee. |
| `server` | URL string | no | `https://visualizer.coffee` | Visualizer server base URL. Override for self-hosted instances. Trailing slashes are normalised automatically. |
| `http_request_id` | ID reference | no | — | Reference to an `http_request:` component for the HTTPS transport. Required for real uploads; omit in YAML-only tests. |

### Secrets

Add the following to your `secrets.yaml`:

```yaml
visualizer_token: "paste-your-api-token-here"
```

Obtain a token from your visualizer.coffee account settings page.

---

## How It Works

### Auto-Recording (Recommended)

When wired to the `espresso_machine` orchestrator via the `espresso_machine:`
key, the component automatically:

1. **Detects brew start** — watches the orchestrator's mode for the
   `BREWING` transition (rising edge).
2. **Samples telemetry at ~10 Hz** — reads temperature and flow rate from the
   orchestrator's sensors every 100 ms.
3. **Detects brew end** — watches for the mode to leave `BREWING`
   (falling edge), then finalises the shot record.
4. **Uploads immediately** — serialises the shot and POSTs it on the next
   `loop()` tick.

No YAML automations or lambdas are needed — just wire the orchestrator ID.

### Manual Recording (Advanced)

The component also exposes three C++ methods for manual control:

| Method | When to call | Purpose |
|--------|-------------|---------|
| `begin_shot()` | At brew start | Clears previous data, increments shot ID, starts recording |
| `add_datapoint(time_ms, pressure, temp, flow, weight)` | Every ~100 ms during brew | Records one telemetry sample |
| `end_shot(duration_ms)` | At brew end | Finalises the shot and marks it for upload |

### Upload Flow

```
1. User pulls a shot
2. Orchestrator / automation calls begin_shot()
3. Telemetry sampled at ~10 Hz via add_datapoint()
4. Shot ends → end_shot(duration_ms)
5. Next loop() tick → serialize_shot_json() → HTTP POST to server
6. HTTP 2xx → shot cleared; HTTP error → retry with exponential backoff
```

### visualizer.coffee JSON Format

The uploaded JSON follows the **visualizer.coffee parallel-arrays format**:

```json
{
  "start_time": 1713340562,
  "machine": "ESPHome Espresso Machine",
  "profile": {
    "name": "Manual"
  },
  "sample_interval": 100,
  "data": {
    "time":        [0.00, 100.00, 200.00],
    "pressure":    [0.10, 0.20,   0.50],
    "temperature": [92.50, 92.70, 92.90],
    "flow":        [7.00, 6.80,   5.20],
    "weight":      [0.00, 0.50,   1.20]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `start_time` | number | Unix timestamp of shot start (seconds) |
| `machine` | string | Machine name from config |
| `profile.name` | string | Profile name from config |
| `sample_interval` | number | Milliseconds between samples (100 = ~10 Hz) |
| `data.time` | float array | Milliseconds since shot start |
| `data.pressure` | float array | Brew pressure in bar (0 if no sensor) |
| `data.temperature` | float array | Water temperature in °C |
| `data.flow` | float array | Water flow rate in ml/s |
| `data.weight` | float array | Cup weight in grams (0 if no scale) |

This format is compatible with the [Visualizer API](https://apidocs.visualizer.coffee/)
and consistent with uploads from Decent Espresso and Gaggiuino devices.

---

## Testing

The component has **47 GoogleTest unit tests** (`tests/cpp/test_vizualise.cpp`).
All HTTP transport is mocked — **no real network calls are ever made during
testing**.

### Test Coverage

| Suite | Tests | What is verified |
|-------|-------|-----------------|
| `VizualiseConfig` | 9 | Default values, custom server/token/profile/machine, trailing-slash normalisation |
| `VizualiseRecording` | 8 | Begin/end shot, datapoint accumulation, state transitions |
| `VizualiseJson` | 7 | JSON structure, parallel-array format, value encoding, quote escaping |
| `VizualiseUpload` | 11 | Success/failure codes, URL construction, auth header, retry, trailing-slash |
| `VizualiseLoop` | 4 | Automatic upload in loop(), no-op when idle, backoff, abandon after max retries |
| `VizualiseEdge` | 2 | Large shots (300 datapoints), empty token |
| `VizualiseAutoRecord` | 6 | Orchestrator integration: auto-start on brew, telemetry sampling, auto-end on brew stop, upload, steam exclusion, multi-shot sequences |

### Running Tests

```bash
cmake -B build/cpp -S tests/cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/cpp -j$(nproc)
./build/cpp/all_tests --gtest_filter="Vizualise*"
```

---

## Memory Footprint

The parallel-arrays format stores floats separately in five `std::vector<float>`
buffers. Footprint is similar to the Sprofiler component.

| Resource | Estimate |
|----------|----------|
| Shot arrays (25 s @ 10 Hz, 5 arrays) | ~30 KB (transient, freed after upload) |
| Shot arrays (60 s @ 10 Hz, 5 arrays) | ~72 KB (transient) |
| Persistent overhead | ~5 KB (config strings, state) |

---

## Obtaining an API Token

1. Create a free account at [visualizer.coffee](https://visualizer.coffee/).
2. Navigate to **Profile → Settings → API**.
3. Generate or copy your Bearer token.
4. Add it to your `secrets.yaml` as `visualizer_token`.

---

## Configuring a Custom Server

Point to any Visualizer-compatible server:

```yaml
espresso_machine_vizualise:
  server: "http://192.168.1.100:8080"  # local / self-hosted instance
  api_token: "local-dev-token"
```

---

## Limitations

- **Upload only** — this component does not download profiles from
  visualizer.coffee.
- **No retry queue to flash** — failed uploads are retried in RAM. If the
  device reboots before a successful upload, the shot is lost.
- **No pressure sensor** — the `pressure` array is always zeros unless you
  add a pressure transducer and wire it to `add_datapoint()` manually.

---

## Related Documentation

- `FEATURES.md` — Feature status table
- `examples/philips_barista_brew.yaml` — Reference config with Vizualise block
- `examples/philips_barista_brew_mock.yaml` — Mock config with Vizualise block
- `docs/sprofiler.md` — Sprofiler cloud integration (alternative upload target)

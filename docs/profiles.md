# Brew Profiles — Planned Feature

> **Status: Planned / Researched — no code written yet.**
> This document captures the design intent, API sketch, and compatibility research
> for brew profiles.  Implementation is tracked in PLAN.md (Phase 12).

---

## What Are Brew Profiles?

A *brew profile* is a named, reusable recipe that describes exactly how an espresso
shot should be pulled.  Instead of hard-coding `target_temperature`, `flow_max`, and
`pre_infusion` settings directly in the `espresso_machine:` YAML block, a profile
bundles all of those parameters — including a multi-phase pressure/flow curve — into
a single named object that can be selected at runtime.

The key capability is:

```
brew.start {profile_name}
```

This single action swaps in a complete set of brewing parameters and starts the shot.
The machine operator (or an HA automation) picks a profile by name; no YAML editing
is required after initial setup.

---

## Why Profiles Are First-Class Citizens

Following the same design philosophy as valves, the pump, and the grinder, brew
profiles will be declared as **top-level ESPHome entities** under an
`espresso_machine_profile:` platform.  This means:

- Each profile is visible in Home Assistant as a selectable preset.
- Profiles can be managed (activated, duplicated) via HA scripts and automations.
- The `espresso_machine:` orchestrator references profiles by `id:`, not by inlining
  their parameters — keeping the orchestrator block clean.
- Profiles can be loaded from external files (JSON/YAML) without touching the device
  firmware YAML.

---

## Planned YAML API

### Declaring profiles

```yaml
# profiles are top-level ESPHome entities (first-class citizens)
espresso_machine_profile:

  - id: profile_classic_9bar
    name: "Classic 9-Bar"
    description: "Traditional Italian-style straight extraction at 9 bar."
    temperature: 93°C
    phases:
      - name: "Pre-infusion"
        type: pressure
        target: 2bar
        duration: 7s
        ramp: linear
      - name: "Extraction"
        type: pressure
        target: 9bar
        exit:
          volume_ml: 36ml    # stop at 36 ml (flow meter)
          # weight_g: 36g    # alternative: stop at 36 g (scale, future)

  - id: profile_bloom
    name: "Bloom / Adaptive"
    description: "Gentle bloom followed by pressure ramp and decline. Works well for light roasts."
    temperature: 90°C
    phases:
      - name: "Bloom"
        type: pressure
        target: 2bar
        duration: 7s
        ramp: linear
      - name: "Ramp"
        type: pressure
        target_start: 2bar
        target_end: 9bar
        duration: 5s
        ramp: ease_in
      - name: "Hold"
        type: pressure
        target: 9bar
        duration: 20s
      - name: "Decline"
        type: pressure
        target_start: 9bar
        target_end: 3bar
        duration: 10s
        ramp: ease_out
        exit:
          volume_ml: 40ml

  - id: profile_flow_control
    name: "Flow Control"
    description: "Constant-flow extraction — requires a dimmer-type pump."
    temperature: 92°C
    phases:
      - name: "Pre-infusion"
        type: flow
        target: 2ml/s
        duration: 6s
      - name: "Extraction"
        type: flow
        target: 3ml/s
        exit:
          volume_ml: 38ml
```

### Referencing profiles in the orchestrator

```yaml
espresso_machine:
  id: my_espresso
  brew:
    heater: main_heater
    pump: main_pump
    flow_meter: brew_flow
    valve: brew_valve
    purge_valve: purge_valve
    # active_profile selects the default profile at boot.
    # The operator (or HA) can switch this at runtime.
    active_profile: profile_classic_9bar
```

### Triggering a shot with a specific profile

From an ESPHome script:

```yaml
script:
  - id: pull_bloom_shot
    then:
      - espresso_machine.brew_start:
          id: my_espresso
          profile: profile_bloom
```

From a Home Assistant automation or script (via the ESPHome native API service call):

```yaml
service: esphome.philips_barista_brew_brew_start
data:
  profile: "Bloom / Adaptive"
```

---

## Multi-Phase Pressure / Flow Curves

Each phase in a profile defines one segment of the extraction curve.  Phases are
executed sequentially.  The active phase ends when **either** its exit condition is
met **or** its duration expires (whichever comes first).

### Phase parameters

| Parameter | Type | Description |
|---|---|---|
| `name` | string | Human-readable label (shown on display and in HA) |
| `type` | `pressure` \| `flow` | What the pump controller targets |
| `target` | pressure (bar) or flow (ml/s) | Setpoint for this phase |
| `target_start` / `target_end` | same as target | For ramped phases |
| `ramp` | `linear` \| `ease_in` \| `ease_out` \| `step` | Ramp curve shape |
| `duration` | time (s) | Maximum time in phase; 0 = unlimited |
| `exit.volume_ml` | volume (ml) | Advance when this volume passes flow meter |
| `exit.weight_g` | mass (g) | Advance when this weight lands in cup (future: scale) |
| `exit.pressure_bar` | pressure (bar) | Advance on pressure threshold (future: pressure sensor) |

### Ramp shapes

```
linear:   target = start + (end - start) * (t / duration)
ease_in:  target = start + (end - start) * (t / duration)^2
ease_out: target = start + (end - start) * (1 - (1 - t/duration)^2)
step:     target = end  (immediate, no ramp)
```

---

## Gaggiuino / GaggiaMate Compatibility

> **Planned — not implemented yet.**

[Gaggiuino](https://github.com/Zer0-bit/gaggiuino) and
[GaggiaMate](https://github.com/jniebuhr/gaggimate) are popular open-source espresso
machine controllers that use a JSON-based profile format.  The long-term goal is that
a user should be able to take a Gaggiuino or GaggiaMate profile file and load it into
this component with minimal (ideally zero) manual editing.

### Gaggiuino / GaggiaMate JSON format (researched)

Both projects share a similar JSON schema.  A representative profile looks like:

```json
{
  "name": "Adaptive Bloom",
  "description": "Gentle bloom, pressure ramp, hold, and decline.",
  "temperature": 93,
  "phases": [
    { "type": "preinfusion", "pressure": 2.0, "duration": 7, "rampStyle": "linear" },
    { "type": "ramp",        "startPressure": 2.0, "endPressure": 9.0, "duration": 5, "rampStyle": "ease-in" },
    { "type": "hold",        "pressure": 9.0, "duration": 20 },
    { "type": "decline",     "startPressure": 9.0, "endPressure": 3.0, "duration": 10, "rampStyle": "ease-out" }
  ],
  "target": { "type": "weight", "value": 36 }
}
```

Key observations from researching the format:

- `temperature` — global °C value; both projects use a single temperature per profile
  (per-phase temperatures are a future extension in those projects too).
- `phases[].type` — phase type strings differ between Gaggiuino versions
  (`"preinfusion"`, `"ramp"`, `"hold"`, `"decline"`, `"fill"`, `"brew"`).
- `phases[].pressure` vs `startPressure`/`endPressure` — constant vs ramped phases.
- `phases[].rampStyle` — `"linear"`, `"ease-in"`, `"ease-out"` (maps directly to our
  `ramp:` field).
- `phases[].duration` — seconds; 0 means unlimited (same convention we adopt).
- `target.type` — `"weight"` (scale) or `"time"` shot termination condition.

### Planned import path

```
Gaggiuino/GaggiaMate .json profile file
          │
          ▼
  esphome_espresso profile converter
  (Python script or HA integration helper)
          │
          ▼
  espresso_machine_profile: YAML block
  (or runtime load via API call)
```

The converter will need to handle:

1. **Phase type normalization** — map `"preinfusion"` → `type: pressure, target: <pressure>`;
   map `"ramp"` → `type: pressure, target_start: …, target_end: …, ramp: ease_in`, etc.
2. **Ramp style translation** — `"ease-in"` → `ease_in`, `"ease-out"` → `ease_out`.
3. **Exit condition mapping** — `"target": {"type": "weight", "value": 36}` → requires a
   scale integration (future); for now maps to `exit.volume_ml` using a configurable
   brew ratio (e.g. 1 g ≈ 1 ml).
4. **Pressure unit** — Gaggiuino profiles use bar; our phases also use bar.  No conversion needed.
5. **Missing fields** — if a field has no equivalent (e.g. advanced valve control),
   it is silently ignored with a warning logged to the ESPHome console.

### Community profile repositories

- [ShotProfiles.com](https://shotprofiles.com/) — community-curated Gaggiuino profiles
- [Gaggiuino GitHub discussions](https://github.com/Zer0-bit/gaggiuino/discussions/350) — profile format proposal thread
- [GaggiaMate docs](https://docs.gaggimate.eu/docs/profiles/) — official profile documentation

---

## Runtime Profile Selection (Home Assistant UI)

When profiles are implemented, a `select` entity will be exposed to HA showing all
configured profile names.  Changing the selection immediately activates that profile
for the next shot.

```
Home Assistant entity: select.philips_barista_brew_active_profile
Options: ["Classic 9-Bar", "Bloom / Adaptive", "Flow Control"]
```

The display (if fitted) will show the active profile name during idle and the current
phase name during extraction.

---

## Shot Statistics per Profile

Each shot will log statistics against the profile that was active, allowing HA
dashboards to track per-profile performance:

| Statistic | HA Entity |
|---|---|
| Shot duration | `sensor.last_shot_duration` |
| Total volume | `sensor.last_shot_volume_ml` |
| Average flow rate | `sensor.last_shot_avg_flow` |
| Profile used | `sensor.last_shot_profile` |
| Peak temperature | `sensor.last_shot_peak_temp` |

These will be persisted across reboots using ESPHome's `globals:` platform.

---

## Implementation Notes (for future reference)

- Profiles that require **pressure control** need a pressure sensor (0–16 bar, I²C or
  analogue) and a dimmer-type pump.  Without hardware pressure sensing, pressure targets
  are approximated via pump power curves (open-loop).
- Profiles that require **weight-based exit** need a scale wired to the ESP32 (HX711
  or NAU7802 load cell amplifier) — this is a separate future platform.
- The `espresso_machine_profile:` platform itself will be a lightweight schema-only
  component in Phase 1 (parsed, stored, selectable via HA) and gain full multi-phase
  execution support in Phase 12.
- Internal representation: each profile is compiled into a C++ struct array at firmware
  build time (zero heap allocation at runtime).

---

## Open Questions

1. Should profiles be stored in firmware flash (compiled in) or in LittleFS/SPIFFS so
   they can be updated over-the-air without a firmware rebuild?
2. How should we handle profiles that reference hardware not present in the current
   machine config (e.g. a pressure-sensor phase on a relay-only pump build)?
3. Should there be a default "simple" profile that mirrors the existing `brew:` block
   behaviour so that users upgrading from the simple config see no change?

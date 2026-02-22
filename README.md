# ESPHome Espresso Machine Controller

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![ESPHome](https://img.shields.io/badge/ESPHome-External%20Component-green)](https://esphome.io/components/external_components/)

A fully-featured ESPHome **external component** that reimplements the low-voltage controller of a
semi-automatic espresso machine — designed specifically around the
**Philips Barista Brew** (integrated grinder, single thermoblock, steam wand, volumetric flow meter)
but written to be generic enough for other machines with similar hardware.

> ⚠️ **USE AT YOUR OWN RISK.** Working with espresso machines involves mains voltage on the high-voltage
> side. This project **only** addresses the low-voltage (3.3 V / 5 V) control side. Always maintain
> proper galvanic isolation between the LV controller board and the 220 V side. If you are unsure,
> do not proceed.

---

## Features

| Feature | Description |
|---|---|
| **PID temperature control** | Thermoblock heater controlled via SSR with a software PID loop |
| **Temperature profiling** | Brew temperature offset + decay curve (temperature surfing) |
| **Volumetric shot control** | Flow meter integration for accurate ml-based shot measurement |
| **Grinder integration** | Relay-driven grinder with configurable grind time |
| **Steam mode** | Separate steam temperature target with pump duty-cycle flow control |
| **Valve management** | Named valves (brew, steam, purge) for clean shot & steam sequences |
| **Pre-infusion** | Configurable pre-wet phase before full-pressure extraction |
| **Cleanup scripts** | Declarative flush/purge sequences after brew and steam |
| **Home Assistant integration** | Full native ESPHome API — sensors, switches, numbers exposed automatically |
| **OTA updates** | Standard ESPHome OTA via Wi-Fi |

---

## Hardware Overview (Philips Barista Brew)

```
┌──────────────────────────────────────────────────────┐
│                  HIGH VOLTAGE SIDE (220 V)            │
│   ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│   │ Thermoblock│  │  Pump    │  │   Grinder Motor  │  │
│   │  (heater)  │  │ (vibrtn) │  │  (relay)         │  │
│   └────┬─────┘  └────┬─────┘  └────────┬─────────┘  │
│        │SSR           │SSR/relay        │relay        │
└────────┼─────────────┼─────────────────┼─────────────┘
         │             │                 │
┌────────┼─────────────┼─────────────────┼─────────────┐
│                  LOW VOLTAGE SIDE (3.3/5 V)           │
│   ┌────▼─────────────▼─────────────────▼───────────┐ │
│   │               ESP32 controller board            │ │
│   │                                                 │ │
│   │  Thermocouple ──► MAX6675/MAX31855              │ │
│   │  Flow meter   ──► GPIO (pulse counting)         │ │
│   │  Valves       ──► GPIO → relay/MOSFET           │ │
│   └─────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

---

## Quick Start

### 1. Wire up the controller board

See [`docs/wiring.md`](docs/wiring.md) for a full pin-out and connection diagram.

### 2. Copy the example configuration

```bash
cp examples/philips_barista_brew.yaml my_espresso.yaml
# Edit my_espresso.yaml to match your pin assignments and Wi-Fi credentials
```

### 3. Flash with ESPHome

```bash
esphome run my_espresso.yaml
```

### 4. Add to Home Assistant

The device will be discovered automatically via the ESPHome native API.

---

## Component YAML Reference

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components: [espresso_machine]

espresso_machine:
  id: my_espresso

  heater:
    id: main_heater
    type: thermoblock       # thermoblock | boiler
    sensor_type: max6675    # max6675 | max31855 | ntc
    cs_pin: GPIO5
    ssr_pin: GPIO4
    pid:
      kp: 2.5
      ki: 0.05
      kd: 15.0

  grinder:
    id: main_grinder
    type: relay             # relay | none
    pin: GPIO23
    default_grind_time: 7s

  flow_meter:
    id: brew_flow
    pin: GPIO34
    pulses_per_ml: 0.5195

  pump:
    id: main_pump
    type: relay             # relay | dimmer
    pin: GPIO25

  valves:
    - id: brew_valve
      pin: GPIO26
    - id: steam_valve
      pin: GPIO27
    - id: purge_valve
      pin: GPIO14

  brew:
    heater: main_heater
    target_temperature: 90°C
    temperature_profile:
      offset: 5             # °C above setpoint at shot start
      ramp_time: 20s        # time to decay back to setpoint
    flow_meter: brew_flow
    flow_max: 40ml
    flow_offset: 20ml
    valve: brew_valve
    purge_valve: purge_valve
    pump: main_pump

  steam:
    heater: main_heater
    target_temperature: 135°C
    flow_max: 2             # ml/s — pump duty-cycle target
    cool_down_to: 90°C
    valve: steam_valve
    purge_valve: purge_valve
    pump: main_pump
```

See [`examples/philips_barista_brew.yaml`](examples/philips_barista_brew.yaml) for the full annotated
example including pre-infusion, cleanup scripts, display, and Home Assistant automations.

---

## Project Structure

See [`structure.md`](structure.md) for the complete layout of source files and their roles.

---

## Roadmap / Plan

See [`PLAN.md`](PLAN.md) for the phased development plan.

---

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

---

## License

Apache 2.0 — see [`LICENSE`](LICENSE).

---

## Acknowledgements & Inspiration

- [pico_espresso](https://github.com/vecinimod/pico_espresso) — Raspberry Pi Pico W based controller
  with PID, pump profiling, flow meter, pressure transducer, and web UI; a major source of inspiration
  for the feature set and hardware approach of this project.
- [ESPHome PID Climate](https://esphome.io/components/climate/pid/) — built-in PID loop used for
  temperature control.
- [ESPHome External Components](https://esphome.io/components/external_components/) — the plugin
  mechanism this project builds on.

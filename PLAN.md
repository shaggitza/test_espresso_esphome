# Project Plan — ESPHome Espresso Machine Controller

## Goal

Reimplement the low-voltage controller of a **Philips Barista Brew** (single thermoblock,
integrated grinder, steam wand, volumetric flow meter) as a reusable **ESPHome external component**.
The result should be drop-in replaceable with the stock controller board using only the existing
wiring harness, and should expose full control through Home Assistant.

The component must follow ESPHome's external-component conventions so that any other user can
reference it from their own YAML with a single `external_components:` block.

---

## Non-Goals (for now)

- Replacing the high-voltage (220 V) wiring or SSRs/relays already present in the machine.
- Building a custom PCB (the project targets a generic ESP32 dev board).
- Pressure profiling via an active pump controller (future phase).
- A standalone web UI independent of Home Assistant (pico_espresso already does this well).

---

## Phased Roadmap

### Phase 0 — Project Initialisation ✅

- [x] Repository created with Apache 2.0 license
- [x] README, PLAN, structure, CONTRIBUTING, NOTICE, CHANGELOG, .gitignore
- [x] GitHub Copilot instructions
- [x] Reference example YAML (`examples/philips_barista_brew.yaml`)

---

### Phase 1 — Core Component Scaffold

**Goal:** An ESPHome external component that compiles and loads without errors.

Tasks:
- [ ] Create `components/espresso_machine/__init__.py` with top-level `CONFIG_SCHEMA`
- [ ] Stub C++ class `EspressoMachine` (`.h` / `.cpp`) inheriting from `esphome::Component`
- [ ] Register component with ESPHome's component registry
- [ ] Validate schema with `esphome config` against the reference YAML
- [ ] CI workflow: lint Python (`flake8`) + validate YAML example

Deliverables:
- Component loads, logs its presence at boot, exposes an `id:` in YAML.

---

### Phase 2 — Heater Subsystem (PID Thermoblock)

**Goal:** Accurate, stable temperature control of the thermoblock via SSR.

Tasks:
- [ ] `heater/__init__.py` — schema: `type`, `sensor_type`, `cs_pin`, `ssr_pin`, `pid {kp, ki, kd, ...}`
- [ ] Wrap ESPHome's built-in `climate.pid` for the control loop (reuse, don't reinvent)
- [ ] Support thermocouple types: MAX6675, MAX31855; NTC resistor
- [ ] Expose `heater.set_temperature` action and `temperature` sensor to HA
- [ ] Safety: over-temperature cutoff (hard limit, independent of PID)
- [ ] Tuning helper: auto-tune mode that runs ESPHome's PID autotune

Deliverables:
- Thermoblock reaches and holds setpoint ±0.5 °C; sensor value visible in HA.

---

### Phase 3 — Flow Meter Integration

**Goal:** Accurate volumetric measurement driving shot termination.

Tasks:
- [ ] `flow_meter/__init__.py` — schema: `pin`, `pulses_per_ml`
- [ ] Interrupt-driven pulse counter (uses ESPHome `pulse_counter` sensor platform)
- [ ] Expose: instantaneous flow rate (ml/s), total volume per shot (ml), lifetime total (ml)
- [ ] Calibration action: `flow_meter.calibrate` — run pump for known volume, compute factor
- [ ] Reset action: `flow_meter.reset`

Deliverables:
- Shot volume displayed in HA; shot auto-terminates at `flow_max` ml.

---

### Phase 4 — Valve & Pump Subsystem

**Goal:** Named, safe valve and pump control.

Tasks:
- [ ] `valve/__init__.py` — schema: `id`, `pin`, (optional) `normally_open`
- [ ] `pump/__init__.py` — schema: `id`, `type` (`relay` | `dimmer`), `pin`
- [ ] Safety interlock: at most one valve open at a time (configurable override)
- [ ] Actions: `valve.open`, `valve.close`, `pump.run` (with optional `volume_ml` or `duration`)
- [ ] Expose each valve and pump as a `switch` entity in HA

Deliverables:
- Valves and pump controllable from HA; interlocks prevent conflicting states.

---

### Phase 5 — Grinder Subsystem

**Goal:** Timed relay grind with Home Assistant control.

Tasks:
- [ ] `grinder/__init__.py` — schema: `type` (`relay` | `none`), `pin`, `default_grind_time`
- [ ] Action: `grinder.grind` with optional `duration` override
- [ ] Expose as a `button` entity (one-shot grind) and `number` entity (grind time)
- [ ] Safety: do not allow grind during active brew or steam

Deliverables:
- Grinder operable from HA with configurable time.

---

### Phase 6 — Brew Mode

**Goal:** Full automated espresso extraction sequence.

Tasks:
- [ ] `brew/__init__.py` — schema: `heater`, `target_temperature`, `temperature_profile`,
  `flow_meter`, `flow_max`, `flow_offset`, `valve`, `purge_valve`, `pump`, `pre_infusion`,
  `cleanup_script`
- [ ] Temperature profile: offset + ramp_time (temperature surfing implementation)
- [ ] Pre-infusion phase: low-pressure soak before full extraction
- [ ] Shot sequence state machine: `idle → heating → pre_infusion → brewing → done → cleanup`
- [ ] Auto-terminate on `flow_max` reached; expose progress sensor (ml brewed)
- [ ] `brew.start` / `brew.stop` actions
- [ ] Publish shot data (time, volume, temperature) as HA events for logging/automation

Deliverables:
- Full shot pulled automatically; shot stats logged to HA.

---

### Phase 7 — Steam Mode

**Goal:** Safe, controlled milk steaming.

Tasks:
- [ ] `steam/__init__.py` — schema: `heater`, `target_temperature`, `flow_max`, `cool_down_to`,
  `valve`, `purge_valve`, `pump`
- [ ] Steam sequence: heat to `target_temperature` → open valve → pump duty-cycle for flow control
- [ ] Auto cool-down: after steam, return heater setpoint to `cool_down_to`
- [ ] `steam.start` / `steam.stop` actions
- [ ] Purge sequence on cool-down

Deliverables:
- Steam wand usable; machine automatically cools back to brew temperature.

---

### Phase 8 — Cleanup Scripts & Automation API

**Goal:** Declarative flush/rinse sequences after brew and steam.

Tasks:
- [ ] Allow `cleanup_script:` block in `brew:` and `steam:` to reference ESPHome script actions
- [ ] Built-in helper actions: `espresso.flush` (pump N ml through purge valve)
- [ ] Document how to combine with ESPHome `script:` platform for custom sequences

Deliverables:
- Machine self-rinses after each shot/steam with a single YAML block.

---

### Phase 9 — Display & UI (Optional)

**Goal:** Local OLED feedback.

Tasks:
- [ ] Optional `display:` integration block in the top-level schema
- [ ] Default display layout: current temp, setpoint, mode, shot volume
- [ ] Works with any ESPHome-supported OLED (SSD1306 / SH1106)

---

### Phase 10 — Documentation & Release

Tasks:
- [ ] `docs/wiring.md` — complete wiring guide with diagrams
- [ ] `docs/pid_tuning.md` — PID tuning guide for thermoblock machines
- [ ] `docs/home_assistant.md` — HA dashboard YAML cards for the espresso machine
- [ ] `docs/troubleshooting.md` — common issues and fixes
- [ ] Tag v1.0.0 release

---

## Hardware Bill of Materials (Reference Build — Philips Barista Brew)

| Component | Purpose | Notes |
|---|---|---|
| ESP32 dev board | Main controller | Any standard 30-pin board |
| MAX6675 or MAX31855 | Thermocouple interface | K-type thermocouple preferred |
| K-type thermocouple | Thermoblock temp sensor | M6 threaded tip, screws into block |
| SSR (10 A) | Thermoblock heater control | DC control input to ESP32 GPIO |
| Relay module (5 V) | Grinder motor | Stock grinder relay replacement |
| Relay module (5 V) | Pump (basic) | Or AC dimmer for profiling |
| Relay module(s) | Brew / steam / purge valves | One per valve |
| Flow meter (AB32 or similar) | Volumetric measurement | ~0.5 pulses/ml calibration |
| 3.3 V / 5 V PSU | Controller power | Derived from machine's LV rail |
| SSD1306 OLED (optional) | Local display | I2C, 128×64 |

---

## Safety Considerations

1. **Galvanic isolation:** The ESP32 must never have a direct electrical path to the 220 V side.
   All control signals must go through opto-isolated SSRs or relay modules.
2. **Thermal runaway protection:** A hard over-temperature limit must be enforced in firmware,
   independent of the PID loop, using a second comparison on every temperature reading.
3. **Watchdog:** ESPHome's built-in watchdog resets the ESP32 if the loop stalls, ensuring
   the SSR defaults to OFF (heater off) on restart.
4. **Valve interlock:** Only one valve may be open at a time unless explicitly overridden —
   prevents cross-contamination between brew and steam paths.
5. **Grinder lockout:** Grinder must not run during an active brew or steam sequence.

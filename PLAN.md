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

**Goal:** All five ESPHome platform modules compile and load without errors.

Each subsystem is its own top-level ESPHome platform (`espresso_machine_valve`,
`espresso_machine_pump`, etc.) following ESPHome's first-class entity pattern.
The `espresso_machine` platform is a pure orchestrator with no hardware of its own.

Tasks:
- [ ] Create stub `__init__.py` + `.h` / `.cpp` for each platform:
  - `espresso_machine/` — orchestrator stub
  - `espresso_machine_valve/` — switch platform stub
  - `espresso_machine_pump/` — switch/number platform stub
  - `espresso_machine_grinder/` — button/number platform stub
  - `espresso_machine_flow_meter/` — sensor platform stub
- [ ] Register all platforms with ESPHome's component registry
- [ ] Validate schema with `esphome config` against the reference YAML
- [ ] CI workflow: lint Python (`flake8`) + validate YAML example

Deliverables:
- All components load, log their presence at boot, and expose their `id:` in YAML.

---

### Phase 2 — Heater (Native ESPHome PID + Thermocouple)

**Goal:** Accurate, stable temperature control of the thermoblock via SSR.

The heater is **not** a custom platform — it uses ESPHome's built-in `climate.pid`
and `sensor.max6675` directly. The orchestrator simply calls `climate.set_temperature`
on the existing entity. This phase ensures the reference YAML compiles end-to-end with a real
thermocouple and SSR.

Tasks:
- [x] Document supported thermocouple types in YAML comments (MAX6675, MAX31855, NTC)
- [x] Document `output.slow_pwm` SSR wiring in `docs/wiring.md`
- [x] Safety: hard over-temperature cutoff via `on_value_range` on `thermoblock_temp` — independent of PID
- [x] Expose PID autotune button in example YAML
- [x] Document control algorithm alternatives (PID vs bang-bang; Ziegler-Nichols vs Cohen-Coon vs autotune)
- [ ] Validate `esphome config` compiles the heater section (requires Phase 1 component scaffold)

Deliverables:
- Thermoblock reaches and holds setpoint ±0.5 °C; sensor and climate entity visible in HA.

---

### Phase 3 — `espresso_machine_flow_meter` Platform

**Goal:** Accurate volumetric measurement driving shot termination.

Tasks:
- [ ] `espresso_machine_flow_meter/__init__.py` — schema: `id`, `name`, `pin`, `pulses_per_ml`
- [ ] Registers as a **sensor platform** exposing two child sensors:
  - `{id}_rate` — instantaneous flow rate (ml/s)
  - `{id}_total` — accumulated volume (ml), resets at shot start
- [ ] Interrupt-driven pulse counter in C++ (`ISR`-safe)
- [ ] Actions: `espresso_machine_flow_meter.reset`, `espresso_machine_flow_meter.calibrate`

Deliverables:
- Shot volume displayed in HA; shot auto-terminates at `flow_max` ml.

---

### Phase 4 — `espresso_machine_valve` Platform

**Goal:** Safe, named solenoid valve control with hardware interlock.

Tasks:
- [ ] `espresso_machine_valve/__init__.py` — schema: `id`, `name`, `pin`, `normally_open`
- [ ] Registers as a **switch platform** — each valve is its own HA switch entity
- [ ] Platform-level interlock: opening one valve closes all others (single-open invariant)
- [ ] Actions: `espresso_machine_valve.open`, `espresso_machine_valve.close`
- [ ] `normally_open: false` default — valves close on power loss

Deliverables:
- Each valve is an independent HA switch; interlock enforced at platform level.

---

### Phase 5 — `espresso_machine_pump` Platform

**Goal:** Vibration pump control (relay or dimmer).

Tasks:
- [ ] `espresso_machine_pump/__init__.py` — schema: `id`, `name`, `type` (`relay`|`dimmer`), `pin`
- [ ] `relay` type: registers as a **switch platform** (on/off HA entity)
- [ ] `dimmer` type: registers as a **number platform** (0–100% HA entity via slow_pwm)
- [ ] Action: `espresso_machine_pump.run` with `volume_ml` or `duration` and optional `valve` arg

Deliverables:
- Pump is an independent HA entity; controllable from scripts and automations.

---

### Phase 6 — `espresso_machine_grinder` Platform

**Goal:** Timed relay grind with Home Assistant control and brew/steam lockout.

Tasks:
- [ ] `espresso_machine_grinder/__init__.py` — schema: `id`, `name`, `type` (`relay`|`none`), `pin`, `default_grind_time`
- [ ] Registers as a **button platform** (one-shot timed grind) and **number platform** (grind time)
- [ ] Lockout: refuses activation when orchestrator is in brew or steam state
- [ ] Action: `espresso_machine_grinder.grind` with optional `duration` override

Deliverables:
- Grinder is an independent HA entity with configurable time; lockout prevents unsafe use.

---

### Phase 7 — Brew Mode (Orchestrator)

**Goal:** Full automated espresso extraction sequence.

Tasks:
- [ ] `espresso_machine/__init__.py` `brew:` sub-schema — references: `heater`, `pump`,
  `flow_meter`, `valve`, `purge_valve`, `target_temperature`, `temperature_profile`,
  `flow_max`, `flow_offset`, `pre_infusion`, `cleanup_script`
- [ ] Temperature surfing: offset + ramp_time in `espresso_machine.cpp`
- [ ] Pre-infusion phase: low-pressure soak before full extraction
- [ ] Shot state machine: `idle → heating → pre_infusion → brewing → done → cleanup`
- [ ] Auto-terminate when `flow_max` ml reached
- [ ] `espresso_machine.brew_start` / `espresso_machine.brew_stop` actions
- [ ] Publish shot stats (time, volume, temperature) as HA events

Deliverables:
- Full shot pulled automatically; shot stats logged to HA.

---

### Phase 8 — Steam Mode (Orchestrator)

**Goal:** Safe, controlled milk steaming.

Tasks:
- [ ] `espresso_machine/__init__.py` `steam:` sub-schema — references: `heater`, `pump`,
  `valve`, `purge_valve`, `target_temperature`, `flow_max`, `cool_down_to`, `cleanup_script`
- [ ] Steam state machine: `idle → heating → steaming → cooling → cleanup`
- [ ] Pump duty-cycle during steaming to maintain `flow_max` ml/s
- [ ] Auto cool-down: set heater setpoint to `cool_down_to` after steam
- [ ] `espresso_machine.steam_start` / `espresso_machine.steam_stop` actions

Deliverables:
- Steam wand usable from HA; machine automatically cools back to brew temperature.

---

### Phase 9 — Cleanup Scripts & Automation API

**Goal:** Declarative flush/rinse sequences after brew and steam.

Tasks:
- [ ] `cleanup_script:` block in `brew:` and `steam:` sub-schemas accepts ESPHome action lists
- [ ] Built-in helper action: `espresso_machine.flush` (pump N ml through purge valve)
- [ ] Document combining with ESPHome `script:` platform for custom sequences

Deliverables:
- Machine self-rinses after each shot/steam with a single YAML block.

---

### Phase 10 — Display & UI (Optional)

**Goal:** Local OLED feedback.

Tasks:
- [ ] Document display lambda using entity IDs from the first-class platforms
- [ ] Default display layout: current temp, setpoint, mode, shot volume
- [ ] Works with any ESPHome-supported OLED (SSD1306 / SH1106)

---

### Phase 11 — Documentation & Release

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

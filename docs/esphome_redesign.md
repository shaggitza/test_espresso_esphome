# ESPHome API Architecture — Redesign Proposals

> **Status: Design Exploration — no code changes yet.**
> This document analyses the current ESPHome external-component architecture used by this
> project and proposes alternative designs that could improve developer experience,
> extensibility, testability, or runtime flexibility.  Each proposal is independent; they
> can be adopted in full, in part, or combined.

---

## 1  Current Architecture (Baseline)

### 1.1  Design summary

The current design follows ESPHome's own philosophy: every hardware subsystem is a
**first-class entity** declared at the top level of the YAML config.  The orchestrator
(`espresso_machine:`) owns no hardware — it references siblings by `id:` and coordinates
them through pure-virtual C++ interfaces (`IHeater`, `IPump`, `IValve`, `IFlowMeter`).

```
YAML (user)
├── espresso_machine_flow_meter:   id: brew_flow      (sensor platform)
├── espresso_machine_valve:        id: brew_valve      (switch platform × N)
├── espresso_machine_pump:         id: main_pump       (switch|number platform)
├── espresso_machine_grinder:      id: main_grinder    (button+number platform)
├── espresso_machine_heater:       id: heater_ctrl     (IHeater adapter)
├── climate.pid:                   id: main_heater     (native ESPHome)
└── espresso_machine:              id: my_espresso     (orchestrator — references all above)
```

Orchestrator runtime flow (simplified):

```
EspressoMachine::loop()
  ├── BrewController::tick()   — runs brew state machine (IDLE→HEATING→BREWING→DONE→CLEANUP)
  └── SteamController::tick()  — runs steam state machine (IDLE→HEATING→PURGING→STEAMING→COOLING→CLEANUP)
```

### 1.2  Strengths

| Strength | Detail |
|---|---|
| **ESPHome-native** | Every subsystem is a standard HA entity; no custom UI needed |
| **Dependency injection** | Interfaces (IHeater, IPump, …) enable unit-testable mocks without hardware |
| **Clear ownership** | Each platform class owns exactly one piece of hardware |
| **Backward compatibility** | Adding new optional YAML keys never breaks existing configs |
| **Fail-safe defaults** | All hardware defaults to OFF on reset (SSR, valves, pump) |

### 1.3  Known friction points

| Problem | Where it shows |
|---|---|
| **Verbose YAML** | User must wire each entity by `id:` in the `espresso_machine:` block; many required keys |
| **Separate platform per subsystem** | User must remember 5–7 top-level platform names |
| **State machine is flat / hard to extend** | Adding a new brew phase requires editing `espresso_machine.cpp` directly |
| **No runtime reconfigurability** | Brew parameters (temperature, flow target) can only change through number entities, not hot-swappable recipes |
| **No persistence across reboots** | State resets to IDLE; a shot in progress is lost on power-cut |
| **Tight orchestrator coupling** | `BrewController` knows about `IPump`, `IValve`, AND `IFlowMeter` — three different subsystems |

---

## 2  Proposal A — Unified Nested Platform (`espresso_machine_device:`)

### 2.1  Concept

Collapse all hardware declarations into a single top-level platform block
(`espresso_machine_device:`) with subsections.  The orchestrator schema becomes
part of the same block.  Each subsection still maps to the same C++ platform class
internally — the change is purely in the YAML surface area.

```yaml
# PROPOSED — single top-level block replaces ~7 separate platform declarations
espresso_machine_device:
  id: my_espresso
  name: "Philips Barista Brew"

  heater:
    sensor: thermoblock_temp      # reference to native ESPHome sensor
    output: heater_ssr            # reference to native ESPHome output
    pid: main_heater              # reference to native climate.pid

  pump:
    type: relay
    pin: GPIO25
    flow_meter:
      pin: GPIO34
      pulses_per_ml: 0.5195

  valves:
    brew:   { pin: GPIO26 }
    steam:  { pin: GPIO27 }
    purge:  { pin: GPIO14 }

  grinder:
    type: relay
    pin: GPIO23
    default_grind_time: 7s

  brew:
    target_temperature: 90°C
    flow_max: 40ml
    flow_offset: 20ml
    pre_infusion:
      volume_ml: 5ml
      hold_time: 5s

  steam:
    target_temperature: 135°C
    purge_volume: 5ml
    cool_down_to: 90°C
    timeout: 5min
```

### 2.2  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **YAML verbosity** | ~40% fewer lines; one block to find | Less like standard ESPHome pattern |
| **HA entity names** | Predictable (all prefixed by device name) | Less user control over entity IDs |
| **Valve interlock** | Trivial — all valves declared in one schema | Harder to support arbitrary valve counts |
| **ESPHome compatibility** | Diverges from first-class-entity pattern | Violates ESPHome conventions; external component behaviour may differ |
| **Migration** | Requires a YAML migration for existing users | Breaking change |

### 2.3  Verdict

**Not recommended for adoption** as the primary API.  The verbosity reduction is real,
but the divergence from ESPHome conventions creates long-term friction and loses the
ability to drive individual entities directly from HA without the orchestrator.

However, a **convenience schema wrapper** could be offered alongside the current API as
an opt-in shorthand (similar to ESPHome's `sensor.average` wrapping `sensor.template`).

---

## 3  Proposal B — Event-Bus / Reactive Architecture

### 3.1  Concept

Replace the polling `loop()` / tick-based state machines with an **event bus** where
components publish typed events and subscribe to conditions.  The orchestrator becomes a
pure **rule engine** that reacts to events rather than polling state on every loop tick.

```cpp
// Each component publishes events:
flow_meter_.on_volume_reached(40.0f, []() { brew_.on_flow_target_met(); });
heater_.on_temperature_reached(90.0f, []() { brew_.on_heater_ready(); });
valve_.on_opened([](ValveId id) { event_bus_.publish(ValveOpenEvent{id}); });

// Brew controller subscribes:
event_bus_.subscribe<HeaterReadyEvent>([this](auto&) {
  if (state_ == BrewState::HEATING) transition(BrewState::PRE_INFUSION);
});
event_bus_.subscribe<FlowTargetMetEvent>([this](auto& e) {
  if (state_ == BrewState::BREWING) transition(BrewState::DONE);
});
```

### 3.2  Implementation sketch

A lightweight event bus implemented as a static singleton with typed event queues:

```cpp
// interfaces.h addition
template <typename EventT>
class IEventBus {
 public:
  virtual void publish(const EventT& event) = 0;
  virtual void subscribe(std::function<void(const EventT&)> handler) = 0;
};

// Concrete events
struct HeaterReadyEvent   { float current_temp; float target_temp; };
struct FlowTargetMetEvent { float volume_ml; };
struct ValveOpenEvent     { const char* valve_id; };
struct ShotDoneEvent      { float volume_ml; float duration_s; };
```

### 3.3  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **Extensibility** | New phases added without editing core state machine | Event ordering bugs are subtle and hard to debug |
| **Decoupling** | Heater component doesn't know about the brew controller | Implicit control flow; harder to follow in code review |
| **Testability** | Events can be injected in unit tests without mock objects | Requires event-bus infrastructure not present in ESPHome core |
| **Performance** | No spinning on conditions already met | `std::function` heap allocation on ESP32 (~96 B per callback) |
| **ESPHome fit** | ESPHome `Automation` is already event-like | ESPHome uses lambda-chains, not a bus; mismatch |

### 3.4  Verdict

**Partially adopt.**  The event-bus idea is most useful at the boundary between the
orchestrator and HA (i.e. exposing `on_brew_start`, `on_brew_done`, `on_shot_stats`
callbacks in YAML via `on_*:` automation triggers).  The internal state machines should
remain tick-based for predictability and debuggability on embedded hardware.

**Actionable change:** Add `on_brew_done:`, `on_steam_done:`, and `on_shot_stats:`
automation hooks to `espresso_machine:` schema so users can trigger HA scripts without
a separate `interval:` sensor hack.

---

## 4  Proposal C — Profile-First API (Declarative Shot Language)

### 4.1  Concept

Make the primary API **profile-first**: every shot is described by a profile, and the
`espresso_machine.brew_start` action *always* takes a profile argument.  The current
flat `brew:` sub-schema becomes the **default profile**, automatically generated from
the existing YAML keys for backward compatibility.

```yaml
# User declares profiles (already planned in Phase 12 / docs/profiles.md)
espresso_machine_profile:
  - id: profile_classic
    name: "Classic"
    temperature: 90°C
    phases:
      - type: pre_infusion
        volume_ml: 5ml
        hold_time: 5s
      - type: extraction
        exit:
          volume_ml: 36ml

# Orchestrator references a default profile
espresso_machine:
  id: my_espresso
  brew:
    default_profile: profile_classic   # replaces individual target_temperature / flow_max keys
    pump: main_pump
    valve: brew_valve
    purge_valve: purge_valve
```

### 4.2  Migration path

The existing flat keys (`target_temperature`, `flow_max`, `flow_offset`, `pre_infusion`)
are still accepted and auto-synthesized into an anonymous `__default_profile__` at
code-generation time.  This makes the change **non-breaking**.

```python
# __init__.py migration shim (simplified)
if CONF_TARGET_TEMPERATURE in brew_config and CONF_DEFAULT_PROFILE not in brew_config:
    # Auto-synthesize a default profile from flat keys
    brew_config[CONF_DEFAULT_PROFILE] = synthesize_profile(brew_config)
```

### 4.3  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **Runtime flexibility** | Switch profiles from HA without reflashing | Profile schema adds YAML complexity |
| **Multi-phase support** | Natural home for pressure/flow curves (Phase 12) | Requires dimmer pump for pressure profiling |
| **HA select entity** | Profile list auto-published as HA `select` entity | Another entity in HA |
| **Testing** | Each profile is a data structure, easily unit-tested | Profile executor adds a new state machine layer |
| **Migration** | Auto-synthesis from flat keys = no breaking change | Auto-synthesis hides complexity; debugging harder |

### 4.4  Verdict

**Strongly recommended** as the long-term direction.  The `docs/profiles.md` document
already describes this design.  This proposal formalises the migration path and confirms
the auto-synthesis shim as the compatibility bridge.

The key implementation decision: **profiles are compiled into firmware** (zero runtime heap
allocation) as `const` struct arrays.  Runtime selection is by index into this array;
no dynamic profile loading is needed for v1.

---

## 5  Proposal D — Typed Action Parameters (Template-Safe Brew Control)

### 5.1  Current problem

The `espresso_machine.brew_start` and `espresso_machine.steam_start` actions take no
parameters — all settings are baked into the YAML at compile time.  Users who want to
change, say, the target temperature for a single shot must expose a number entity and
hope the orchestrator reads it before the shot starts.

### 5.2  Concept

Make `brew_start` and `steam_start` accept **full typed parameter overrides** using
ESPHome's `cv.templatable()` mechanism:

```yaml
# Trigger a shot with a custom temperature override
- espresso_machine.brew_start:
    id: my_espresso
    target_temperature: !lambda "return id(brew_temp_number).state;"
    flow_max: 38ml
    profile: profile_bloom       # optional: override default profile too
```

All parameters default to the compiled-in values if omitted (backward compatible).  In
C++ the action class captures the parameters as `TemplatableValue<float>` and applies
them transactionally at brew start.

```cpp
// Generated C++ action
template <typename Ts>
class BrewStartAction : public Action<Ts> {
 public:
  TEMPLATABLE_VALUE(float, target_temperature)
  TEMPLATABLE_VALUE(float, flow_max_ml)

  void play(Ts... x) override {
    auto* machine = this->parent_;
    BrewOverrides overrides;
    if (has_target_temperature_) overrides.target_temperature = target_temperature_.value(x...);
    if (has_flow_max_ml_)        overrides.flow_max_ml = flow_max_ml_.value(x...);
    machine->brew_start(overrides);
  }
};
```

### 5.3  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **Flexibility** | Per-shot overrides without number entities or reflashing | Action schema becomes larger and harder to document |
| **Template support** | Lambda-based values integrate with ESPHome automations | Code generation is more complex |
| **Safety** | Overrides are transient — the next shot reverts to defaults | Overrides silently ignored if keys misspelled (ESPHome validates at compile time though) |
| **HA scripts** | HA service calls can include temperature as a field | HA service call schema must be hand-crafted |

### 5.4  Verdict

**Recommended for brew_start, medium priority.**  The immediate win is letting users
run a slightly hotter or cooler shot without reflashing.  Combine with Proposal C
(profile-first): if a profile is specified, its parameters win over flat overrides.

Priority order: `profile params` > `action overrides` > `compiled-in defaults`.

---

## 6  Proposal E — IScale Interface & Weight-First Exit Strategy

### 6.1  Current problem

Shot exit is volumetric-only (flow meter pulses).  Weight-based exit — more accurate
because it measures what is in the cup — requires a scale, which is currently planned
as a separate platform (`espresso_machine_scale`, Phase 13).

### 6.2  Concept

Introduce `IScale` as a **first-class interface** in `interfaces.h` alongside `IHeater`
and `IPump`, with a **priority-based exit strategy** pattern:

```cpp
// interfaces.h
class IScale {
 public:
  virtual float get_weight_g() const = 0;
  virtual float get_flow_g_per_s() const = 0;
  virtual bool  is_connected() const = 0;
  virtual void  tare() = 0;
  virtual ~IScale() = default;
};

// Brew exit condition strategy (replaces hard-coded checks in BrewController)
class IBrewExitStrategy {
 public:
  // Called every loop tick. Returns true when the shot should stop.
  virtual bool should_stop(float volume_ml, float weight_g) const = 0;
  virtual ~IBrewExitStrategy() = default;
};

class VolumeExitStrategy : public IBrewExitStrategy { ... };   // flow_max
class WeightExitStrategy : public IBrewExitStrategy { ... };   // target_weight (scale)
class FallbackExitStrategy : public IBrewExitStrategy {        // scale → volume fallback
  // Checks scale first; falls back to volume if scale is stale.
};
```

The orchestrator selects the exit strategy at `brew_start()` time based on what is wired:

```cpp
void BrewController::brew_start() {
  if (scale_ && scale_->is_connected()) {
    exit_strategy_ = &weight_exit_;   // primary: weight
  } else {
    exit_strategy_ = &volume_exit_;   // fallback: volume
  }
  ...
}
```

### 6.3  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **Extensibility** | New exit conditions (pressure, time) added as new strategy classes | One more abstraction layer to understand |
| **Testability** | Each strategy is independently unit-testable | Strategy selection logic must itself be tested |
| **Safety** | Explicit fallback path always present | Programmer error: forgetting to set a strategy |
| **ESPHome fit** | Matches existing interface pattern (IHeater, IPump) | `std::variant` or pointer selection adds ~4 B per strategy pointer on heap |

### 6.4  Verdict

**Strongly recommended.**  The `IBrewExitStrategy` pattern is the cleanest way to add
scale support without littering `BrewController::tick()` with if/else chains.  This
directly enables Phase 13 (scale platform) with minimal risk of regressions.

---

## 7  Proposal F — Persistent State (Survive Reboot)

### 7.1  Current problem

If the ESP32 reboots or crashes mid-shot, all state is lost.  The machine returns to
IDLE.  For safety this is acceptable, but for diagnostics it is a gap: there is no
record of what was happening when the crash occurred.

### 7.2  Concept

Use ESPHome's `globals:` platform (backed by non-volatile storage) to persist a minimal
state record across reboots:

```yaml
globals:
  - id: last_state
    type: int
    restore_value: true
    initial_value: '0'           # 0 = IDLE

  - id: last_crash_volume_ml
    type: float
    restore_value: true
    initial_value: '0.0'

  - id: last_crash_timestamp
    type: uint32_t
    restore_value: true
    initial_value: '0'
```

On `setup()`, the orchestrator reads `last_state`.  If it is non-zero (i.e. the machine
was active when it rebooted), it logs a diagnostic and publishes a `status_sensor` event:
`"Recovered from crash in BREWING state at 14.2 mL"`.

The machine always **returns to IDLE on reboot** — no attempt to resume the shot.
Resuming a brew automatically would be unsafe (puck could be exhausted, cup could
overflow).

### 7.3  Persistent shot statistics

The existing shot statistics (`last_shot_time_s`, `last_shot_volume_ml`) are already
good candidates for persistence via `globals: restore_value: true`.  This is a low-risk,
high-value addition:

```yaml
# Already exists as sensor entities — just add restore_value: true to the backing globals
globals:
  - id: last_shot_time_s_global
    type: float
    restore_value: true
  - id: last_shot_volume_ml_global
    type: float
    restore_value: true
```

### 7.4  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **Diagnostics** | Crash context survives reboot; helps debug | Extra flash writes (NVS wear, though negligible) |
| **Shot stats** | Last-shot stats survive reboot/power cycle | User must understand that stats are from the previous boot |
| **Safety** | Machine always returns to IDLE — no auto-resume | Partial shots are silently abandoned |
| **Complexity** | globals: already supported in ESPHome | Schema must expose globals IDs |

### 7.5  Verdict

**Recommended in two stages:**
1. **Short-term:** Persist shot statistics only — low risk, immediately useful.
2. **Medium-term:** Add crash-state record with diagnostic reporting.
Auto-resume of interrupted shots is explicitly **out of scope** for safety reasons.

---

## 8  Proposal G — Native ESPHome Climate Mode Integration

### 8.1  Concept

Model the espresso machine as a **climate entity** natively in ESPHome/Home Assistant.
Instead of separate brew/steam state machines with custom actions, the machine exposes
a single climate entity with custom modes:

```
climate entity: espresso_machine
  modes: [off, brew, steam, flush]
  current_temperature: thermoblock_temp
  target_temperature: (mode-dependent setpoint)
  preset: [profile_classic, profile_bloom, ...]
```

Changing `mode` to `brew` triggers `brew_start()`.  Changing to `off` triggers stop.
Changing `preset` selects the brew profile.

### 8.2  Advantages

- **Native HA climate card** — no custom dashboard YAML needed; the built-in HA climate
  card shows current/target temperature and mode with voice-control support.
- **Voice control** — "Hey Google, set espresso machine to brew mode" works out of the box.
- **HA scenes/automations** — climate entity works natively in HA scenes, scripts, and
  the mobile app widget.

### 8.3  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **HA integration** | Climate card, voice control, scenes work natively | Climate entity semantics (heat/cool) don't map cleanly to espresso machine modes |
| **PID conflict** | Machine exposes one climate entity | The heater is *also* a climate entity (climate.pid); two climate entities for one machine |
| **Action surface** | Mode change = state transition | HA climate mode is a string — no type safety; typo-proof only via `select:` |
| **ESPHome fit** | `climate::Climate` base class is well-tested | Must implement `control()`, `traits()` on `EspressoMachine` — significant refactor |

### 8.4  Verdict

**Not recommended as primary API**, but **worth adding as a thin adapter layer**.
Specifically: expose a `select` entity (not a climate entity) for machine mode
(`idle | brew | steam | flush | off`) that wraps the existing `brew_start` /
`steam_start` / `flush` actions.  This gives the HA UX benefits without the semantic
mismatch of using a climate entity for a machine that is not a thermostat.

---

## 9  Proposal H — Component Discovery via Roles (Tagless Wiring)

### 9.1  Current problem

The orchestrator block requires explicit `id:` references for every wired component:

```yaml
espresso_machine:
  brew:
    heater: main_heater        # must match id: of climate.pid
    pump: main_pump            # must match id: of espresso_machine_pump
    flow_meter: brew_flow      # must match id: of espresso_machine_flow_meter
    valve: brew_valve          # must match id: of espresso_machine_valve
    purge_valve: purge_valve   # must match id: of espresso_machine_valve (different valve)
```

Each new subsystem (e.g. scale) adds another required `id:` reference.

### 9.2  Concept

Give each platform component a **role** annotation.  The orchestrator auto-discovers
components by role without needing explicit `id:` wiring:

```yaml
espresso_machine_pump:
  id: main_pump
  role: brew_pump              # ← role annotation

espresso_machine_valve:
  - id: brew_valve
    role: brew_valve           # ← role annotation
  - id: purge_valve
    role: brew_purge_valve

espresso_machine:
  id: my_espresso
  # No explicit id: references needed — components are found by role
  brew:
    target_temperature: 90°C
    flow_max: 40ml
```

At schema validation time, the Python `to_code()` function queries all registered
components with matching roles and wires them automatically.

### 9.3  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **Verbosity** | Eliminates 5–8 explicit `id:` references in orchestrator block | Role typos cause silent mis-wiring (hard to debug) |
| **Discoverability** | YAML is more self-documenting | Less explicit — reader cannot tell what is wired where without checking all components |
| **Multi-machine** | Clean for single-machine configs | Ambiguous if two pumps share the same role in a dual-machine setup |
| **ESPHome fit** | ESPHome uses explicit IDs throughout | Diverges from ESPHome conventions; may confuse experienced users |

### 9.4  Verdict

**Not recommended for the primary wiring pattern**, but a useful **validation aid**: the
schema could warn the user if a required role is missing (e.g. no component with
`role: brew_pump` declared) instead of a confusing `use_id` resolution error.

---

## 10  Proposal I — REST / MQTT API Shim

### 10.1  Concept

Add an optional `espresso_machine_api:` component that exposes a thin REST or MQTT
interface for integration with non-Home-Assistant systems (e.g. Node-RED, custom iOS
shortcut, a Raspberry Pi running a local dashboard).

```yaml
espresso_machine_api:
  id: api_shim
  machine: my_espresso
  rest:
    port: 8080
    endpoints:
      - path: /brew/start
        action: espresso_machine.brew_start
      - path: /brew/stop
        action: espresso_machine.brew_stop
      - path: /status
        action: espresso_machine.get_status
  mqtt:
    broker: homeassistant.local
    topic_prefix: espresso/
```

### 10.2  Trade-offs

| Aspect | Pros | Cons |
|---|---|---|
| **Independence from HA** | Machine works without HA running | REST server runs on ESP32 — adds ~20 KB flash + RAM overhead |
| **Node-RED / iOS Shortcuts** | Easy to call from any HTTP client | Security: no auth by default (add API key at minimum) |
| **MQTT** | Native for Home Assistant MQTT integration | Redundant with ESPHome native API for HA users |
| **ESPHome fit** | ESPHome has native `web_server:` component | Custom REST component would duplicate `web_server:` functionality |

### 10.3  Verdict

**Not recommended as a new component.**  ESPHome's built-in `web_server:` already
exposes HTTP endpoints for every entity action.  MQTT is natively supported via
`mqtt:` in ESPHome.  Users who need non-HA integration should use these existing
mechanisms.  The only gap is **machine-level actions** (brew_start, steam_start):
these can be exposed via template buttons that trigger the actions, making them
accessible through `web_server:` without any custom code.

---

## 11  Summary & Recommended Roadmap

### Priority-ranked recommendations

| Rank | Proposal | Effort | Impact | Recommendation |
|---|---|---|---|---|
| 1 | **C — Profile-First API** | High | Very High | Adopt as long-term primary API; auto-synthesise from flat keys for backward compat |
| 2 | **E — IBrewExitStrategy** | Medium | High | Adopt before Phase 13 (scale); direct enabler for weight-based exit |
| 3 | **D — Typed Action Parameters** | Medium | High | Adopt for `brew_start` / `steam_start`; enables per-shot overrides from HA |
| 4 | **B — Automation hooks** (partial) | Low | Medium | Add `on_brew_done:` / `on_shot_stats:` hooks — zero internal refactor needed |
| 5 | **F — Persistent shot stats** | Low | Medium | Add `restore_value: true` to shot-stats globals |
| 6 | **A — Unified schema** (optional) | Medium | Low | Offer as an optional convenience shim only; do not replace current API |
| 7 | **G — Climate mode adapter** | Low | Low | Expose `select` for machine mode; skip full climate entity refactor |
| 8 | **H — Role-based discovery** | High | Low | Explore only as a schema validation aid; not for primary wiring |
| 9 | **I — REST/MQTT shim** | High | Low | Not needed; delegate to `web_server:` + template buttons |

### Suggested implementation order

```
Phase 11 (current)  → Add on_brew_done: / on_shot_stats: hooks (Proposal B partial)
                    → Persist shot stats (Proposal F, stage 1)
Phase 12            → Profile-first API (Proposal C); auto-synth shim for backward compat
Phase 12+           → Typed brew_start parameters (Proposal D)
Phase 13            → IBrewExitStrategy + IScale interface (Proposal E)
Future              → Machine-mode select entity (Proposal G partial)
```

### Non-starters (explicitly ruled out)

- Auto-resuming interrupted shots (safety risk)
- Custom REST server (duplicate of `web_server:`)
- Full climate-entity refactor (semantic mismatch)
- Role-based component discovery (implicit wiring is hard to debug on embedded hardware)

---

## 12  Open Questions

1. **Profile storage:** Compiled-in struct array (current proposal) vs. LittleFS JSON
   for OTA profile updates without firmware rebuild.  The JSON path is more flexible but
   adds ~30 KB flash overhead and complex de/serialisation.

2. **Per-phase temperature control:** Should each brew profile phase be able to override
   the heater setpoint (e.g. drop temperature mid-shot for a decline profile)?  This
   requires `IHeater::set_target_temperature()` to be called from the phase executor,
   which could interfere with steam-mode cooldown logic.

3. **Pressure sensor abstraction:** A pressure sensor (0–16 bar transducer) would enable
   closed-loop pressure profiles.  Should it be modelled as an `IPressure` interface
   (matching `IFlowMeter` and `IScale`) or as a standard ESPHome sensor referenced by
   `id:` in the profile schema?

4. **Brew ratio target:** Some users specify a target brew ratio (e.g. 1:2 — 18 g in,
   36 g out) rather than an absolute weight.  Should `target_weight` support a ratio
   expression (`ratio: 2.0`) alongside an absolute value, requiring grinder dosing info?

5. **Wi-Fi / HA disconnect handling:** Currently a brew timeout stops the shot if the
   loop stalls.  Should there be an explicit "offline mode" where the machine can complete
   a shot even if Wi-Fi is disconnected (i.e. rely entirely on the on-device state machine
   and do not require HA to ACK shot completion)?

---

## 13  Related Documents

| Document | Relevance |
|---|---|
| [`docs/profiles.md`](profiles.md) | Full design for Proposal C (profile-first API) |
| [`docs/scales.md`](scales.md) | Full design for Proposal E (IScale + IBrewExitStrategy) |
| [`PLAN.md`](../PLAN.md) | Phased roadmap — Phase 12 (profiles), Phase 13 (scale) |
| [`FEATURES.md`](../FEATURES.md) | Current feature status table |
| [`components/espresso_machine/interfaces.h`](../components/espresso_machine/interfaces.h) | IHeater, IPump, IValve, IFlowMeter definitions |
| [`components/espresso_machine/__init__.py`](../components/espresso_machine/__init__.py) | Current YAML schema |

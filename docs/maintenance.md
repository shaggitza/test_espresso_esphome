# Maintenance Routines — Descale and Backflush

This document describes the automated **descale** and **backflush** cleaning routines
implemented in the ESPHome Espresso Machine Controller.  Both routines are controlled
directly from Home Assistant with a single button press.

---

## Descale Routine

### Why descale?

Mineral deposits (limescale) build up inside the thermoblock and brew path over time,
reducing heating efficiency and potentially clogging the machine.  Descaling dissolves
these deposits using a mild acid solution (citric acid or a commercial descaler).

### Recommended frequency

Descale every 1–3 months depending on water hardness.  With very hard water (>200 ppm
TDS) descale monthly; with soft water (<100 ppm) every 2–3 months.

### Hardware required

- A descaling solution (citric acid 1 tsp / 500 ml, or a commercial product)
- No additional hardware — uses the existing brew pump and purge valve

### How it works

The descale state machine runs `cycles` iterations of:

```
PUMPING  →  SOAKING  →  PUMPING  →  SOAKING  → … →  DONE → IDLE
```

| Phase | What happens |
|-------|-------------|
| PUMPING | Brew pump runs; descaling solution is pushed through the brew purge valve and thermoblock for `pump_time` |
| SOAKING | Pump stops; solution sits inside the thermoblock for `soak_time` to dissolve scale deposits |
| DONE | Pump stops, purge valve closes; machine returns to idle |

### Step-by-step procedure

1. **Mix the descaling solution** — fill the water tank with the recommended
   concentration (follow your descaler's label).
2. **Place a container** under the brew group / purge port to collect the effluent.
3. **Press "Descale Start"** in the Home Assistant device page (or fire the
   `espresso_machine.descale_start` action from an automation).
4. **Watch the status sensor** in HA — it shows `Descaling: pumping (cycle 1/3)`,
   `Descaling: soaking (cycle 1/3)`, etc.
5. **Wait for completion** — the machine returns to IDLE automatically.
6. **Rinse** — empty and refill the tank with fresh water, then run 2–3 manual
   flush cycles:
   ```yaml
   - espresso_machine.flush:
       id: my_espresso
       volume_ml: 200ml
   ```
   Alternatively, press "Descale Start" again with fresh water in the tank (the
   same pump/soak cycle helps rinse residual acid from the thermoblock).

### YAML configuration

Inside the `espresso_machine:` block:

```yaml
espresso_machine:
  id: my_espresso
  # ... brew/steam config ...

  descale:
    cycles: 3       # number of pump-on / soak cycles (default: 3)
    pump_time: 30s  # how long to pump per cycle (default: 30 s)
    soak_time: 30s  # how long to soak between cycles (default: 30 s)
```

Trigger buttons (add to the `button:` section):

```yaml
button:
  - platform: template
    name: "Descale Start"
    on_press:
      - espresso_machine.descale_start:
          id: my_espresso

  - platform: template
    name: "Descale Stop"
    on_press:
      - lambda: id(my_espresso).descale_stop();
```

Or trigger from a YAML automation:

```yaml
- service: esphome.my_machine_descale_start
```

### Emergency stop

Press "Descale Stop" at any time to abort the routine.  All hardware (pump, valves)
is stopped immediately and the machine returns to IDLE.

---

## Backflush Routine

### Why backflush?

Coffee oils and micro-grounds accumulate in the group head, shower screen, and
solenoid valve over time.  Backflushing forces water backwards through these
components, flushing out residues.

### Recommended frequency

Backflush weekly (dry, without detergent) and monthly (with backflush detergent
such as Cafiza or Puly Caff).

### Hardware required

- A **blind filter basket** (a solid disc with no holes) — inserted into the
  portafilter in place of the standard basket
- A **3-way solenoid brew valve** — standard on most semi-automatic espresso
  machines; releases pressure through a separate port when de-energised
- Optionally: backflush detergent (Cafiza, Puly Caff, etc.)

> **Note:** The backflush routine uses the **brew valve** (not the purge valve)
> and relies on the 3-way solenoid to release back-pressure through the purge
> port when the valve closes.  If your machine does not have a 3-way solenoid,
> backflushing is not applicable.

### How it works

The backflush state machine runs `cycles` iterations of:

```
PRESSURIZING → RELEASING → PRESSURIZING → RELEASING → … → DONE → IDLE
```

| Phase | What happens |
|-------|-------------|
| PRESSURIZING | Brew valve opens and pump runs; water is forced against the blind filter, building back-pressure in the group head for `pressurize_time` |
| RELEASING | Pump stops and brew valve closes; the 3-way solenoid depressurises the group head through the purge port, flushing loosened residue for `release_time` |
| DONE | All hardware stops; machine returns to idle |

### Step-by-step procedure

**With detergent (monthly deep clean):**

1. **Install the blind filter** in the portafilter basket.
2. **Add detergent** — one dose (≈ 1 g) of backflush detergent to the blind filter.
3. **Lock the portafilter** into the group head.
4. **Press "Backflush Start"** in the HA device page.
5. **Watch the status sensor** — `Backflush: pressurizing (cycle 1/5)`, etc.
6. **Wait for completion** — machine returns to IDLE.
7. **Remove and rinse** the portafilter and blind filter under warm water.
8. **Reinstall without detergent** and repeat with fresh water:
   press "Backflush Start" 2–3 more times to rinse out detergent residue.

**Without detergent (weekly rinse):**

Skip steps 2 and 8 — otherwise the procedure is identical.

### YAML configuration

Inside the `espresso_machine:` block:

```yaml
espresso_machine:
  id: my_espresso
  # ... brew/steam config ...

  backflush:
    cycles: 5              # number of pressurize / release cycles (default: 5)
    pressurize_time: 10s   # how long to pump per cycle (default: 10 s)
    release_time: 10s      # how long to release per cycle (default: 10 s)
```

Trigger buttons (add to the `button:` section):

```yaml
button:
  - platform: template
    name: "Backflush Start"
    on_press:
      - espresso_machine.backflush_start:
          id: my_espresso

  - platform: template
    name: "Backflush Stop"
    on_press:
      - lambda: id(my_espresso).backflush_stop();
```

### Emergency stop

Press "Backflush Stop" at any time to abort the routine.  All hardware (pump, valves)
is stopped immediately and the machine returns to IDLE.

---

## Safety notes

- Both routines are only accepted when the machine is **powered on** and **idle**
  (not brewing or steaming).
- Pressing "Descale Start" or "Backflush Start" during an active brew or steam session
  is silently ignored.
- If the **hard over-temperature cutoff** is triggered, both routines are blocked
  until the device is rebooted.
- `machine_off()` during either routine stops all hardware immediately.

---

## Status sensor values

When a status text sensor is configured (`status_sensor:` in the `espresso_machine:`
block), the following values are published during maintenance routines:

| Routine | Status string |
|---------|--------------|
| Descaling, pumping | `Descaling: pumping (cycle N/M)` |
| Descaling, soaking | `Descaling: soaking (cycle N/M)` |
| Descaling complete | `Descale done` |
| Backflushing, pressurizing | `Backflush: pressurizing (cycle N/M)` |
| Backflushing, releasing | `Backflush: releasing (cycle N/M)` |
| Backflushing complete | `Backflush done` |

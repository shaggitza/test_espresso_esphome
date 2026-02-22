# PID Tuning Guide — Thermoblock Espresso Machines

Thermoblocks behave very differently from traditional boilers. They heat a small volume of
water very quickly, which means:

- Thermal mass is **low** → the system responds fast → derivative term matters more.
- Overshoot is a real risk → start with conservative gains.
- The PID setpoint you see in ESPHome is the **thermoblock surface temperature**, which
  will be higher than the actual brew water temperature at the group head.

---

## Control Algorithm Alternatives

Before tuning PID parameters, choose the right algorithm for your needs:

| Algorithm | Accuracy | Ease of setup | ESPHome platform | Notes |
|---|---|---|---|---|
| **PID** (recommended) | ±0.5 °C | Medium — needs tuning | `climate.pid` | Best overall; use with `slow_pwm` output |
| **Bang-bang / Thermostat** | ±1–2 °C | Easy — set deadband only | `climate.thermostat` | No kp/ki/kd; use with `gpio` output |
| **Manual lambda** | Unlimited | Hard — write custom C++ | `climate.pid` + lambda | For advanced users; override output function |

> For most home espresso machines, **PID with autotune** is the recommended path.
> Only switch to bang-bang if PID oscillations persist after tuning or you need simpler setup.

---

## Recommended Starting Values (Philips Barista Brew)

```yaml
control_parameters:
  kp: 2.5
  ki: 0.05
  kd: 15.0
  output_averaging_samples: 5
  derivative_averaging_samples: 5
deadband_parameters:
  threshold_high: 0.3   # °C
  threshold_low: -0.3   # °C
```

### Alternative profiles for different machine types

| Machine type | kp | ki | kd | Notes |
|---|---|---|---|---|
| Single thermoblock (Philips, DeLonghi) | 2.5 | 0.05 | 15.0 | Low thermal mass; high kd |
| Dual boiler (large) | 1.0 | 0.02 | 5.0 | Higher mass, slower response |
| E61 group head + boiler | 1.5 | 0.03 | 8.0 | Medium mass, group adds lag |

---

## ESPHome PID Autotune

ESPHome's PID climate platform includes a built-in autotune (relay method). To run it:

1. Add the `pid.autotune` call to a `button:` entity in your YAML:

```yaml
button:
  - platform: template
    name: "PID Autotune"
    on_press:
      - climate.pid.autotune: main_heater
```

2. Flash, then press the button from Home Assistant.
3. Watch the ESPHome logs. The autotune will oscillate the heater and compute kp/ki/kd.
4. Copy the suggested values back into your YAML and reflash.

> **Tip:** Run autotune with the portafilter locked in and a full water tank — mimics real
> operating conditions and produces more accurate gains.

---

## Manual Tuning Methods

### Ziegler-Nichols (classic)

1. Set `ki: 0` and `kd: 0`.
2. Increase `kp` slowly until the temperature oscillates steadily (ultimate gain Ku).
3. Note the oscillation period Tu (in seconds).
4. Compute: `kp = 0.6 * Ku`, `ki = 2 * kp / Tu`, `kd = kp * Tu / 8`.
5. Add the deadband to prevent integral windup when at setpoint.

### Cohen-Coon (alternative — better for slow systems / large boilers)

1. Apply a step change to the heater output and record the temperature response curve.
2. Identify: dead time L (seconds), time constant T (seconds), steady-state gain K (°C per unit output).
3. Compute:
   - `kp = (1.35 / K) * (T/L + 0.185)`
   - `ki = kp / (2.5 * L)`
   - `kd = kp * 0.37 * L`

> Cohen-Coon is better when there is noticeable transport delay (long tube between thermoblock
> and thermocouple, or a large group head). Ziegler-Nichols tends to overestimate kp for these systems.

### Trial-and-error (simplest)

1. Set `kp: 1.0`, `ki: 0`, `kd: 0`. Observe steady-state error.
2. Increase `ki` until steady-state error is eliminated (typical range 0.01–0.1).
3. Increase `kd` until overshoot is eliminated (typical range 5–25 for thermoblocks).
4. Fine-tune `kp` last.

---

## Understanding Thermoblock Temperature Offset

The thermocouple measures the thermoblock surface temperature. The water exiting the
thermoblock is typically **5–15 °C cooler** depending on flow rate and thermal contact.

Calibrate by measuring actual brew water temperature with a scace device or a
calibrated thermometer in the portafilter. Adjust `target_temperature` upward to
compensate for this offset until you achieve the desired brew water temperature.

---

## Temperature Surfing (Profile)

The `temperature_profile` in the `brew:` block implements a simple temperature offset
that decays over time. This compensates for the cold portafilter absorbing heat at the
start of the shot:

```yaml
brew:
  temperature_profile:
    offset: 5°C      # raise setpoint by 5 °C at shot start
    ramp_time: 20s   # decay linearly back to target_temperature over 20 s
```

You can tune `offset` and `ramp_time` by pulling shots and tasting. A common starting
point is +5 °C for 15–25 seconds.

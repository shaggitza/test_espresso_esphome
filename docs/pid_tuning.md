# PID Tuning Guide — Thermoblock Espresso Machines

Thermoblocks behave very differently from traditional boilers. They heat a small volume of
water very quickly, which means:

- Thermal mass is **low** → the system responds fast → derivative term matters more.
- Overshoot is a real risk → start with conservative gains.
- The PID setpoint you see in ESPHome is the **thermoblock surface temperature**, which
  will be higher than the actual brew water temperature at the group head.

---

## Recommended Starting Values (Philips Barista Brew)

```yaml
pid:
  kp: 2.5
  ki: 0.05
  kd: 15.0
  output_averaging_samples: 5
  derivative_averaging_samples: 5
  deadband_parameters:
    threshold_high: 0.3°C
    threshold_low: -0.3°C
```

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

---

## Manual Tuning (Ziegler-Nichols method)

1. Set `ki: 0` and `kd: 0`.
2. Increase `kp` slowly until the temperature oscillates steadily (ultimate gain Ku).
3. Note the oscillation period Tu (in seconds).
4. Compute: `kp = 0.6 * Ku`, `ki = 2 * kp / Tu`, `kd = kp * Tu / 8`.
5. Add the deadband to prevent integral windup when at setpoint.

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

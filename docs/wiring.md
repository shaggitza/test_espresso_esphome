# Wiring Guide — Philips Barista Brew (ESP32 Controller)

> ⚠️ **SAFETY FIRST.** Always unplug the machine and discharge any capacitors before opening it.
> The high-voltage (220 V) side must remain isolated from the low-voltage controller at all times.
> Use only opto-isolated SSRs and relay modules. When in doubt, hire an electrician.

---

## Overview

The Philips Barista Brew has a clean separation between the high-voltage (HV) board and the
low-voltage (LV) controller. You replace the LV controller board with an ESP32 while keeping
the HV board, its SSRs, and its relay for the grinder exactly as they are.

```
Machine wiring harness connector
  ┌─────────────────┐
  │  LV signals     │──► ESP32 GPIO pins (this project)
  │  3.3 V / 5 V   │──► ESP32 VIN / 3V3 power
  │  GND            │──► ESP32 GND
  └─────────────────┘
  ┌─────────────────┐
  │  HV board       │  (unchanged — SSRs, grinder relay already wired)
  │  220 V section  │
  └─────────────────┘
```

---

## Recommended Pin Assignment (ESP32 dev board, 30-pin)

| Signal | GPIO | Direction | Notes |
|---|---|---|---|
| Thermocouple CLK (SPI) | GPIO18 | OUT | Shared SPI clock |
| Thermocouple MISO (SPI) | GPIO19 | IN | Shared SPI MISO |
| Thermocouple CS (MAX6675) | GPIO5 | OUT | Chip select for thermocouple board |
| SSR control (heater) | GPIO4 | OUT | HIGH = heater ON via SSR |
| Pump relay control | GPIO25 | OUT | HIGH = pump ON |
| Grinder relay control | GPIO23 | OUT | HIGH = grinder ON |
| Brew valve solenoid | GPIO26 | OUT | HIGH = valve opens |
| Steam valve solenoid | GPIO27 | OUT | HIGH = valve opens |
| Purge/drain valve | GPIO14 | OUT | HIGH = valve opens |
| Flow meter pulse input | GPIO34 | IN | Interrupt-capable; use PULL_DOWN |
| OLED SDA (I2C) | GPIO21 | IN/OUT | Optional display |
| OLED SCL (I2C) | GPIO22 | OUT | Optional display |

> GPIO34–GPIO39 are **input only** on the ESP32 — suitable for the flow meter.
> All outputs should be buffered through 5 V relay/optocoupler modules if the LV signals
> need to switch loads above 3.3 V.

---

## Power Supply

The Philips Barista Brew LV board runs on **5 V**. The ESP32 can be powered from:
- The machine's internal 5 V rail (if current capacity allows — check with a multimeter).
- A dedicated USB power supply connected to the ESP32's USB port.

If powering from the internal rail, add a 100 µF bulk capacitor close to the ESP32 VIN pin
to absorb pump-induced noise.

---

## Flow Meter Wiring (AB32 or compatible)

```
Flow meter
  Red  ──► 5 V
  Black ──► GND
  Yellow ──► GPIO34 (plus 10 kΩ pull-down to GND)
```

---

## Thermocouple Board (MAX6675)

```
MAX6675 board
  VCC  ──► 3.3 V
  GND  ──► GND
  SCK  ──► GPIO18
  CS   ──► GPIO5
  SO   ──► GPIO19
```

The thermocouple probe tip should be mounted directly on the thermoblock. Use thermal
paste and an M6 threaded stainless thermocouple probe in the original M4 mounting hole
(requires re-tapping to M6 — see the
[pico_espresso project](https://github.com/vecinimod/pico_espresso) for details and photos).

### Thermocouple interface alternatives

All three options below use the same physical SPI pin connections; only the ESPHome
`platform:` and `cs_pin:` differ (see the YAML comments for ready-to-use code blocks).

| Interface board | Sensor type | Accuracy | Fault detection | Notes |
|---|---|---|---|---|
| **MAX6675** | K-type only | ±1.5 °C | Open-wire bit only | Most common; cheap; good enough |
| **MAX31855** | K-type only | ±2 °C | Open/short-GND/short-VCC | Better diagnostics; recommended for new builds |
| **NTC thermistor** | NTC 10 kΩ | ±1 °C (< 100 °C), ±3 °C (> 100 °C) | None | No SPI; one analog pin; limited to ~150 °C |

> For steam-capable machines (setpoints up to 140 °C), a K-type thermocouple (MAX6675 or
> MAX31855) is strongly preferred over NTC because the NTC resistance curve becomes very
> non-linear above 100 °C, reducing accuracy.

---

## SSR Wiring (Heater)

```
ESP32 GPIO4 ──► SSR DC+ (control input)
GND         ──► SSR DC-
SSR AC1     ──► in series with thermoblock 220 V supply line
SSR AC2     ──► thermoblock
```

The SSR must be rated for at least **10 A** at 250 VAC. Mount it on an aluminium heatsink
or directly on the machine chassis (if metal) with thermal paste.

### SSR type alternatives

| SSR type | Control signal | ESPHome output | Recommended |
|---|---|---|---|
| DC control, zero-crossing | 3–32 V DC | `slow_pwm` (period 1 s) | ✅ Best choice — silent, low EMI |
| DC control, random-firing | 3–32 V DC | `slow_pwm` (period 1 s) | ✅ Also fine, slightly more EMI |
| Mechanical relay (not SSR) | 5 V relay module | `slow_pwm` (period ≥ 5 s) | ⚠️ Typically rated ~100 k cycles; at 5 s period + 50% duty, expect ~140 h before end-of-life |

### `slow_pwm` period guidance

| Period | Heater duty-cycle steps | SSR stress | Notes |
|---|---|---|---|
| `0.5s` | 200 ms steps | High | Too fast for most SSRs; avoid |
| `1s` (default) | 1 % resolution over 100 s | Normal | Best for PID with thermoblock |
| `2s` | 2 % resolution | Low | Use for larger boilers or mechanical relays |
| `5s` | 5 % resolution, 200 ms min on-time | Very low | Required for mechanical relays |

> The `period` in `output.slow_pwm` controls how often the SSR is switched.
> Shorter periods give the PID finer control but stress the SSR more.
> For opto-isolated solid-state relays (SSRs), 1 s is the standard and safe choice.

---

## Relay Module Wiring (Pump, Grinder, Valves)

Each relay module has:
```
VCC ──► 5 V
GND ──► GND
IN  ──► ESP32 GPIO (e.g., GPIO25 for pump)
COM ──► one side of the 220 V load wire
NO  ──► other side (Normally Open: load is OFF when ESP32 is OFF — safest default)
```

> Always use the **NO (Normally Open)** terminal so that a power failure or ESP32 reset
> leaves all loads switched OFF.

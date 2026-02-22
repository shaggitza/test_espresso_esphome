# Troubleshooting

## ESPHome won't compile

**Error: `Component 'espresso_machine' not found`**

- Make sure your YAML has the correct `external_components:` block:
  ```yaml
  external_components:
    - source: github://shaggitza/test_espresso_esphome@main
      components: [espresso_machine]
  ```
- Check your internet connection; ESPHome fetches the component at build time.
- Try pinning to a specific release tag instead of `@main` for stability.

---

## Temperature reads as 0 or NaN

- Check the SPI wiring: CLK, MISO, and CS must be connected correctly.
- Verify the thermocouple is making good contact with the thermoblock.
- The MAX6675/MAX31855 returns a fault code if the thermocouple is disconnected — check ESPHome logs.
- Try reducing SPI clock speed in the `spi:` block if using long wires.

---

## PID temperature oscillates / won't stabilise

- Increase `kd` (derivative gain) slightly — thermoblocks respond fast, so derivative action helps.
- Add or increase the `deadband_parameters` to prevent micro-oscillation at setpoint.
- Run ESPHome's PID autotune: see `docs/pid_tuning.md`.

---

## Flow meter doesn't count pulses

- Confirm the flow meter is powered (typically 5 V) and the signal wire is connected to a
  GPIO that supports interrupts (GPIO0–GPIO33 on ESP32).
- GPIO34–GPIO39 are input-only and should work, but verify with a logic analyser or oscilloscope.
- Make sure `normally_open` / pull direction matches your flow meter's output type.

---

## Valve opens but no water flows

- Check that the pump relay is also being activated (watch the logs).
- Confirm `brew_valve` is set correctly and the 3-way solenoid direction is correct.
- Inspect the physical valve for blockage.

---

## Grinder runs during brew

- This is a safety interlock violation. Check that you have not overridden the interlock.
- If your machine requires simultaneous grind + brew, open an issue — this use case
  may need a configuration option.

---

## Wi-Fi disconnects frequently

- Add a static IP in the `wifi:` block to avoid DHCP renewal issues.
- Move the ESP32 antenna away from the machine chassis (metal shielding reduces signal).
- Check power supply quality — pump switching can cause voltage dips that reset the ESP32.

---

## Home Assistant shows entity as unavailable

- Ensure the ESP32 is on the same VLAN/subnet as Home Assistant.
- Check the `api.encryption.key` matches on both sides.
- Try restarting the ESPHome integration in HA: **Settings → Devices & Services → ESPHome → Reload**.

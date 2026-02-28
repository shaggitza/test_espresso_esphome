# Loop Time Optimisation Guide

ESPHome calls every component's `loop()` on every tick (~1–16 ms depending on
the platform and workload). Expensive work inside `loop()` directly increases
the tick-to-tick latency, which affects PID stability, sensor update rates, and
Home Assistant responsiveness.

This document catalogues the known bottlenecks, the fixes already applied, and
further optimisation opportunities.

---

## Applied Fixes

### 1. Throttled `publish_status_()` in the orchestrator (HIGH impact)

**Problem:** `EspressoMachine::loop()` called `publish_status_()` on **every
tick**. This function calls `status_name()`, which formats a `snprintf` string
with live floating-point values, allocates a temporary `std::string` on the
heap, and compares it against the last published value.

During active brewing/steaming the status string changes on nearly every tick
(volume and temperature keep changing), so the deduplication guard rarely fires
and HA receives a new text sensor update at the full loop rate (~100 Hz). Each
tick pays:

| Cost                     | Approx cycles (ESP32) |
|--------------------------|-----------------------|
| `snprintf` (3 floats)   | 60–120                |
| `std::string` heap alloc | 50–100               |
| String compare           | 10–30                |
| HA publish (when changed)| 200+                 |

**Fix:** The periodic status update in `loop()` is now throttled to **250 ms**
(`STATUS_PUBLISH_INTERVAL_MS`). State transitions (brew_start, brew_stop,
steam_start, etc.) still call `publish_status_()` directly for **immediate**
feedback — the throttle timer is reset on every explicit call to avoid stale
data after a transition.

**Result:** ~25× fewer `status_name()` calls during active operation; zero
calls during IDLE (string doesn't change so deduplication catches it).

### 2. Pre-reserved sprofiler datapoints vector (MEDIUM impact)

**Problem:** During a brew, `SprofilerShotUpload::add_datapoint()` calls
`push_back()` on a `std::vector` without pre-reserving capacity. A 30-second
shot at 10 Hz produces ~300 datapoints, causing ~9 reallocations (at vector
capacities 1, 2, 4, 8, 16, 32, 64, 128, 256, 512). Each reallocation copies
all existing elements to a new heap block.

**Fix:** `begin_shot()` now calls `datapoints_.reserve(300)` to pre-allocate
enough capacity for a typical shot. This eliminates all mid-shot reallocations.

---

## Further Optimisation Opportunities

The following items are documented for future consideration. They are ordered
roughly by expected impact on real hardware.

### 3. Cache `status_name()` result across ticks (LOW impact after fix #1)

After fix #1, `status_name()` is only called every 250 ms. A further
optimisation is to cache the formatted string and only regenerate it when the
mode, sub-state, or one of the displayed sensor values has actually changed.
This would require tracking a "dirty" flag on every state transition and on
sensor value changes exceeding a display threshold (e.g. 0.1 ml).

### 4. Use fixed-point formatting instead of `snprintf` (LOW–MEDIUM)

`snprintf` with `%f` on ESP32 is slow because of full IEEE 754 float-to-string
conversion. A hand-rolled integer-based formatter (e.g. multiply by 10, print
as two integers separated by '.') is 5–10× faster. This matters if
`status_name()` is called frequently.

### 5. Reduce mock component update rate (LOW — mock only)

`MockPump::loop()` and `MockHeater::loop()` run their physics simulation every
10 ms, computing multiple `std::exp()` calls (expensive on ESP32). For mock/
simulation builds, increasing the minimum update interval to 50 ms would reduce
CPU usage by ~5× with minimal loss of simulation fidelity.

This does NOT affect real hardware builds where these components are absent.

### 6. Batch sensor publishes (LOW)

Multiple components publish sensor values on every tick or at high frequency.
ESPHome sensor publishes involve state tracking and optional filter chains. When
many sensors update simultaneously, batching or staggering their updates across
different ticks reduces per-tick peak CPU usage.

### 7. Replace `std::exp()` with lookup table in mock components (LOW — mock only)

The mock pump and heater use `std::exp()` for physics simulation (puck wetting
model, thermal diffusion, pressure decay). A lookup table with linear
interpolation would be ~10× faster with negligible accuracy loss. Only affects
simulation builds.

### 8. Avoid `std::string` heap allocation in hot paths (MEDIUM — architectural)

`status_name()` returns `std::string` by value, causing a heap allocation on
every call (unless SSO kicks in for short strings). Refactoring to use a
pre-allocated `char[]` member buffer and returning `const char*` would
eliminate this allocation entirely. This would require changing the
`publish_status_()` interface to compare `const char*` strings instead of
`std::string`.

---

## Measuring Loop Time

To diagnose loop time issues, add a timing probe in the orchestrator's
`loop()`:

```cpp
void EspressoMachine::loop() {
  uint32_t start = micros();
  // ... existing loop body ...
  uint32_t elapsed = micros() - start;
  if (elapsed > 1000)  // flag ticks > 1 ms
    ESP_LOGW(TAG, "Slow loop: %u µs", elapsed);
}
```

For continuous monitoring, publish the loop time to a Home Assistant sensor:

```yaml
sensor:
  - platform: template
    name: "Loop Time"
    id: loop_time_sensor
    unit_of_measurement: "µs"
    update_interval: 1s
```

Target: **< 1 ms** per orchestrator tick on ESP32 during active brewing.

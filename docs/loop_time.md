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

### 3. Sprofiler upload retry backoff (HIGH impact — WiFi-dependent)

**Problem:** After a shot ends, `SprofilerShotUpload::loop()` called
`upload_pending_shot()` on **every tick** while a shot was pending. This
function:

1. Serialises the entire shot to JSON (~6 KB heap allocation)
2. Makes a **blocking** `http_request_->start()` call (TCP connect + TLS
   handshake + HTTP POST)
3. Waits for the response via `container->end()`

If the upload fails (server unreachable, weak WiFi, DNS timeout), the pending
flag stays `true` and the **very next tick** retries the blocking HTTP request.
With degraded WiFi signal, each TCP timeout can block for **5–30 seconds**,
freezing the entire ESPHome loop and stalling PID control, sensor updates, and
Home Assistant communication.

**Fix:** Upload retries now use **exponential backoff**: first retry after 5 s,
then 10 s, 20 s, 40 s, … up to 5 minutes. After 10 consecutive failures the
upload is abandoned. A new shot resets the retry state. This ensures the loop
is never blocked by repeated HTTP timeouts.

---

## Further Optimisation Opportunities

The following items are documented for future consideration. They are ordered
roughly by expected impact on real hardware.

### 4. Cache `status_name()` result across ticks (LOW impact after fix #1)

After fix #1, `status_name()` is only called every 250 ms. A further
optimisation is to cache the formatted string and only regenerate it when the
mode, sub-state, or one of the displayed sensor values has actually changed.
This would require tracking a "dirty" flag on every state transition and on
sensor value changes exceeding a display threshold (e.g. 0.1 ml).

### 5. Use fixed-point formatting instead of `snprintf` (LOW–MEDIUM)

`snprintf` with `%f` on ESP32 is slow because of full IEEE 754 float-to-string
conversion. A hand-rolled integer-based formatter (e.g. multiply by 10, print
as two integers separated by '.') is 5–10× faster. This matters if
`status_name()` is called frequently.

### 6. Reduce mock component update rate (LOW — mock only)

`MockPump::loop()` and `MockHeater::loop()` run their physics simulation every
10 ms, computing multiple `std::exp()` calls (expensive on ESP32). For mock/
simulation builds, increasing the minimum update interval to 50 ms would reduce
CPU usage by ~5× with minimal loss of simulation fidelity.

This does NOT affect real hardware builds where these components are absent.

### 7. Batch sensor publishes (LOW)

Multiple components publish sensor values on every tick or at high frequency.
ESPHome sensor publishes involve state tracking and optional filter chains. When
many sensors update simultaneously, batching or staggering their updates across
different ticks reduces per-tick peak CPU usage.

### 8. Replace `std::exp()` with lookup table in mock components (LOW — mock only)

The mock pump and heater use `std::exp()` for physics simulation (puck wetting
model, thermal diffusion, pressure decay). A lookup table with linear
interpolation would be ~10× faster with negligible accuracy loss. Only affects
simulation builds.

### 9. Avoid `std::string` heap allocation in hot paths (MEDIUM — architectural)

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

---

## WiFi Signal and Loop Time

Weak WiFi signal can amplify loop time issues because ESPHome's `sensor::Sensor::publish_state()`
and `text_sensor::TextSensor::publish_state()` enqueue data for transmission to
Home Assistant via the native API. When the TCP send buffer is full (due to
slow WiFi throughput or retransmits), these calls may block briefly.

More critically, **any blocking HTTP request** in `loop()` — such as the
sprofiler shot upload — will freeze the entire ESPHome loop for the duration of
the network operation. With weak WiFi this can be 5–30 seconds per attempt.

**Symptoms of WiFi-related loop stalls:**
- Loop time spikes correlate with physical distance from / orientation of the
  WiFi access point.
- The ESP32 WiFi RSSI sensor (if configured) shows values below −75 dBm.
- `esphome logs` shows intermittent "Component took a long time" warnings.

**Mitigations applied:**
- Sprofiler upload uses exponential retry backoff (fix #3 above), preventing
  repeated blocking HTTP requests on every loop tick.
- Status publishing is throttled (fix #1), reducing the volume of HA API
  traffic.

**Further mitigations (not yet applied):**
- Move HTTP uploads to an async task (FreeRTOS task / ESPHome defer) so the
  main loop is never blocked by network I/O.
- Reduce sensor publish frequency when WiFi RSSI drops below a threshold.
- Add an ESPHome WiFi signal strength sensor and log warnings when RSSI is
  poor.

# BrewOS Integration — Control Loop Impact Analysis

## Executive Summary

**Current implementation has a critical blocking issue during connection/reconnection
that can stall the control loop for up to 5 seconds.**

| Aspect | Status | Risk Level |
|--------|--------|------------|
| Normal operation (connected) | ✅ Non-blocking | Low |
| Message dispatch | ✅ Synchronous, fast | Low |
| Status JSON build | ✅ ~100µs, negligible | None |
| **TCP connect/reconnect** | ❌ **Blocking 5s timeout** | **CRITICAL** |
| SSL handshake | ❌ **Blocking ~2-3s** | **HIGH** |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                     ESPHome Main Loop                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐      │
│  │ PID Climate  │  │ Orchestrator │  │ BrewOSConnector      │      │
│  │   loop()     │  │   loop()     │  │   loop()             │      │
│  │  ~1-10ms     │  │  ~100µs      │  │  ~100µs (connected)  │      │
│  │              │  │              │  │  **5s (reconnecting)**│      │
│  └──────────────┘  └──────────────┘  └──────────────────────┘      │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
            All components share ONE thread (no RTOS tasks)
```

---

## Detailed Analysis

### 1. Normal Operation (WebSocket Connected)

When the WebSocket is already connected, `BrewOSConnector::loop()` executes:

```cpp
void BrewOSConnector::loop() {
  g_ws_client.loop();              // Check for incoming data, ~100µs
  connected_ = g_ws_connected_flag;
  
  if (connected_ && (now - last_status_ms_) >= STATUS_INTERVAL_MS) {
    send_raw(build_status_json()); // ~200µs every 5 seconds
  }
}
```

**Impact:** Negligible. The `WebSocketsClient::loop()` in connected state:
1. Calls `handleClientData()` — reads available bytes (non-blocking, uses `available()`)
2. Calls `handleHBPing()` — processes heartbeat if needed
3. Returns immediately if no data

**Measured time:** < 200µs per call

### 2. Message Reception (Event-Driven)

When the cloud sends a command, the WebSocket library calls `ws_event()`:

```cpp
static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
  case WStype_TEXT:
    g_connector_ptr->on_message(...);  // Dispatches synchronously
}
```

`on_message()` parses JSON and calls the orchestrator method (e.g., `machine_->brew_start()`).

**Impact:** These are simple method calls, not state machine transitions. The actual
brew/steam sequence executes in `EspressoMachine::loop()`, not here.

| Command | Action | Time |
|---------|--------|------|
| `brew_start` | Sets `brew_state_ = PREHEATING` | ~10µs |
| `steam_start` | Sets `steam_state_ = PREHEATING` | ~10µs |
| `flush` | Triggers flush sequence | ~10µs |

**Verdict:** ✅ Safe — commands are dispatched instantly.

### 3. Status Reporting

Every 5 seconds, `build_status_json()` is called:

```cpp
std::string BrewOSConnector::build_status_json() const {
  char buf[256];
  snprintf(buf, ...);  // ~50µs
  return std::string(buf);
}
```

Then `sendTXT()` queues the data for transmission (does not block for ACK).

**Impact:** ~200µs every 5 seconds — negligible.

---

## ⚠️ CRITICAL ISSUE: Blocking Reconnection

When the WebSocket is **not connected**, `g_ws_client.loop()` attempts to reconnect:

```cpp
// From WebSocketsClient.cpp:
void WebSocketsClient::loop(void) {
  if(!clientIsConnected(&_client)) {
    if((millis() - _lastConnectionFail) < _reconnectInterval) {
      return;  // ← Quick exit during cooldown (good)
    }
    // ...
    if(_client.tcp->connect(_host.c_str(), _port, WEBSOCKETS_TCP_TIMEOUT)) {
      //                                          ^^^^^^^^^^^^^^^^^^^^^^^^
      //                     THIS IS BLOCKING FOR UP TO 5 SECONDS!
      connectedCb();
    } else {
      connectFailedCb();
    }
  }
}
```

**`WEBSOCKETS_TCP_TIMEOUT` defaults to 5000ms (5 seconds).**

### What happens during a 5-second block:

| Component | Effect |
|-----------|--------|
| **PID Climate** | ❌ Temperature readings stop, heater output frozen |
| **Orchestrator** | ❌ State machine stalls (brew/steam stops advancing) |
| **Safety timers** | ❌ Watchdog may trigger if > WDT timeout |
| **Flow meter ISR** | ✅ Still runs (hardware interrupt) |
| **User inputs** | ❌ Unresponsive |

### Scenarios that trigger blocking:

1. **Boot** — First connection attempt (always blocks once)
2. **Cloud unreachable** — Every 5s reconnect interval
3. **WiFi dropout** — Single block, then cooldown
4. **SSL certificate error** — Each retry blocks

---

## Risk Assessment

### Safety Implications

| Scenario | Current Risk | Mitigation |
|----------|--------------|------------|
| Heater on, cloud unreachable | **CRITICAL** — PID frozen for 5s | Relocate WS to async task |
| Pre-infusion in progress | **HIGH** — Timing ruined | As above |
| Steam boil-off | **MEDIUM** — Temperature may overshoot | As above |
| Idle machine | LOW | N/A |

### ESP32 Watchdog

The default task watchdog timeout on ESP32 is **5 seconds**. A 5-second blocking
connect could trigger a WDT reset if other tasks are also delayed.

---

## Recommended Fixes

### Option A: Use ESPHome's AsyncTCP-based WebSocket (Preferred)

ESPHome already has an async event system. We could:

1. Use `AsyncWebSocket` from `ESPAsyncWebServer` (already used by ESPHome API)
2. Or use the async mode of WebSocketsClient (see `WEBSOCKETS_NETWORK_TYPE`)

**Pros:** No blocking, integrates with existing event loop  
**Cons:** Requires refactoring, async callbacks

### Option B: Move WebSocket to Dedicated FreeRTOS Task

```cpp
void brewos_task(void *param) {
  while (true) {
    g_ws_client.loop();  // Can block here safely
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void BrewOSConnector::setup() {
  xTaskCreate(brewos_task, "brewos", 4096, nullptr, 1, nullptr);
}
```

**Pros:** Isolates blocking from main loop  
**Cons:** Thread safety needed for `on_message()` dispatching

### Option C: Reduce Timeout + Skip While Critical

```cpp
void BrewOSConnector::loop() {
  // Skip WebSocket handling during active brew/steam
  if (machine_ != nullptr && machine_->is_busy()) {
    return;  // Do NOT call g_ws_client.loop()
  }
  g_ws_client.loop();
}
```

Also set shorter timeout:
```cpp
#define WEBSOCKETS_TCP_TIMEOUT 500  // 500ms instead of 5000ms
```

**Pros:** Quick to implement  
**Cons:** Still blocks for 500ms; cloud commands delayed during brew

### Option D: HTTP Long-Polling Fallback (SSE)

ESPHome has Server-Sent Events (SSE) support. We could:

1. Use SSE for receiving commands (non-blocking, event-driven)
2. Use HTTP POST for sending status (async with `http_request:`)

**Pros:** No external library, fully async  
**Cons:** Higher latency, more cloud infrastructure changes

---

## Immediate Action Items

1. **SHORT TERM (now):** ✅ **IMPLEMENTED**
   - Added `is_busy()` guard to skip `g_ws_client.loop()` during brew/steam
   - Added build flag `-DWEBSOCKETS_TCP_TIMEOUT=500` to reduce timeout to 500ms

2. **MEDIUM TERM (next sprint):**
   - Move WebSocket to dedicated FreeRTOS task with queue for commands

3. **LONG TERM:**
   - Evaluate ESPHome-native async WebSocket or SSE approach

---

## Testing Recommendations

| Test Case | Expected Behavior |
|-----------|-------------------|
| Cloud unreachable during idle | Machine responsive, reconnect logged |
| Cloud unreachable during brew | Brew completes normally, no stall |
| Cloud reconnects mid-steam | Steam unaffected, status resumes |
| WiFi dropout, 30s recovery | Machine operates locally, then reconnects |

---

## Appendix: Code References

- [brewos_connector.cpp](../components/espresso_machine_brewos/brewos_connector.cpp) — Main implementation
- [WebSocketsClient.cpp L238-350](https://github.com/Links2004/arduinoWebSockets/blob/master/src/WebSocketsClient.cpp#L238) — Blocking loop
- [WebSockets.h L124](https://github.com/Links2004/arduinoWebSockets/blob/master/src/WebSockets.h#L124) — Timeout default

---

*Document created: 2026-02-25*  
*Status: Analysis complete — awaiting decision on fix approach*

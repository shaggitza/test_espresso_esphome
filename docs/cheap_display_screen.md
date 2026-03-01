# Cheap 12864 Display — Menu Design & Technical Implementation

> **Status: Design only — no code written yet.**
> This document describes the complete screen layout, navigation model, and
> implementation plan for a 128×64 graphic LCD (recovered from a Creality 3D printer)
> controlled by a rotary encoder with a push-click button.  Review and approve this
> design before any C++ or YAML work begins.

---

## 1. Hardware

### 1.1 Display

| Property | Value |
|---|---|
| Controller | **ST7920** (standard on Creality Ender 3/5, CR-10 full-graphic display) |
| Resolution | 128 × 64 pixels |
| Interface | **SPI** (software or hardware) — 3 pins: CS, SID (MOSI), CLK |
| Power | 5 V (level-shifting needed when driven from ESP32 at 3.3 V) |
| ESPHome platform | `display: platform: st7920_spi` (software SPI via `u8g2`) |

The same board carries a **beeper** (optional; pin typically labeled `BEEPER`) and the
rotary encoder + button on a single connector.

### 1.2 Rotary Encoder

| Property | Value |
|---|---|
| Signals | A (CLK), B (DT), SW (push-button) |
| ESPHome platform | `sensor: platform: rotary_encoder` (A + B pins) |
| Button ESPHome | `binary_sensor` on SW pin (INPUT_PULLUP) |
| Detents | 20 or 24 per revolution (typical EC11) |
| Debounce | 10 ms hardware filter on A/B; 50 ms on SW |

### 1.3 Connector Pinout (Creality EXP1/EXP2 ribbon to ESP32)

```
EXP1 (typically)     ESP32 GPIO suggestion
  PIN 1 — BEEPER  →  GPIO (optional PWM)         any free GPIO in 0–33
  PIN 2 — ENC_SW  →  GPIO (binary_sensor)        must be in 0–33 for INPUT_PULLUP
  PIN 3 — GND     →  GND
  PIN 4 — VCC     →  5 V (via level-shifter)
  PIN 5 — ENC_A   →  GPIO (rotary_encoder A)     must be in 0–33
  PIN 6 — ENC_B   →  GPIO (rotary_encoder B)     must be in 0–33

EXP2 (typically)
  PIN 1 — CS      →  GPIO (display CS)            any free GPIO
  PIN 3 — SID     →  GPIO (display MOSI / SID)    any free GPIO
  PIN 5 — SCK     →  GPIO (display CLK)           any free GPIO
  PIN 7 — GND     →  GND
```

> ⚠️ **ESP32 GPIO constraint:** GPIO34–GPIO39 are **input-only** and do **not** support
> internal pull-up or pull-down resistors.  Encoder A, B, and SW (click) pins **must**
> use GPIOs in the range 0–33 so that `INPUT_PULLUP` mode works.  The five display-only
> pins (CS, SID, SCK) can use any free GPIO.

> ⚠️ The ST7920 logic is 5 V tolerant, but signal levels from a 3.3 V ESP32 are
> usually sufficient.  If you see display corruption, add a simple 3.3 V → 5 V
> level-shifter (74AHCT125 or similar) on the SID and CLK lines.

---

## 2. Navigation Model

### 2.1 Controls

| Action | Effect |
|---|---|
| **Rotate clockwise** | Move cursor down / increase value |
| **Rotate counter-clockwise** | Move cursor up / decrease value |
| **Short click** | Select item / confirm value |
| **Long press (≥ 1 s)** | Go back to previous screen / cancel edit |
| **Double-click** (stretch goal) | Jump to main status screen from anywhere |

### 2.2 Display Typography

At 128 × 64 with a 6×8 pixel font:
- **21 characters** wide × **8 lines** tall (normal font)
- **10 characters** wide × **4 lines** tall (large font, for real-time values)

The design uses a **mixed layout**: large font for the primary live value on the status
screen, small font everywhere else.

### 2.3 Screen Rendering Conventions

```
┌────────────────────────┐  ← line 0: title / mode bar (inverted)
│ ▶ selected item        │  ← cursor indicator '▶' on active row
│   unselected item      │
│   ...                  │
│ ──────────────────── ↕ │  ← scrollbar hint when list overflows 7 items
└────────────────────────┘
```

- The **title bar** (line 0) is drawn inverted (white-on-black).
- A **`▶` cursor** marks the focused row; it moves with rotation.
- Items that are **currently active** (e.g. brew in progress) are preceded by `●`.
- **Editable values** are shown with `[` `]` brackets when in edit mode.
- When editing a number the cursor blinks (toggled every 500 ms).

---

## 3. Screen Map

```
                     ┌──────────────────────────────┐
                     │     STATUS SCREEN (home)     │ ← boot default
                     └──────────────┬───────────────┘
                              (click)
                                    ▼
                     ┌──────────────────────────────┐
                     │         MAIN MENU            │
                     └──┬────┬────┬────┬────────────┘
                         │    │    │    │
                   Brew Steam Grnd Settings
                         │    │    │    │
             ┌───────────┘    │    │    └──────────────────────┐
             ▼                │    │                            ▼
    ┌─────────────────┐       │    │               ┌──────────────────────┐
    │   BREW MENU     │       │    │               │   SETTINGS MENU      │
    │ • Start Brew    │       │    │               │ Brew Temp            │
    │   Brew Temp     │       │    │               │ Steam Temp           │
    │   Flow Max      │       │    │               │ Flow Max             │
    │   Pre-infusion  │       │    │               │ Flow Offset          │
    │   Temp Surfing  │       │    │               │ Grind Time           │
    │   Cooldown      │       │    │               │ Pre-infusion         │
    └────────┬────────┘       │    │               │ Temp Surfing         │
             │                │    │               │ Temp Cooldown        │
     (Start Brew click)       │    │               │ Idle Timeout         │
             ▼                │    │               │ SSR Period           │
    ┌─────────────────┐       │    │               │ Maintenance ──►      │
    │  BREW PROGRESS  │       │    │               └──────────────────────┘
    │  (live screen)  │       │    │
    └─────────────────┘       │    │
                              │    │
                    ┌─────────┘    └──────────────────────┐
                    ▼                                       ▼
         ┌────────────────────┐               ┌────────────────────────┐
         │   STEAM MENU       │               │   GRINDER MENU         │
         │ • Start Steam      │               │ • Grind Now            │
         │   Steam Temp       │               │   Grind Time           │
         │   Cool Down To     │               └────────────────────────┘
         │   Purge Volume     │
         │   Flow Rate        │
         │   Timeout          │
         └─────────┬──────────┘
                   │
         (Start Steam click)
                   ▼
         ┌────────────────────┐
         │  STEAM PROGRESS    │
         │  (live screen)     │
         └────────────────────┘
```

---

## 4. Screen Definitions

### 4.1 Status Screen (Home)

**Trigger:** Boot default; returned to after any operation completes.

**Layout (128×64):**

```
┌────────────────────────┐
│ IDLE  │ T: 89.5°C      │  ← line 0: mode | current temp (large area)
│                        │
│   89.5 / 90.0 °C       │  ← lines 1–3: big live temperature display
│                        │
│ Vol: --.- ml           │  ← line 5: shot volume (or last shot value)
│ Wi-Fi: ████ (-65 dBm)  │  ← line 6: Wi-Fi strength (bars)
│ [click to open menu]   │  ← line 7: hint (dimmed)
└────────────────────────┘
```

**Mode strings** (shown top-left):

| Internal state | Display |
|---|---|
| Machine OFF | `OFF` |
| Idle | `IDLE` |
| Brew: Heating | `HEAT ☕` |
| Brewing | `BREW ☕` |
| Brew: Pre-infusion | `PRE ☕` |
| Steam: Heating | `HEAT ♨` |
| Steam: Purging | `PURGE ♨` |
| Steaming | `STEAM ♨` |
| Steam: Cooling | `COOL ♨` |
| Flushing | `FLUSH` |

**During active brew or steam** the status screen auto-updates (no need to navigate
to a progress screen); the user can still click to enter the menu while active.

---

### 4.2 Main Menu

**Trigger:** Short click from Status Screen.

```
┌────────────────────────┐
│ ■ MAIN MENU            │  ← inverted title
│▶  Brew                 │
│   Steam                │
│   Grinder              │
│   Settings             │
│   Power OFF            │
│                        │
│ [long-press = back]    │
└────────────────────────┘
```

Items:

| Item | Action |
|---|---|
| **Brew** | Enter Brew Menu |
| **Steam** | Enter Steam Menu |
| **Grinder** | Enter Grinder Menu |
| **Settings** | Enter Settings Menu |
| **Power OFF / ON** | Toggle `machine_power` switch (label flips with state) |

**Behaviour:** Rotating scrolls the cursor; clicking enters the selected item.
Long-press returns to Status Screen.

---

### 4.3 Brew Menu

**Trigger:** Select "Brew" from Main Menu.

```
┌────────────────────────┐
│ ■ BREW                 │
│▶● Start Brew           │  ← ● when already brewing
│   Stop Brew            │  ← visible only when brew is active
│   Brew Temp  90.0 °C   │
│   Flow Max   40.0 ml   │
│   Pre-infusion  ──►    │
│   Temp Surfing  ON     │
│   Cooldown      OFF    │
└────────────────────────┘
```

| Item | Editable | Range | Unit | Maps to |
|---|---|---|---|---|
| **Start Brew** | — | — | — | `espresso_machine.brew_start` |
| **Stop Brew** | — | — | — | `espresso_machine.brew_stop` (shown only while brewing) |
| **Brew Temp** | ✅ | 70 – 100 | °C | PID setpoint via `IHeater::set_target_temperature()` |
| **Flow Max** | ✅ | 10 – 100 | ml | `flow_max_number` entity |
| **Pre-infusion** | — | — | — | → Pre-infusion sub-menu |
| **Temp Surfing** | ✅ | ON / OFF | — | `temp_surf_switch` entity |
| **Cooldown** | ✅ | ON / OFF | — | `temperature_cooldown` runtime toggle |

**"Start Brew" behaviour:**
1. Confirm dialog: `Start brew? ▶ YES  NO`
2. If YES → calls `brew_start()` and transitions to Brew Progress screen.
3. If machine is OFF, show error: `Machine is OFF`.

---

### 4.3.1 Pre-infusion Sub-menu

```
┌────────────────────────┐
│ ■ PRE-INFUSION         │
│▶  Enabled    ON        │
│   Volume     5.0 ml    │
│   Hold Time  5.0 s     │
│                        │
│                        │
│                        │
│ [long-press = back]    │
└────────────────────────┘
```

| Item | Range | Unit | Description |
|---|---|---|---|
| **Enabled** | ON / OFF | — | Enable/disable pre-infusion phase |
| **Volume** | 1 – 30 | ml | Pre-infusion fill volume |
| **Hold Time** | 0 – 30 | s | Pause duration after fill, before main extraction |

---

### 4.4 Brew Progress Screen

**Trigger:** Automatically shown when brew starts.
**Returns:** Automatically on brew complete; or long-press to return without stopping.

```
┌────────────────────────┐
│ ■ BREWING              │  ← inverted; state changes to PRE-INFUSION etc.
│                        │
│    22.4 / 40.0 ml      │  ← large font: current / target volume
│                        │
│ T: 89.5 / 90.0 °C      │
│ Time: 0:23             │
│ Rate: 1.8 ml/s         │
│ [click=stop]           │
└────────────────────────┘
```

**Fields:**

| Field | Source |
|---|---|
| Volume (big) | `brew_flow.total_volume()` / `flow_max_number` |
| Temperature | `thermoblock_temp` / PID setpoint |
| Elapsed time | `last_shot_time_s` running counter |
| Flow rate | `brew_flow.rate()` (3 s average) |

**Short click** while brewing → confirm dialog `Stop brew? ▶ YES  NO`.

---

### 4.5 Steam Menu

**Trigger:** Select "Steam" from Main Menu.

```
┌────────────────────────┐
│ ■ STEAM                │
│▶● Start Steam          │
│   Stop Steam           │  ← shown only while steaming
│   Steam Temp 135.0 °C  │
│   Cool To    90.0 °C   │
│   Purge Vol  5.0 ml    │
│   Flow Rate  2.0 ml/s  │
│   Timeout    5:00      │
└────────────────────────┘
```

| Item | Editable | Range | Unit | Maps to |
|---|---|---|---|---|
| **Start Steam** | — | — | — | `espresso_machine.steam_start` |
| **Stop Steam** | — | — | — | `espresso_machine.steam_stop` |
| **Steam Temp** | ✅ | 110 – 155 | °C | Steam target temperature |
| **Cool To** | ✅ | 70 – 100 | °C | `cool_down_to` |
| **Purge Vol** | ✅ | 0 – 20 | ml | `purge_volume` |
| **Flow Rate** | ✅ | 0 – 5 | ml/s | `flow_max` (steam) |
| **Timeout** | ✅ | 0 – 15 | min | Steam safety timeout |

---

### 4.6 Steam Progress Screen

**Trigger:** Automatically shown when steam starts.

```
┌────────────────────────┐
│ ■ STEAMING ♨           │
│                        │
│     134.2 / 135.0 °C   │  ← large font: current / target temp
│                        │
│ Time: 1:42             │
│ Rate: 2.1 ml/s         │
│ Timeout: 3:18 left     │
│ [click=stop]           │
└────────────────────────┘
```

State label on title bar changes through: `HEATING ♨` → `PURGING ♨` → `STEAMING ♨`
→ `COOLING ♨` → `DONE`.

---

### 4.7 Grinder Menu

**Trigger:** Select "Grinder" from Main Menu.

```
┌────────────────────────┐
│ ■ GRINDER              │
│▶  Grind Now            │
│   Grind Time  7.0 s    │
│                        │
│                        │
│                        │
│                        │
│ [long-press = back]    │
└────────────────────────┘
```

| Item | Editable | Range | Unit | Maps to |
|---|---|---|---|---|
| **Grind Now** | — | — | — | `espresso_machine_grinder.grind` (uses current Grind Time) |
| **Grind Time** | ✅ | 0.5 – 30 | s | `grind_time_number` entity |

**"Grind Now" confirmation:** `Start grind? ▶ YES  NO`.

During an active grind a simple progress bar is shown:

```
┌────────────────────────┐
│ ■ GRINDING             │
│                        │
│ [████████░░░░░░░░░░░░] │
│   3.2 s / 7.0 s        │
│                        │
│ [click=stop]           │
│                        │
│                        │
└────────────────────────┘
```

---

### 4.8 Settings Menu

**Trigger:** Select "Settings" from Main Menu.

```
┌────────────────────────┐
│ ■ SETTINGS             │
│▶  Brew Temp   90.0 °C  │
│   Steam Temp 135.0 °C  │
│   Flow Max    40.0 ml  │
│   Flow Offset 20.0 ml  │
│   Grind Time   7.0 s   │
│   Pre-infusion   ──►   │
│ ↓ more...              │
└────────────────────────┘
```

Second page (scroll down):

```
┌────────────────────────┐
│ ■ SETTINGS (2/2)       │
│▶  Temp Surfing   ON    │
│   Temp Cooldown  OFF   │
│   Idle Timeout  30 min │
│   SSR Period  1000 ms  │
│   Maintenance    ──►   │
│                        │
│                        │
└────────────────────────┘
```

Full item table:

| Item | Editable | Range | Unit | Maps to |
|---|---|---|---|---|
| **Brew Temp** | ✅ | 70 – 100 | °C | PID setpoint |
| **Steam Temp** | ✅ | 110 – 155 | °C | Steam target temp |
| **Flow Max** | ✅ | 10 – 100 | ml | `flow_max_number` |
| **Flow Offset** | ✅ | 0 – 40 | ml | Puck absorption offset |
| **Grind Time** | ✅ | 0.5 – 30 | s | `grind_time_number` |
| **Pre-infusion** | — | — | — | → Pre-infusion sub-menu (same as in Brew Menu) |
| **Temp Surfing** | ✅ | ON / OFF | — | `temp_surf_switch` |
| **Temp Cooldown** | ✅ | ON / OFF | — | Pre-brew cooldown flag |
| **Idle Timeout** | ✅ | 0 – 120 | min | Idle auto-off duration |
| **SSR Period** | ✅ | 500 – 5000 | ms | `ssr_period_number` |
| **Maintenance** | — | — | — | → Maintenance sub-menu |

---

### 4.9 Maintenance Sub-menu

**Trigger:** Select "Maintenance" from Settings Menu.

```
┌────────────────────────┐
│ ■ MAINTENANCE          │
│▶  Flush 50 ml          │
│   Flush Volume 50.0 ml │
│   Restart ESP          │
│   PID Autotune         │
│                        │
│                        │
│ [long-press = back]    │
└────────────────────────┘
```

| Item | Action |
|---|---|
| **Flush N ml** | Confirm → `espresso_machine.flush` with configured volume |
| **Flush Volume** | Editable 10 – 200 ml |
| **Restart ESP** | Confirm → `button.restart` |
| **PID Autotune** | Confirm → `climate.pid.autotune: main_heater` |

---

### 4.10 Number-Edit Mode

When the user clicks on any editable numeric item, the screen enters **edit mode**:

```
┌────────────────────────┐
│ ■ BREW TEMP            │  ← item name in inverted title
│                        │
│     [ 90.0 ] °C        │  ← value in brackets, blinking
│                        │
│  ◄ rotate to change    │
│  ● click to confirm    │
│  ✕ long-press = cancel │
│                        │
└────────────────────────┘
```

- **Rotate**: increment/decrement by the step size appropriate to the parameter
  (0.5 °C for temperature; 0.5 ml for volumes; 100 ms for SSR period; 30 s for timeout).
- **Click**: confirm the new value, apply it via the entity's API, and return to the menu.
- **Long-press**: cancel edit, revert to original value, and return to the menu.

**Step sizes:**

| Parameter type | Step | Hold-rotate acceleration |
|---|---|---|
| Temperature (°C) | 0.5 °C | 2 °C after 1 s of rotation |
| Volume (ml) | 0.5 ml | 2 ml after 1 s |
| Time (s) | 0.5 s | 2 s after 1 s |
| Time (min) | 1 min | 5 min after 1 s |
| SSR period (ms) | 100 ms | 500 ms after 1 s |
| Flow rate (ml/s) | 0.1 ml/s | 0.5 ml/s after 1 s |

**Hold-rotate acceleration:** if the encoder is turned continuously for > 1 s without
stopping, the step size multiplies to make large-range adjustments quicker.

---

### 4.11 ON/OFF Toggle Mode

For boolean items (Temp Surfing, Cooldown):
- Click toggles immediately without a separate edit screen.
- A brief `✓ ON` / `✓ OFF` confirmation overlay appears for 800 ms before returning to the
  menu.

---

### 4.12 Confirmation Dialog

Used before destructive or irreversible actions (Start Brew, Start Steam, Grind Now,
Flush, Restart, PID Autotune):

```
┌────────────────────────┐
│                        │
│  Start brew?           │
│                        │
│  ▶ YES        NO       │
│                        │
│                        │
│                        │
│ [long-press = cancel]  │
└────────────────────────┘
```

- Rotate moves cursor between YES and NO.
- Click confirms the selection.
- Long-press (or selecting NO) cancels and returns.

---

### 4.13 Error Overlay

When an action is rejected by the orchestrator (e.g. brew_start while machine is off,
or steam_start while brewing), a non-blocking error overlay shows for 2 s:

```
┌────────────────────────┐
│                        │
│  ⚠ Cannot start brew   │
│  Machine is OFF        │
│                        │
│  (returns in 2 s)      │
│                        │
│                        │
└────────────────────────┘
```

---

## 5. Screen Flow Summary

```
Boot → Status Screen
         ↕ (click)
       Main Menu
       ├── Brew Menu
       │     ├── Start Brew → Brew Progress
       │     ├── (Stop Brew)
       │     ├── Brew Temp [edit]
       │     ├── Flow Max [edit]
       │     ├── Pre-infusion sub-menu
       │     │     ├── Enabled [toggle]
       │     │     ├── Volume [edit]
       │     │     └── Hold Time [edit]
       │     ├── Temp Surfing [toggle]
       │     └── Cooldown [toggle]
       ├── Steam Menu
       │     ├── Start Steam → Steam Progress
       │     ├── (Stop Steam)
       │     ├── Steam Temp [edit]
       │     ├── Cool Down To [edit]
       │     ├── Purge Volume [edit]
       │     ├── Flow Rate [edit]
       │     └── Timeout [edit]
       ├── Grinder Menu
       │     ├── Grind Now → Grind Progress
       │     └── Grind Time [edit]
       ├── Settings Menu
       │     ├── Brew Temp [edit]
       │     ├── Steam Temp [edit]
       │     ├── Flow Max [edit]
       │     ├── Flow Offset [edit]
       │     ├── Grind Time [edit]
       │     ├── Pre-infusion sub-menu (shared)
       │     ├── Temp Surfing [toggle]
       │     ├── Temp Cooldown [toggle]
       │     ├── Idle Timeout [edit]
       │     ├── SSR Period [edit]
       │     └── Maintenance sub-menu
       │           ├── Flush N ml [confirm → run]
       │           ├── Flush Volume [edit]
       │           ├── Restart ESP [confirm]
       │           └── PID Autotune [confirm]
       └── Power OFF/ON [toggle, no confirm]
```

---

## 6. Technical Implementation Plan

> This section describes **how** the UI would be implemented — still no code.

### 6.1 ESPHome Component Approach

The display and input handling are standard ESPHome entities defined in the device YAML.
The menu logic itself is a new **optional** ESPHome component `espresso_machine_display`
that:

1. Registers a `display:` lambda against the ST7920 display handle.
2. Subscribes to `rotary_encoder` sensor updates and `binary_sensor` click/long-press
   events.
3. Calls existing entity APIs (`espresso_machine.brew_start()`, number entity `set()`,
   etc.) — no new C++ in the orchestrator.
4. Has no mandatory wiring — removing the `espresso_machine_display:` block from the
   YAML simply removes the display; nothing else changes.

### 6.2 New Component: `espresso_machine_display`

**Location:** `components/espresso_machine_display/`

**Files:**

| File | Purpose |
|---|---|
| `__init__.py` | ESPHome schema: ties the component to the display, rotary encoder, click sensor, and espresso_machine orchestrator id |
| `espresso_machine_display.h` | `EspressoDisplay` class inheriting `esphome::Component` |
| `espresso_machine_display.cpp` | `setup()`, `loop()`, menu state machine, render functions |

**Schema sketch (YAML):**

```yaml
espresso_machine_display:
  id: my_display_ui
  display_id: lcd              # id: of the display: entity
  encoder_id: rotary_enc       # id: of the rotary_encoder sensor
  button_id: enc_button        # id: of the binary_sensor for click
  espresso_machine_id: my_espresso  # orchestrator
  long_press_ms: 1000          # ms threshold for long-press (default 1000)
```

### 6.3 Menu State Machine (C++ sketch)

```
enum class Screen {
  STATUS,
  MAIN_MENU,
  BREW_MENU,
  BREW_PROGRESS,
  PRE_INFUSION,
  STEAM_MENU,
  STEAM_PROGRESS,
  GRINDER_MENU,
  GRIND_PROGRESS,
  SETTINGS_MENU,
  MAINTENANCE_MENU,
  EDIT_NUMBER,
  EDIT_TOGGLE,
  CONFIRM_DIALOG,
  ERROR_OVERLAY,
};
```

The `loop()` method:
1. Reads encoder delta since last tick.
2. Reads button state (short click / long press from a timer in `on_press`/`on_release`).
3. Dispatches to the active screen's `handle_input()` function.
4. Calls `render()` on the display handle if state changed or periodic refresh needed.

### 6.4 Display Rendering

Each screen has a dedicated `render_<screen>(display_ref)` function.
The display is redrawn only when:
- Input events change state, or
- A live-data screen (Status, Brew Progress, Steam Progress) needs periodic refresh
  (suggested: every 250 ms to avoid display bus saturation).

### 6.5 Live Data Subscriptions

The component stores pointers to the relevant sensor/entity objects (passed by
`to_code()` in `__init__.py`) and reads values directly:

```cpp
float temp  = heater_ctrl_->get_current_temperature();
float sp    = heater_ctrl_->get_target_temperature();
float vol   = flow_meter_->total_volume();
float rate  = flow_meter_->average_rate();
```

No polling lambdas needed — all values are already in memory.

### 6.6 Encoder + Button ESPHome YAML Fragments

```yaml
sensor:
  - platform: rotary_encoder
    id: rotary_enc
    pin_a: GPIO32     # ← CHANGE ME (ENC_A) — must be in GPIO0–33 for pull-up support
    pin_b: GPIO33     # ← CHANGE ME (ENC_B) — must be in GPIO0–33 for pull-up support
    resolution: 1     # 1 step per detent

binary_sensor:
  - platform: gpio
    id: enc_button
    pin:
      number: GPIO16  # ← CHANGE ME (ENC_SW) — MUST be GPIO0–33 for INPUT_PULLUP
      mode: INPUT_PULLUP   # GPIO34–39 do NOT support pull-ups; avoid them here
      inverted: true
    filters:
      - delayed_on: 10ms

display:
  - platform: st7920_spi
    id: lcd
    cs_pin: GPIO15    # ← CHANGE ME (EXP2 CS)
    sid_pin: GPIO13   # ← CHANGE ME (EXP2 SID / MOSI)
    clk_pin: GPIO17   # ← CHANGE ME (EXP2 SCK) — do NOT use GPIO14 (purge valve conflict)
    pages:
      - id: page_main
        lambda: |-
          // Handled entirely by espresso_machine_display component
          id(my_display_ui).render(it);
```

> Note: The `st7920_spi` platform uses software SPI.  The pins above are suggestions
> only and must not conflict with the flow meter (GPIO34–39) or thermocouple SPI.

### 6.7 Font Requirements

Two fonts are needed:
- **Small (6×8)**: for menus, labels, hints — built-in ESPHome `8x8` or a GFont
  equivalent included as a local `.ttf`.
- **Large (10×16 or 12×24)**: for live numeric values on progress/status screens.

To avoid CI failures from blocked Google Fonts downloads, font files should be committed
to the repository under `components/espresso_machine_display/fonts/`:

```
fonts/
  DejaVuSansMono-8.bdf   (small)
  DejaVuSansMono-16.bdf  (large)
```

### 6.8 Safety Constraints

- Display rendering **must not block** — all render calls complete in one `loop()` tick.
- No menu action bypasses the orchestrator's own safety checks; the display simply calls
  the same public API as HA automations.
- If the machine enters a safety cutoff state the status screen immediately shows:

  ```
  ┌────────────────────────┐
  │ ■ ⚠ OVER-TEMP CUTOFF   │  ← inverted + flashing
  │                        │
  │   xxx.x °C  LIMIT 165  │
  │   Heater forced OFF    │
  │                        │
  │   Power cycle to reset │
  │                        │
  └────────────────────────┘
  ```

### 6.9 Scope of Implementation (Phase 10)

Phase 10 in `PLAN.md` is already reserved for "Display & UI (Optional)".  This component
fills that slot.  Implementation will proceed in two sub-phases:

| Sub-phase | Deliverable |
|---|---|
| **10a** | Schema + `__init__.py`, ST7920 display in example YAML, status screen only (no menu) |
| **10b** | Full menu navigator: all screens, edit mode, confirmation dialogs |

Unit testing will cover the menu state machine in isolation (no display hardware needed)
using a mock display object in the GoogleTest suite under `tests/cpp/`.

---

## 7. Open Questions (for approval)

1. **Beeper feedback**: Should audible clicks (short beep on navigation, long beep on
   action) be included?  Requires one PWM GPIO.
2. **Sleep / screensaver**: Should the display dim or blank after N minutes of no
   encoder activity?  If yes, any key/rotation should wake it.
3. **Value persistence**: Temperature and flow-max edits via the encoder should take
   effect immediately (live entity update).  Should they also persist across reboots?
   (Currently the entities use ESPHome `restore_value: true` — this carries over
   automatically if kept.)
4. **Encoder resolution**: Some EC11 clones emit 2 or 4 pulses per detent.  The YAML
   `resolution:` parameter may need tuning per hardware batch.
5. **Double-click**: Is a double-click gesture (return to home from anywhere) desirable,
   or is long-press-to-back sufficient?
6. **`espresso_machine_display` component name**: Should this live in the same external
   component source as the rest of the project, or be a standalone optional add-on?

---

*Approve this document to proceed to implementation (Phase 10a).*

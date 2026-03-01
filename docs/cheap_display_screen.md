# Cheap 12864 Display — Menu Design & Technical Implementation

> **Status: Design only — no code written yet.**
> This document describes the complete screen layout, navigation model, and
> implementation plan for a 128×64 graphic LCD (recovered from a Creality 3D printer)
> controlled by a rotary encoder with a push-click button.  Review and approve this
> design before any C++ or YAML work begins.
>
> **Updated design goals (v2):**
> - The home screen is an **action hub** — Brew, Steam, Grind, and Power are reachable
>   with a single click, without entering any sub-menu.
> - Each ESPHome component publishes its own display actions; the display component
>   collects them automatically when components are referenced by `id:`.
> - A **theme** system lets the user pick a visual style in one YAML line.
> - The YAML API is designed to be as concise as possible:
>   `espresso_machine: my_espresso` + `theme: classic` is sufficient for a working UI.

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
 ┌──────────────────────────────────────────────────────────────────┐
 │             HOME SCREEN  (boot default — action hub)             │
 │                                                                  │
 │  Top bar:  live temperature + mode name                          │
 │  Body:     rotary-navigable quick-action list                    │
 │            ▶ Brew  /  Steam  /  Grind  /  Settings  /  Power    │
 │  All primary actions reachable with ONE click here               │
 └──────┬────────────┬─────────────┬────────────┬───────────────────┘
 (Brew) │            │ (Steam)     │ (Grind)    │ (Settings)
        ▼            ▼             ▼            ▼
 ┌────────────┐ ┌────────────┐ ┌──────────┐ ┌──────────────────────┐
 │ BREW MENU  │ │ STEAM MENU │ │ GRINDER  │ │   SETTINGS MENU      │
 │ Confirm +  │ │ Confirm +  │ │   MENU   │ │ All parameters in    │
 │ params     │ │ params     │ │ (params) │ │ one scrollable list  │
 └─────┬──────┘ └─────┬──────┘ └────┬─────┘ │ Maintenance ──►      │
       │              │              │       └──────────────────────┘
       ▼              ▼              ▼
 ┌────────────┐ ┌────────────┐ ┌──────────┐
 │    BREW    │ │   STEAM    │ │  GRIND   │
 │  PROGRESS  │ │  PROGRESS  │ │ PROGRESS │
 │ (live)     │ │ (live)     │ │ (bar)    │
 └────────────┘ └────────────┘ └──────────┘
```

> **Key principle:** every primary action (Brew, Steam, Grind, Power toggle) is
> reachable from the Home Screen by rotating to the action and clicking once.
> Sub-menus are for parameter tuning, not for starting operations.
> There is no separate "Main Menu" screen — the Home Screen *is* the main menu.

---

## 4. Screen Definitions

### 4.1 Home Screen (Action Hub)

**Trigger:** Boot default; returned to after any operation completes.

The Home Screen has two modes: **idle** and **active**.  In idle mode it doubles as the
main command centre — all primary actions are reachable directly without entering a
sub-menu.  In active mode it automatically switches to a live progress view for the
running operation.

---

#### 4.1a — Idle Mode (action hub)

```
┌────────────────────────┐
│ IDLE       89.5/90.0°C │  ← line 0: mode name + current/target temp (inverted)
│ Vol:  --.- ml          │  ← line 1: last shot volume (or "--" if none)
│▶☕ Brew                │  ← line 2: quick action #1 (cursor here on boot)
│ ♨ Steam               │  ← line 3: quick action #2
│ ⚙ Grind               │  ← line 4: quick action #3
│ ≡ Settings             │  ← line 5: opens Settings Menu
│ ──────────────────     │  ← line 6: separator
│ ⏻ Power OFF            │  ← line 7: machine power toggle
└────────────────────────┘
```

**Interaction:**
- Rotate moves the cursor (▶) between lines 2–7.
- Short click on **Brew** → Brew Menu (with confirm + params).
- Short click on **Steam** → Steam Menu (with confirm + params).
- Short click on **Grind** → Grinder Menu (with confirm + params).
- Short click on **Settings** → Settings Menu.
- Short click on **Power OFF/ON** → toggles `machine_power` immediately (no confirm).
- Long press from anywhere on the Home Screen → no-op (already home; optional beep).

> No separate "Main Menu" screen exists.  The Home Screen *is* the main menu.

---

#### 4.1b — Active: Brew in Progress

When a brew (or pre-infusion or heating) is active, the Home Screen body changes to
show live brew data.  Rotating and clicking still work.

```
┌────────────────────────┐
│ BREWING    89.5/90.0°C │  ← inverted; mode label updates through all brew states
│ 22.4 ml / 40.0 ml      │  ← current volume / target volume (large)
│ Time: 0:23             │  ← elapsed brew time
│ Rate: 1.8 ml/s         │  ← 3 s average flow rate
│ ──────────────────     │
│▶■ Stop Brew            │  ← action list shrinks to relevant items only
│ ≡ Settings             │
│                        │
└────────────────────────┘
```

State labels cycle through: `HEAT ☕` → `PRE ☕` → `BREWING` → `CLEANUP ☕`.

---

#### 4.1c — Active: Steam in Progress

```
┌────────────────────────┐
│ STEAMING   134.2/135°C │
│ Time: 1:42             │
│ Rate: 2.1 ml/s         │
│ Timeout: 3:18 left     │
│ ──────────────────     │
│▶■ Stop Steam           │
│ ≡ Settings             │
│                        │
└────────────────────────┘
```

State labels cycle: `HEAT ♨` → `PURGE ♨` → `STEAMING ♨` → `COOL ♨` → `DONE`.

---

#### 4.1d — Active: Grind in Progress

```
┌────────────────────────┐
│ GRINDING               │
│ [████████░░░░░░░░░░░░] │  ← progress bar
│   3.2 s / 7.0 s        │
│                        │
│ ──────────────────     │
│▶■ Stop Grind           │
│                        │
│                        │
└────────────────────────┘
```

---

**Mode strings** (shown in title bar top-left):

| Internal state | Display |
|---|---|
| Machine OFF | `OFF` |
| Idle | `IDLE` |
| Brew: Heating | `HEAT ☕` |
| Brew: Pre-infusion | `PRE ☕` |
| Brewing | `BREWING` |
| Brew: Cleanup | `CLEANUP ☕` |
| Steam: Heating | `HEAT ♨` |
| Steam: Purging | `PURGE ♨` |
| Steaming | `STEAMING ♨` |
| Steam: Cooling | `COOL ♨` |
| Flushing | `FLUSH` |
| Grinding | `GRINDING ⚙` |

> The temperature suffix (`89.5/90.0°C`) is appended by the render function, not part of
> the mode string itself.  Mode strings are short fixed labels; the render function adds
> live sensor data alongside them in the title bar.


---

### 4.2 Brew Menu

> **Note:** There is no separate "Main Menu" screen.  The Home Screen (§ 4.1) acts as
> the main menu.  Brew, Steam, Grind, Settings, and Power are all reachable with a
> single click from the Home Screen.  The Brew Menu below is the parameter/confirm
> screen reached by clicking "Brew" on the Home Screen.

**Trigger:** Click "Brew" on the Home Screen.

```
┌────────────────────────┐
│ ■ BREW                 │  ← inverted title
│▶  ☕ Start Brew        │  ← primary action at top
│   Brew Temp  90.0 °C   │
│   Flow Max   40.0 ml   │
│   Pre-infusion  ──►    │
│   Temp Surfing  ON     │
│   Cooldown      OFF    │
│ [long-press = home]    │
└────────────────────────┘
```

| Item | Editable | Range | Unit | Maps to |
|---|---|---|---|---|
| **Start Brew** | — | — | — | `espresso_machine.brew_start` (confirm dialog) |
| **Brew Temp** | ✅ | 70 – 100 | °C | PID setpoint via `IHeater::set_target_temperature()` |
| **Flow Max** | ✅ | 10 – 100 | ml | `flow_max_number` entity |
| **Pre-infusion** | — | — | — | → Pre-infusion sub-menu |
| **Temp Surfing** | ✅ | ON / OFF | — | `temp_surf_switch` entity |
| **Cooldown** | ✅ | ON / OFF | — | `temperature_cooldown` runtime toggle |

**"Start Brew" behaviour:**
1. Confirm dialog: `Start brew? ▶ YES  NO`
2. If YES → calls `brew_start()` and Home Screen switches to Brew-Active mode (§ 4.1b).
3. If machine is OFF, show error: `Machine is OFF`.

Long-press returns to Home Screen.


---

### 4.3 Pre-infusion Sub-menu

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

### 4.4 Brew Progress (inline on Home Screen)

> Brew progress is displayed **in-place on the Home Screen** (§ 4.1b) — there is no
> separate navigation step.  When a brew starts, the Home Screen body instantly
> switches to the progress layout below; when the brew finishes it returns to idle layout.

**Layout:** see § 4.1b for the full wireframe.

**Data fields:**

| Field | Source |
|---|---|
| Volume | `brew_flow.total_volume()` / `flow_max_number` |
| Temperature | `thermoblock_temp` / PID setpoint |
| Elapsed time | Running counter since `brew_start()` |
| Flow rate | `brew_flow.rate()` (3 s rolling average) |

**Short click** while brewing → confirm dialog `Stop brew? ▶ YES  NO`.
Long-press → return to Home Screen idle layout **without stopping** the brew.

---

### 4.5 Steam Menu

**Trigger:** Click "Steam" on the Home Screen.

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

### 4.6 Steam Progress (inline on Home Screen)

> Steam progress is displayed **in-place on the Home Screen** (§ 4.1c) — no separate
> navigation required.  The Home Screen body switches to steam progress on `steam_start()`
> and reverts to idle on completion or stop.

**Layout:** see § 4.1c for the full wireframe.

State label on title bar changes through: `HEAT ♨` → `PURGE ♨` → `STEAMING ♨`
→ `COOL ♨` → `DONE`.

**Short click** while steaming → confirm dialog `Stop steam? ▶ YES  NO`.
Long-press → return to Home Screen idle layout **without stopping** the steam.

---

### 4.7 Grinder Menu

**Trigger:** Click "Grind" on the Home Screen.

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

During an active grind, the Home Screen body (§ 4.1d) shows a progress bar
with elapsed/total time and a "Stop Grind" action.  No separate navigation is needed.

---

### 4.8 Settings Menu

**Trigger:** Click "Settings" on the Home Screen.

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

**Trigger:** Select "Maintenance ──►" from the Settings Menu.

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
Boot → Home Screen  (action hub — idle mode)
       ├── ☕ Brew  ──────────► Brew Menu (§ 4.2)
       │                         ├── Start Brew [confirm] → Home Screen brew-active (§ 4.1b)
       │                         ├── Brew Temp [edit]
       │                         ├── Flow Max [edit]
       │                         ├── Pre-infusion sub-menu (§ 4.3)
       │                         │     ├── Enabled [toggle]
       │                         │     ├── Volume [edit]
       │                         │     └── Hold Time [edit]
       │                         ├── Temp Surfing [toggle]
       │                         └── Cooldown [toggle]
       │
       ├── ♨ Steam ──────────► Steam Menu (§ 4.5)
       │                         ├── Start Steam [confirm] → Home Screen steam-active (§ 4.1c)
       │                         ├── Steam Temp [edit]
       │                         ├── Cool Down To [edit]
       │                         ├── Purge Volume [edit]
       │                         ├── Flow Rate [edit]
       │                         └── Timeout [edit]
       │
       ├── ⚙ Grind ──────────► Grinder Menu (§ 4.7)
       │                         ├── Grind Now [confirm] → Home Screen grind-active (§ 4.1d)
       │                         └── Grind Time [edit]
       │
       ├── ≡ Settings ────────► Settings Menu (§ 4.8)
       │                         ├── Brew Temp [edit]
       │                         ├── Steam Temp [edit]
       │                         ├── Flow Max [edit]
       │                         ├── Flow Offset [edit]
       │                         ├── Grind Time [edit]
       │                         ├── Pre-infusion sub-menu (shared, § 4.3)
       │                         ├── Temp Surfing [toggle]
       │                         ├── Temp Cooldown [toggle]
       │                         ├── Idle Timeout [edit]
       │                         ├── SSR Period [edit]
       │                         └── Maintenance sub-menu (§ 4.9)
       │                               ├── Flush N ml [confirm → run]
       │                               ├── Flush Volume [edit]
       │                               ├── Restart ESP [confirm]
       │                               └── PID Autotune [confirm]
       │
       └── ⏻ Power OFF/ON ──── [toggle immediately, no confirm]
```

> **Legend:**  `[edit]` = number-edit mode (§ 4.10);  `[toggle]` = ON/OFF mode (§ 4.11);
> `[confirm]` = confirmation dialog (§ 4.12).  Long-press from any sub-menu returns to
> the Home Screen.

---

## 6. YAML API Design

> This section describes the **declarative YAML configuration** for the display component.
> No code is written yet — this is the interface contract that the implementation must satisfy.

---

### 6.1 Design Goals

The YAML API must satisfy these goals in order of priority:

1. **Explicit, conflict-safe wiring** — every GPIO and every entity reference must be
   declared by the user in YAML.  No auto-discovery.  The ESP32 in this project already
   has many GPIO assignments (valves, pump, flow meter, thermocouple SPI) that vary per
   build; silent auto-wiring would risk pin conflicts that are hard to debug.
2. **Component-aware** — if an optional component (grinder, heater adapter, flow meter)
   is referenced, its actions and data automatically appear in the UI; if omitted, the
   UI adapts silently.  Component bindings are explicit `id:` references, not scanned.
3. **Themeable** — visual style is a one-word choice, not a custom layout lambda.

---

### 6.2 Minimal Configuration (recommended starting point)

```yaml
# ── Hardware (native ESPHome entities — declare once, referenced below by id:) ──────────────
sensor:
  - platform: rotary_encoder
    id: rotary_enc
    pin_a: GPIO32          # ENC_A — must be GPIO0–33
    pin_b: GPIO33          # ENC_B — must be GPIO0–33
    resolution: 1          # 1 step per detent; set to 2 or 4 for different EC11 batches

binary_sensor:
  - platform: gpio
    id: enc_button
    pin:
      number: GPIO16       # ENC_SW — must be GPIO0–33 for INPUT_PULLUP
      mode: INPUT_PULLUP   # GPIO34–39 do NOT support pull-ups; avoid them here
      inverted: true
    filters:
      - delayed_on: 10ms

display:
  - platform: st7920_spi
    id: lcd
    cs_pin:  GPIO15        # EXP2 CS
    sid_pin: GPIO13        # EXP2 SID/MOSI
    clk_pin: GPIO17        # EXP2 SCK — do NOT use GPIO14 (purge valve, see examples/philips_barista_brew.yaml)
    lambda: |-
      id(my_display_ui).render(it);

# ── Espresso Machine Display — the single config block that wires everything ─────────────────
espresso_machine_display:
  id: my_display_ui
  # Required hardware bindings — all three must be declared explicitly (no auto-discovery)
  display_id:  lcd             # ← id: of the display: entity above
  encoder_id:  rotary_enc      # ← id: of the sensor: rotary_encoder entity above
  button_id:   enc_button      # ← id: of the binary_sensor: gpio entity above
  # Machine orchestrator (required)
  espresso_machine: my_espresso    # ← id: of the espresso_machine orchestrator
  # Visual style (required)
  theme: classic                   # ← see § 6.4 for available themes
```

**All hardware bindings must be declared explicitly.**  There is no auto-discovery of
display, encoder, or button entities.  This is intentional: the ESP32 in this project
has many GPIO assignments that vary per build (valves on GPIO14/26/27, pump on GPIO25,
flow meter on GPIO34, thermocouple SPI on other pins).  Silent auto-wiring would risk
binding to the wrong entity and causing pin conflicts that are difficult to debug.
See § 6.3 for the full reference schema including optional component bindings.

---

### 6.3 Full Configuration Reference

```yaml
espresso_machine_display:
  id: my_display_ui           # (optional) C++ variable name; needed if referenced in lambdas

  # ── Hardware bindings (ALL THREE REQUIRED — no auto-discovery) ────────────────────────────
  # Explicit id: references prevent pin conflicts. The ESP32 has many GPIO assignments
  # across components; silent auto-wiring could bind to the wrong entity silently.
  display_id: lcd             # (required) id: of the display: entity
  encoder_id: rotary_enc      # (required) id: of the sensor: rotary_encoder entity
  button_id:  enc_button      # (required) id: of the binary_sensor: gpio entity (click button)

  # ── Machine component bindings (each is optional) ──────────────────────────────────────────
  # The component queries each bound sub-component for its published actions and live data.
  # If a binding is omitted, its actions and data fields are silently absent from the UI.
  espresso_machine: my_espresso   # (required) orchestrator → Brew, Steam, Flush actions
  grinder: main_grinder           # (optional) → Grind action on Home Screen; Grind Time edit in Grinder Menu
  heater: heater_ctrl             # (optional) → Brew/Steam Temp edit; SSR Period edit
  flow_meter: brew_flow           # (optional) → live flow rate + volume on Home Screen

  # ── Theme ─────────────────────────────────────────────────────────────────────────────────
  # Visual style applied to all screens.  See § 6.4 for theme details.
  theme: classic           # classic | minimal | barista | dark

  # ── Behaviour ─────────────────────────────────────────────────────────────────────────────
  long_press_ms: 1000          # ms hold before a press is treated as long-press (default 1000)
  screensaver_timeout: 5min    # blank display after N minutes idle; any input wakes it (0 = off)
  beeper_pin: GPIO2            # (optional) PWM GPIO for audible navigation clicks and alerts

  # ── Home Screen quick-action order ────────────────────────────────────────────────────────
  # Controls which actions appear on the Home Screen and in what order.
  # Omit to use the theme default.  Actions not listed are hidden from the Home Screen
  # (still accessible via their sub-menu if the sub-menu itself is reachable from Settings).
  home_actions:
    - brew          # Start Brew (requires espresso_machine:)
    - steam         # Start Steam (requires espresso_machine:)
    - grind         # Grind Now (requires grinder:)
    - settings      # Open Settings Menu
    - power         # Toggle machine power
    # - flush       # Quick flush (optional; confirm dialog before running)
```

---

### 6.4 Theme System

A **theme** is a named bundle of visual and layout choices applied uniformly to all
screens.  Themes are compile-time constants selected by the `theme:` key.

#### Built-in themes

| Theme | Description |
|---|---|
| `classic` | Matches the original Creality 12864 UI style: inverted title bar, `▶` cursor, horizontal separator lines, mixed small/large fonts |
| `minimal` | No borders or separators; plain monospace text only; maximises content density |
| `barista` | Temperature value dominates the Home Screen (large font, centre-stage); compact action strip at the bottom; navigation model TBD (see Open Question #7) |
| `dark` | Full white-on-black throughout (inverted entire framebuffer); high contrast in bright kitchens |

#### What a theme controls

| Attribute | `classic` | `minimal` | `barista` | `dark` |
|---|---|---|---|---|
| Title bar | Inverted | Bold text only | Inverted | Inverted |
| Cursor glyph | `▶` | `>` | `●` | `▶` |
| Separator lines | Yes | No | No | Yes |
| Home Screen layout | List (§ 4.1) | List (§ 4.1) | Hero temp + strip | Inverted list |
| Primary font | 6×8 (small) | 6×8 (small) | 6×8 body / 12×16 hero | 6×8 (small) |
| Active item marker | `●` | `*` | filled block | `●` |

> **Note:** The `barista` theme has a unique Home Screen layout where the live
> temperature fills the top 3/4 of the display in large font, and the quick-action
> strip (`BREW  STEAM  GRIND`) is a compact single line at the bottom.  This gives
> the machine a "professional appliance" look at a glance.

---

### 6.5 Component Action Publishing

Each bound component contributes a set of **display actions** — named, labelled
operations the display component can invoke.  This is a compile-time wiring done
in `__init__.py` (Python schema) rather than a C++ registration bus:

| Bound component | Published actions | Published live data |
|---|---|---|
| `espresso_machine:` | Brew Start, Brew Stop, Steam Start, Steam Stop, Flush | Mode string, brew volume, brew time, steam time, steam timeout |
| `grinder:` | Grind Now, Grind Stop | Grind elapsed time |
| `heater:` | — (data only) | Current temperature, target temperature |
| `flow_meter:` | Reset Flow | Current flow rate, total volume |

**How it works in practice:**

- When `grinder: main_grinder` is present in `espresso_machine_display:`, the Python
  `to_code()` function calls `cg.add(display.set_grinder(grinder_var))` so the C++
  class has a pointer to the grinder.  The "Grind" quick-action on the Home Screen
  is automatically included.
- When `grinder:` is **absent**, `set_grinder()` is never called.  The C++ class
  detects that no grinder is bound and omits the "Grind" item from the Home Screen
  list and all menus — no YAML boilerplate needed from the user.
- The same pattern applies to `heater:` (temperature edit available) and
  `flow_meter:` (live flow rate shown on Home Screen).

This means the display component is **self-adapting**: add a component reference and
its controls appear; remove it and they disappear.

---

### 6.6 C++ Menu State Machine

```
enum class Screen {
  HOME,              // action hub (idle + all active-progress modes)
  BREW_MENU,
  STEAM_MENU,
  GRINDER_MENU,
  SETTINGS_MENU,
  MAINTENANCE_MENU,
  PRE_INFUSION_MENU,
  EDIT_NUMBER,       // shared full-screen number editor
  EDIT_TOGGLE,       // inline ON/OFF toggle (no separate screen)
  CONFIRM_DIALOG,
  ERROR_OVERLAY,
};
```

The `loop()` method:
1. Reads encoder delta since last tick.
2. Reads button state (short click / long press from a timer in `on_press`/`on_release`).
3. Dispatches to the active screen's `handle_input()` function.
4. Calls `render(display_ref)` if state changed or a periodic refresh is due
   (every 500 ms for live-data screens is recommended for the ST7920 to avoid display
   bus saturation and ghosting; static menus need no periodic refresh).

---

### 6.7 Display Rendering

Each screen has a dedicated `render_<screen>(display_ref)` function.
The theme object is passed into every render function; it controls glyphs, fonts,
and layout constants so render functions are theme-agnostic.

Fonts required:

| Font | Usage | Source |
|---|---|---|
| `mono_8` (6×8 px) | Menus, labels, all text | `DejaVuSansMono-8.bdf` (committed to repo) |
| `mono_16` (10×16 px) | Large numbers on Home/Progress screens | `DejaVuSansMono-16.bdf` |

Font files are committed under `components/espresso_machine_display/fonts/` to avoid
CI failures from blocked Google Fonts downloads.

---

### 6.8 Safety Constraints

- Display rendering **must not block** — all render calls complete in one `loop()` tick.
- No menu action bypasses the orchestrator's own safety checks; the display calls the
  same public C++ API as Home Assistant automations.
- If the over-temperature cutoff is triggered, the Home Screen **immediately** forces
  the display to show the safety overlay (§ 4.13) regardless of which screen is active,
  and this overlay cannot be dismissed by the encoder.

---

### 6.9 Implementation Roadmap (Phase 10)

Phase 10 in `PLAN.md` is reserved for "Display & UI (Optional)".

| Sub-phase | Deliverable |
|---|---|
| **10a** | `espresso_machine_display` schema + `__init__.py`; ST7920 in example YAML; Home Screen status only (`classic` theme, no menu navigation) |
| **10b** | Full Home Screen action hub (Brew/Steam/Grind/Settings/Power); Brew Menu + confirm; Brew Progress inline |
| **10c** | Steam Menu, Grinder Menu, Pre-infusion sub-menu, Settings + Maintenance |
| **10d** | Number-edit mode, ON/OFF toggle, confirmation dialogs, error overlays |
| **10e** | Additional themes (`minimal`, `barista`, `dark`); screensaver; beeper support |

Unit testing covers the menu state machine in isolation via a mock display object in
the GoogleTest suite under `tests/cpp/`.

---

## 7. Open Questions (for approval)

### Resolved by this design revision

| # | Question | Resolution |
|---|---|---|
| — | Should there be a separate "Main Menu" screen? | **No** — Home Screen *is* the main menu |
| — | How to start Brew/Steam/Grind quickly? | **Home Screen action hub** — one click from the home cursor |
| — | YAML API shape? | **`espresso_machine: id` + `theme: name`** minimal; full ref in § 6.3 |
| — | How do optional components affect the UI? | **Component publishing** — bind by `id:`, UI adapts automatically |

### Still open (please decide before Phase 10a begins)

1. **Beeper feedback** — audible navigation click + action confirmation beep.
   `beeper_pin:` is in the schema; should it default to enabled or disabled?
   Requires one free PWM-capable GPIO (e.g. `GPIO2` on the BEEPER pin from EXP1).

2. **Screensaver** — blank after `screensaver_timeout:` of no input.
   Proposed default: `5min`.  Set to `0` to disable.

3. **Value persistence** — edits via the encoder take effect immediately (live entity
   update via ESPHome number/switch API).  Persistence across reboots is handled by
   the entity's own `restore_value: true` flag — no special action needed.
   **Resolved:** No extra work required; ESPHome entities already handle this.

4. **Encoder resolution** — some EC11 batches emit 2 or 4 pulses per physical detent.
   The YAML `resolution:` parameter on `sensor: rotary_encoder` covers this.
   Proposed: document `resolution: 1` as default and note that users may need
   `resolution: 2` or `resolution: 4` for their specific hardware batch.

5. **`home_actions:` order default** — The proposed default list is:
   `brew → steam → grind → settings → power`.
   Note: wrap-around navigation (last item → first) would make "power" adjacent to "brew",
   potentially causing accidental power-off.  Proposal: **disable wrap-around on the Home
   Screen**, or place `power` in the middle of the list (e.g. after `settings`).  Please confirm.

6. **Theme default** — Which theme should be the default when `theme:` is omitted?
   Proposal: `classic` (matches Creality hardware origin).

7. **`barista` theme Home Screen layout** — The barista theme shows temperature in
   large font with a compact action strip at the bottom.  Should it still allow the
   rotary encoder to navigate the action strip, or should it be click-to-cycle only
   (to avoid the large temp readout jumping out of view)?

8. **Component packaging** — Should `espresso_machine_display` be part of the main
   `external_components:` source (same repo, same `components/` directory), or a
   separate optional add-on that users can include independently?
   Proposal: same repo, same `components/` directory (simplest for end users).

---

*Approve this document to proceed to implementation (Phase 10a).*

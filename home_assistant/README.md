# Home Assistant UI — Quick Start

See **[CUSTOM_UI.md](../CUSTOM_UI.md)** (repo root) for full installation
instructions, troubleshooting, and feature documentation.

> **No SSH, no AppDaemon, no terminal.**  
> Install the **Espresso Shot History** add-on by adding this repo as a custom
> add-on repository in HA — configure it in the UI, click Start, done.

## Files in this directory

| File | Purpose |
|---|---|
| `espresso_package.yaml` | **Combined** helpers + template sensors — include this single file in `configuration.yaml` |
| `helpers.yaml` | HA input helpers — alternative if you prefer separate files |
| `template_sensors.yaml` | Template sensors for shot viewer — alternative if you prefer separate files |
| `dashboards/espresso_real.yaml` | Lovelace dashboard for real hardware |
| `dashboards/espresso_mock.yaml` | Lovelace dashboard for mock/simulation |
| `appdaemon/shot_history.py` | AppDaemon app that records every shot (alternative — add-on is preferred) |
| `appdaemon/apps.yaml` | AppDaemon configuration (alternative) |

## Setup (no SSH required)

1. Install HACS cards: **Mushroom**, **Mini Graph Card**, **ApexCharts Card**.
2. Install the **File Editor** add-on; use it to add `espresso_package.yaml`
   as an HA package in `configuration.yaml` and restart HA.
3. Add `https://github.com/shaggitza/test_espresso_esphome` as a custom add-on
   repository (**Settings → Add-ons → Add-on Store → ⋮ → Repositories**).
4. Install **Espresso Shot History** from the add-on store, configure entity IDs
   in the add-on's **Configuration** tab, enable **Auto-start**, click **Start**.
5. Paste the appropriate dashboard YAML into a new HA dashboard via the Raw
   configuration editor.
6. Pull an espresso — the shot appears in the Shot History view automatically.

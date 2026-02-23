# Home Assistant UI — Quick Start

See **[CUSTOM_UI.md](../CUSTOM_UI.md)** (repo root) for full installation
instructions, troubleshooting, and feature documentation.

> **No SSH needed.** The recommended path uses only the HA web UI and the
> **File Editor** add-on (available from the HA Add-on Store).

## Files in this directory

| File | Purpose |
|---|---|
| `espresso_package.yaml` | **Combined** helpers + template sensors — include this single file in `configuration.yaml` |
| `helpers.yaml` | HA input helpers — alternative if you prefer separate files |
| `template_sensors.yaml` | Template sensors for shot viewer — alternative if you prefer separate files |
| `dashboards/espresso_real.yaml` | Lovelace dashboard for real hardware |
| `dashboards/espresso_mock.yaml` | Lovelace dashboard for mock/simulation |
| `appdaemon/shot_history.py` | AppDaemon app that records every shot |
| `appdaemon/apps.yaml` | AppDaemon configuration |

## Setup (no SSH required)

1. Install **File Editor** add-on from the HA Add-on Store.
2. Use File Editor to copy `espresso_package.yaml` to your HA config folder and
   add `!include espresso_package.yaml` as a package in `configuration.yaml`.
3. Restart HA — four helper entities are created automatically.
4. Install **AppDaemon 4** add-on, then use File Editor to copy
   `appdaemon/shot_history.py` and configure `apps.yaml`.
5. Install HACS cards: **Mushroom**, **Mini Graph Card**, **ApexCharts Card**.
6. Paste the appropriate dashboard YAML into a new HA dashboard via the Raw
   configuration editor.
7. Pull an espresso — the shot appears in the Shot History view automatically.

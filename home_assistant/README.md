# Home Assistant UI — Quick Start

See **[CUSTOM_UI.md](../CUSTOM_UI.md)** (repo root) for full installation
instructions, troubleshooting, and feature documentation.

## Files in this directory

| File | Purpose |
|---|---|
| `helpers.yaml` | HA input helpers — add to `configuration.yaml` |
| `template_sensors.yaml` | Template sensors for shot viewer — add to `configuration.yaml` |
| `dashboards/espresso_real.yaml` | Lovelace dashboard for real hardware |
| `dashboards/espresso_mock.yaml` | Lovelace dashboard for mock/simulation |
| `appdaemon/shot_history.py` | AppDaemon app that records every shot |
| `appdaemon/apps.yaml` | AppDaemon configuration |

## 60-second setup

1. Add `helpers.yaml` + `template_sensors.yaml` as HA packages, restart HA.
2. Copy `appdaemon/shot_history.py` + `appdaemon/apps.yaml` to AppDaemon `apps/`.
3. Install HACS cards: **Mushroom**, **Mini Graph Card**, **ApexCharts Card**.
4. Paste the appropriate dashboard YAML into a new HA dashboard.
5. Pull an espresso — the shot appears in the Shot History view automatically.

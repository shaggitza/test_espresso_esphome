# =============================================================================
# AppDaemon — Espresso Shot History Tracker
# =============================================================================
#
# This app listens to the ESPHome machine_mode text sensor and automatically
# records each completed espresso shot to the input_text.espresso_shot_log
# helper in Home Assistant.
#
# When the user selects a shot from the input_select.espresso_shot_viewer
# dropdown, the app updates input_datetime.espresso_shot_view_start and
# input_datetime.espresso_shot_view_end so that the dashboard graphs
# snap to that shot's time window.
#
# CONFIGURATION (apps.yaml):
#   espresso_shot_history:
#     module: shot_history
#     class: ShotHistory
#     mode_entity: text_sensor.philips_barista_brew_machine_mode
#     duration_entity: sensor.philips_barista_brew_last_shot_duration
#     yield_entity: sensor.philips_barista_brew_last_shot_yield
#     temp_entity: sensor.philips_barista_brew_thermoblock_temperature
#     max_shots: 20
#     graph_padding_sec: 15
#
# For the MOCK device, use these entity names instead:
#   mode_entity: text_sensor.philips_barista_brew_mock_machine_mode
#   duration_entity: sensor.philips_barista_brew_mock_last_shot_duration
#   yield_entity: sensor.philips_barista_brew_mock_last_shot_yield
#   temp_entity: sensor.philips_barista_brew_mock_thermoblock_temperature
# =============================================================================

import appdaemon.plugins.hass.hassapi as hass
import json
from datetime import datetime, timezone, timedelta


class ShotHistory(hass.Hass):

    def initialize(self):
        self.shot_start_time = None
        self.shots = []

        # Configuration with defaults
        self.mode_entity = self.args.get(
            "mode_entity",
            "text_sensor.philips_barista_brew_machine_mode"
        )
        self.duration_entity = self.args.get(
            "duration_entity",
            "sensor.philips_barista_brew_last_shot_duration"
        )
        self.yield_entity = self.args.get(
            "yield_entity",
            "sensor.philips_barista_brew_last_shot_yield"
        )
        self.temp_entity = self.args.get(
            "temp_entity",
            "sensor.philips_barista_brew_thermoblock_temperature"
        )
        self.max_shots = int(self.args.get("max_shots", 20))
        self.graph_padding_sec = int(self.args.get("graph_padding_sec", 15))

        # Load any previously recorded shots from the HA helper
        self._load_shots()

        # Listen for machine mode changes (shot tracking)
        self.listen_state(self.on_mode_change, self.mode_entity)

        # Listen for shot selection changes (graph window updates)
        self.listen_state(
            self.on_shot_selected,
            "input_select.espresso_shot_viewer"
        )

        self.log(
            f"ShotHistory initialized — tracking {self.mode_entity}; "
            f"{len(self.shots)} shots in log",
            level="INFO"
        )

    # -------------------------------------------------------------------------
    # Mode change handler
    # -------------------------------------------------------------------------

    def on_mode_change(self, entity, attribute, old, new, kwargs):
        """Record shot start and end times as the machine changes mode."""
        if new == "brewing":
            self.shot_start_time = datetime.now(tz=timezone.utc)
            self.log("Shot started", level="INFO")

        elif old == "brewing" and new == "idle":
            if self.shot_start_time is not None:
                self.log("Shot completed — recording stats", level="INFO")
                self._record_shot()
            else:
                self.log(
                    "Shot ended but start time was not recorded "
                    "(app may have just started — skipping)",
                    level="WARNING"
                )

    # -------------------------------------------------------------------------
    # Shot selection handler
    # -------------------------------------------------------------------------

    def on_shot_selected(self, entity, attribute, old, new, kwargs):
        """Update the datetime helpers when the user picks a different shot."""
        if new in ("No shots recorded yet", "unknown", "unavailable", ""):
            return

        for shot in self.shots:
            if self._shot_label(shot) == new:
                self._update_view_window(shot)
                return

        self.log(f"Selected shot label not found in log: {new!r}", level="WARNING")

    # -------------------------------------------------------------------------
    # Internal helpers
    # -------------------------------------------------------------------------

    def _load_shots(self):
        """Restore shot log from the HA input_text helper on startup."""
        raw = self.get_state("input_text.espresso_shot_log")
        if raw and raw not in ("unknown", "unavailable", "[]", ""):
            try:
                self.shots = json.loads(raw)
                self.log(f"Loaded {len(self.shots)} shots from HA helper", level="INFO")
            except (json.JSONDecodeError, TypeError) as exc:
                self.log(f"Could not parse shot log JSON: {exc}", level="WARNING")
                self.shots = []
        else:
            self.shots = []

    def _record_shot(self):
        """Save the just-completed shot and refresh HA helpers."""
        end_time = datetime.now(tz=timezone.utc)

        duration = self._safe_float(self.get_state(self.duration_entity))
        yield_ml = self._safe_float(self.get_state(self.yield_entity))
        temp_c = self._safe_float(self.get_state(self.temp_entity))

        shot = {
            "number": (self.shots[-1]["number"] + 1) if self.shots else 1,
            "start": self.shot_start_time.isoformat(),
            "end": end_time.isoformat(),
            "duration_s": duration,
            "yield_ml": yield_ml,
            "temp_c": temp_c,
        }

        self.shots.append(shot)
        self.shots = self.shots[-self.max_shots:]   # keep only the last N shots
        self.shot_start_time = None

        self._save_shots()
        self._refresh_select()
        # Auto-select the newest shot so the graph immediately shows it
        self._select_shot_by_index(-1)

    def _save_shots(self):
        """Persist the shot log to the HA input_text helper."""
        self.call_service(
            "input_text/set_value",
            entity_id="input_text.espresso_shot_log",
            value=json.dumps(self.shots),
        )

    def _refresh_select(self):
        """Rebuild the dropdown options list from the current shot log."""
        if not self.shots:
            options = ["No shots recorded yet"]
        else:
            # Newest shot first
            options = [self._shot_label(s) for s in reversed(self.shots)]

        self.call_service(
            "input_select/set_options",
            entity_id="input_select.espresso_shot_viewer",
            options=options,
        )

    def _select_shot_by_index(self, index: int):
        """Select the shot at list position `index` (supports negative indexing)."""
        if not self.shots:
            return
        shot = self.shots[index]
        self.call_service(
            "input_select/select_option",
            entity_id="input_select.espresso_shot_viewer",
            option=self._shot_label(shot),
        )

    def _update_view_window(self, shot: dict):
        """
        Set the two datetime helpers to the selected shot's time window,
        with `graph_padding_sec` seconds of padding on each side.
        """
        start = datetime.fromisoformat(shot["start"])
        end = datetime.fromisoformat(shot["end"])

        padded_start = start - timedelta(seconds=self.graph_padding_sec)
        padded_end = end + timedelta(seconds=self.graph_padding_sec)

        self.call_service(
            "input_datetime/set_datetime",
            entity_id="input_datetime.espresso_shot_view_start",
            datetime=padded_start.astimezone().strftime("%Y-%m-%d %H:%M:%S"),
        )
        self.call_service(
            "input_datetime/set_datetime",
            entity_id="input_datetime.espresso_shot_view_end",
            datetime=padded_end.astimezone().strftime("%Y-%m-%d %H:%M:%S"),
        )
        self.log(
            f"Shot #{shot['number']} window: {padded_start} → {padded_end}",
            level="INFO"
        )

    @staticmethod
    def _shot_label(shot: dict) -> str:
        """Return a human-readable label for the dropdown selector."""
        dt = datetime.fromisoformat(shot["start"]).astimezone()
        date_str = dt.strftime("%m/%d %H:%M")
        return (
            f"Shot #{shot['number']} | {date_str} | "
            f"{shot['duration_s']:.0f}s | {shot['yield_ml']:.1f} mL"
        )

    @staticmethod
    def _safe_float(value, default: float = 0.0) -> float:
        """Convert a state string to float, returning default on failure."""
        try:
            return float(value)
        except (TypeError, ValueError):
            return default

#!/usr/bin/env python3
# =============================================================================
# Espresso Shot History — Home Assistant Add-on
# =============================================================================
#
# Standalone asyncio service that:
#   1. Connects to the HA WebSocket API using the Supervisor token.
#   2. Watches the ESPHome machine_mode text sensor for brewing transitions.
#   3. Records each completed shot (duration, yield, temperature) to the
#      input_text.espresso_shot_log helper and the local /data/shots.json file.
#   4. Refreshes the input_select.espresso_shot_viewer dropdown.
#   5. Responds to shot selection changes by updating the two input_datetime
#      helpers so the dashboard graphs snap to the selected shot's time window.
#
# Configuration is read from /data/options.json (written by HA Supervisor from
# the add-on Options panel).  Shot log is persisted to /data/shots.json so it
# survives add-on restarts independently of the HA helper.
# =============================================================================

from __future__ import annotations

import asyncio
import json
import logging
import os
from datetime import datetime, timedelta, timezone

import aiohttp
import websockets

_LOGGER = logging.getLogger("shot_history")
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)

HA_WS_URL = "ws://supervisor/core/websocket"
HA_API_URL = "http://supervisor/core/api"
OPTIONS_FILE = "/data/options.json"
SHOTS_FILE = "/data/shots.json"


def _load_options() -> dict:
    try:
        with open(OPTIONS_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        _LOGGER.warning("Could not read options.json (%s) — using defaults", exc)
        return {}


class ShotHistory:
    """Tracks espresso shots and keeps HA helper entities in sync."""

    def __init__(self, options: dict) -> None:
        self.mode_entity: str = options.get(
            "mode_entity",
            "text_sensor.philips_barista_brew_machine_mode",
        )
        self.duration_entity: str = options.get(
            "duration_entity",
            "sensor.philips_barista_brew_last_shot_duration",
        )
        self.yield_entity: str = options.get(
            "yield_entity",
            "sensor.philips_barista_brew_last_shot_yield",
        )
        self.temp_entity: str = options.get(
            "temp_entity",
            "sensor.philips_barista_brew_thermoblock_temperature",
        )
        self.max_shots: int = int(options.get("max_shots", 20))
        self.graph_padding_sec: int = int(options.get("graph_padding_sec", 15))

        self.shot_start_time: datetime | None = None
        self.shots: list[dict] = self._load_shots_from_disk()
        self._msg_id: int = 0
        self._token: str = os.environ.get("SUPERVISOR_TOKEN", "")

    # -------------------------------------------------------------------------
    # Persistence
    # -------------------------------------------------------------------------

    def _load_shots_from_disk(self) -> list[dict]:
        """Load shot log from the local /data/shots.json file."""
        try:
            with open(SHOTS_FILE) as f:
                shots = json.load(f)
            _LOGGER.info("Loaded %d shots from %s", len(shots), SHOTS_FILE)
            return shots
        except FileNotFoundError:
            return []
        except (json.JSONDecodeError, TypeError) as exc:
            _LOGGER.warning("Could not parse %s: %s — starting fresh", SHOTS_FILE, exc)
            return []

    def _save_shots_to_disk(self) -> None:
        """Persist shot log to /data/shots.json."""
        with open(SHOTS_FILE, "w") as f:
            json.dump(self.shots, f)

    # -------------------------------------------------------------------------
    # HA REST helpers
    # -------------------------------------------------------------------------

    def _auth_headers(self) -> dict:
        return {
            "Authorization": f"Bearer {self._token}",
            "Content-Type": "application/json",
        }

    async def _get_state(
        self, session: aiohttp.ClientSession, entity_id: str
    ) -> str | None:
        url = f"{HA_API_URL}/states/{entity_id}"
        try:
            async with session.get(url, headers=self._auth_headers()) as resp:
                if resp.status == 200:
                    data = await resp.json()
                    return data.get("state")
                _LOGGER.warning("GET state %s → HTTP %s", entity_id, resp.status)
        except aiohttp.ClientError as exc:
            _LOGGER.warning("GET state %s failed: %s", entity_id, exc)
        return None

    async def _call_service(
        self,
        session: aiohttp.ClientSession,
        domain: str,
        service: str,
        data: dict,
    ) -> None:
        url = f"{HA_API_URL}/services/{domain}/{service}"
        try:
            async with session.post(
                url, headers=self._auth_headers(), json=data
            ) as resp:
                if resp.status not in (200, 201):
                    _LOGGER.warning(
                        "Service %s.%s failed: HTTP %s", domain, service, resp.status
                    )
        except aiohttp.ClientError as exc:
            _LOGGER.warning("Service %s.%s error: %s", domain, service, exc)

    # -------------------------------------------------------------------------
    # Shot recording
    # -------------------------------------------------------------------------

    async def _record_shot(self, session: aiohttp.ClientSession) -> None:
        """Capture stats for the just-completed shot and sync everything."""
        end_time = datetime.now(tz=timezone.utc)

        duration = _safe_float(await self._get_state(session, self.duration_entity))
        yield_ml = _safe_float(await self._get_state(session, self.yield_entity))
        temp_c = _safe_float(await self._get_state(session, self.temp_entity))

        shot: dict = {
            "number": (self.shots[-1]["number"] + 1) if self.shots else 1,
            "start": self.shot_start_time.isoformat(),  # type: ignore[union-attr]
            "end": end_time.isoformat(),
            "duration_s": duration,
            "yield_ml": yield_ml,
            "temp_c": temp_c,
        }

        self.shots.append(shot)
        self.shots = self.shots[-self.max_shots:]
        self.shot_start_time = None

        self._save_shots_to_disk()
        await self._sync_ha_helpers(session)
        _LOGGER.info(
            "Recorded shot #%d: %.0fs / %.1f mL / %.1f°C",
            shot["number"],
            duration,
            yield_ml,
            temp_c,
        )

    async def _sync_ha_helpers(self, session: aiohttp.ClientSession) -> None:
        """Push current shot log and dropdown options to HA helpers."""
        await self._call_service(
            session,
            "input_text",
            "set_value",
            {
                "entity_id": "input_text.espresso_shot_log",
                "value": json.dumps(self.shots),
            },
        )

        options = (
            ["No shots recorded yet"]
            if not self.shots
            else [_shot_label(s) for s in reversed(self.shots)]
        )
        await self._call_service(
            session,
            "input_select",
            "set_options",
            {
                "entity_id": "input_select.espresso_shot_viewer",
                "options": options,
            },
        )

        if self.shots:
            await self._call_service(
                session,
                "input_select",
                "select_option",
                {
                    "entity_id": "input_select.espresso_shot_viewer",
                    "option": _shot_label(self.shots[-1]),
                },
            )
            await self._update_view_window(session, self.shots[-1])

    async def _update_view_window(
        self, session: aiohttp.ClientSession, shot: dict
    ) -> None:
        """Set the datetime helpers to the selected shot's (padded) time window."""
        start = datetime.fromisoformat(shot["start"])
        end = datetime.fromisoformat(shot["end"])
        padded_start = start - timedelta(seconds=self.graph_padding_sec)
        padded_end = end + timedelta(seconds=self.graph_padding_sec)
        fmt = "%Y-%m-%d %H:%M:%S"

        await self._call_service(
            session,
            "input_datetime",
            "set_datetime",
            {
                "entity_id": "input_datetime.espresso_shot_view_start",
                "datetime": padded_start.astimezone().strftime(fmt),
            },
        )
        await self._call_service(
            session,
            "input_datetime",
            "set_datetime",
            {
                "entity_id": "input_datetime.espresso_shot_view_end",
                "datetime": padded_end.astimezone().strftime(fmt),
            },
        )
        _LOGGER.info(
            "Shot #%d graph window: %s → %s",
            shot["number"],
            padded_start,
            padded_end,
        )

    # -------------------------------------------------------------------------
    # Event handlers
    # -------------------------------------------------------------------------

    async def _on_mode_change(
        self,
        session: aiohttp.ClientSession,
        old_state: str,
        new_state: str,
    ) -> None:
        if new_state == "brewing":
            self.shot_start_time = datetime.now(tz=timezone.utc)
            _LOGGER.info("Shot started")
        elif old_state == "brewing" and new_state == "idle":
            if self.shot_start_time is not None:
                _LOGGER.info("Shot completed — recording stats")
                await self._record_shot(session)
            else:
                _LOGGER.warning(
                    "Shot ended but start time not recorded (add-on just started?) — skipping"
                )

    async def _on_shot_selected(
        self, session: aiohttp.ClientSession, new_value: str
    ) -> None:
        if new_value in ("No shots recorded yet", "unknown", "unavailable", ""):
            return
        for shot in self.shots:
            if _shot_label(shot) == new_value:
                await self._update_view_window(session, shot)
                return
        _LOGGER.warning("Selected shot label not found in log: %r", new_value)

    # -------------------------------------------------------------------------
    # WebSocket connection loop
    # -------------------------------------------------------------------------

    async def run(self) -> None:
        _LOGGER.info(
            "ShotHistory starting — tracking %s; %d shots in log",
            self.mode_entity,
            len(self.shots),
        )
        async with aiohttp.ClientSession() as session:
            # On startup, restore helpers from local shot log so the dashboard
            # is correct even if HA was restarted and cleared the helper state.
            if self.shots:
                _LOGGER.info("Restoring HA helpers from local shot log")
                await self._sync_ha_helpers(session)

            while True:
                try:
                    await self._connect(session)
                except (
                    websockets.WebSocketException,
                    aiohttp.ClientError,
                    OSError,
                    asyncio.TimeoutError,
                    RuntimeError,
                ) as exc:
                    _LOGGER.error(
                        "WebSocket connection lost: %s — reconnecting in 10 s", exc
                    )
                    await asyncio.sleep(10)

    async def _connect(self, session: aiohttp.ClientSession) -> None:
        """Authenticate and subscribe to state_changed events."""
        async with websockets.connect(HA_WS_URL) as ws:
            # Step 1 — authentication handshake
            auth_required = json.loads(await ws.recv())
            if auth_required.get("type") != "auth_required":
                raise RuntimeError(f"Unexpected first message: {auth_required}")

            await ws.send(
                json.dumps({"type": "auth", "access_token": self._token})
            )
            auth_result = json.loads(await ws.recv())
            if auth_result.get("type") != "auth_ok":
                raise RuntimeError(f"Authentication failed: {auth_result}")

            # Step 2 — subscribe to all state_changed events
            self._msg_id += 1
            sub_id = self._msg_id
            await ws.send(
                json.dumps(
                    {
                        "id": sub_id,
                        "type": "subscribe_events",
                        "event_type": "state_changed",
                    }
                )
            )
            ack = json.loads(await ws.recv())
            _LOGGER.info(
                "Connected to HA WebSocket (sub id=%d, success=%s)",
                sub_id,
                ack.get("success"),
            )

            # Step 3 — event loop
            async for raw in ws:
                msg = json.loads(raw)
                if msg.get("type") != "event":
                    continue

                data = msg.get("event", {}).get("data", {})
                entity_id: str = data.get("entity_id", "")
                new_state: str = (data.get("new_state") or {}).get("state", "")
                old_state: str = (data.get("old_state") or {}).get("state", "")

                if entity_id == self.mode_entity:
                    await self._on_mode_change(session, old_state, new_state)
                elif entity_id == "input_select.espresso_shot_viewer":
                    await self._on_shot_selected(session, new_state)


# =============================================================================
# Utilities
# =============================================================================


def _shot_label(shot: dict) -> str:
    """Return a human-readable label for the dropdown selector."""
    dt = datetime.fromisoformat(shot["start"]).astimezone()
    date_str = dt.strftime("%m/%d %H:%M")
    return (
        f"Shot #{shot['number']} | {date_str} | "
        f"{shot['duration_s']:.0f}s | {shot['yield_ml']:.1f} mL"
    )


def _safe_float(value: str | None, default: float = 0.0) -> float:
    """Convert a HA state string to float, returning *default* on failure."""
    try:
        return float(value) if value is not None else default
    except (TypeError, ValueError):
        return default


# =============================================================================
# Entry point
# =============================================================================


if __name__ == "__main__":
    asyncio.run(ShotHistory(_load_options()).run())

"""TokenSlate menu bar companion app.

Runs the bridge's refresh + BLE loops in-process (not as a subprocess of
the `tokenslate` CLI) specifically so macOS's Bluetooth permission prompt
attributes to this app bundle ("TokenSlate") instead of a bare `python3`
interpreter -- see docs §9 macapp note.
"""

from __future__ import annotations

import asyncio
import json
import sys
import threading
from datetime import datetime, timezone

import rumps

from tokenslate import ble
from tokenslate.codexbar import CodexBarError, fetch_dashboard, slim
from tokenslate.config import Config
from tokenslate.state import State, now_iso

POLL_CHUNK_SECONDS = 1.0


class TokenSlateApp(rumps.App):
    def __init__(self) -> None:
        super().__init__("TokenSlate", title="TS", quit_button=None)
        self.cfg = Config.load()
        self._running = False
        self._thread: threading.Thread | None = None
        self._payload: bytes | None = None
        self._ble_status = "stopped"
        self._last_sync: str | None = None
        self._discovered_devices: list[str] = []
        self._devices_dirty = False

        self.status_item = rumps.MenuItem("BLE: stopped")
        self.sync_item = rumps.MenuItem("Last sync: never")
        self.toggle_item = rumps.MenuItem("Start Service")
        self.device_item = rumps.MenuItem(f"Device: {self.cfg.device_name_prefix}")
        self.set_name_item = rumps.MenuItem("Set Device Name...")
        self.scan_item = rumps.MenuItem("Scan for Devices...")
        self.found_devices_menu = rumps.MenuItem("Found Devices")
        self.found_devices_menu.add(rumps.MenuItem("(no scan yet)"))
        self.about_item = rumps.MenuItem("About TokenSlate")
        self.quit_item = rumps.MenuItem("Quit")

        self.menu = [
            self.status_item,
            self.sync_item,
            None,
            self.toggle_item,
            None,
            self.device_item,
            self.set_name_item,
            self.scan_item,
            self.found_devices_menu,
            None,
            self.about_item,
            self.quit_item,
        ]

        self.ui_timer = rumps.Timer(self._refresh_ui, 2)
        self.ui_timer.start()
        self._start()

    # --- UI ---------------------------------------------------------

    @rumps.clicked("Start Service")
    def toggle_service(self, _sender: rumps.MenuItem) -> None:
        if self._running:
            self._stop()
        else:
            self._start()

    def _start(self) -> None:
        self._running = True
        self.toggle_item.title = "Stop Service"
        self.title = "TS ●"
        self._thread = threading.Thread(target=self._thread_main, daemon=True)
        self._thread.start()

    def _stop(self) -> None:
        self._running = False
        self.toggle_item.title = "Start Service"
        self.title = "TS"
        self._ble_status = "stopped"

    def _refresh_ui(self, _timer: rumps.Timer) -> None:
        self.status_item.title = f"BLE: {self._ble_status}"
        self.sync_item.title = f"Last sync: {self._format_sync_time()}"
        self.device_item.title = f"Device: {self.cfg.device_name_prefix}"
        if self._devices_dirty:
            self._devices_dirty = False
            self._rebuild_found_devices_menu()

    def _format_sync_time(self) -> str:
        if not self._last_sync:
            return "never"
        try:
            dt = datetime.fromisoformat(self._last_sync.replace("Z", "+00:00"))
            local = dt.astimezone()
            return local.strftime("%-I:%M %p")
        except (ValueError, TypeError):
            return self._last_sync

    @rumps.clicked("Set Device Name...")
    def set_device_name(self, _sender: rumps.MenuItem) -> None:
        response = rumps.Window(
            message="Device name (or prefix) to scan for -- must match what "
                    "the firmware advertises, see firmware/src/main.cpp "
                    "DEVICE_NAME.",
            title="Set Device Name",
            default_text=self.cfg.device_name_prefix,
            ok="Save",
            cancel="Cancel",
        ).run()
        if response.clicked and response.text.strip():
            name = response.text.strip()
            self.cfg.device_name_prefix = name
            self.cfg.save()
            self.device_item.title = f"Device: {name}"

    @rumps.clicked("Scan for Devices...")
    def scan_for_devices(self, _sender: rumps.MenuItem) -> None:
        self.scan_item.title = "Scanning..."
        threading.Thread(target=self._scan_thread, daemon=True).start()

    def _scan_thread(self) -> None:
        loop = asyncio.new_event_loop()
        try:
            names = loop.run_until_complete(ble.discover_named_devices())
        except Exception:
            names = []
        finally:
            loop.close()
        self._discovered_devices = names
        self._devices_dirty = True  # picked up by the next _refresh_ui tick

    def _rebuild_found_devices_menu(self) -> None:
        self.scan_item.title = "Scan for Devices..."
        self.found_devices_menu.clear()
        if not self._discovered_devices:
            self.found_devices_menu.add(rumps.MenuItem("(none found)"))
            return
        for name in self._discovered_devices:
            item = rumps.MenuItem(name)
            item.set_callback(self._select_device(name))
            self.found_devices_menu.add(item)

    def _select_device(self, name: str):
        def _handler(_sender: rumps.MenuItem) -> None:
            self.cfg.device_name_prefix = name
            self.cfg.save()
            self.device_item.title = f"Device: {name}"
        return _handler

    @rumps.clicked("About TokenSlate")
    def show_about(self, _sender: rumps.MenuItem) -> None:
        rumps.alert(
            title="TokenSlate",
            message=(
                "TokenSlate host bridge companion\n"
                f"Providers: {', '.join(self.cfg.providers)}\n"
                f"Device: {self.cfg.device_name_prefix}\n\n"
                "https://github.com/nogoodusername/TokenSlate"
            ),
        )

    @rumps.clicked("Quit")
    def quit_app(self, _sender: rumps.MenuItem) -> None:
        if self._running:
            self._stop()
        rumps.quit_application()

    # --- background loops --------------------------------------------

    def _thread_main(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(self._async_main())
        finally:
            loop.close()

    async def _async_main(self) -> None:
        await asyncio.gather(self._refresh_loop(), self._ble_loop())

    async def _interruptible_sleep(self, total_seconds: float) -> None:
        remaining = total_seconds
        while self._running and remaining > 0:
            await asyncio.sleep(min(POLL_CHUNK_SECONDS, remaining))
            remaining -= POLL_CHUNK_SECONDS

    async def _refresh_loop(self) -> None:
        while self._running:
            # Reload from disk each pass -- config.yaml can change out
            # from under this already-running process (e.g. `tokenslate
            # providers select` run separately), and self.cfg was
            # otherwise only ever read once at app launch.
            self.cfg = Config.load()
            state = State.load() or State()
            try:
                dashboard = fetch_dashboard()
                payload = slim(dashboard, self.cfg.providers)
                self._payload = json.dumps(payload).encode()
                self._last_sync = now_iso()
                state.merge_save(
                    last_sync_at=self._last_sync,
                    last_sync_status="ok",
                    cached_providers=self.cfg.providers,
                )
            except CodexBarError as e:
                state.merge_save(last_sync_status=f"error: {e}")
            await self._interruptible_sleep(self.cfg.refresh_interval_seconds)

    async def _ble_loop(self) -> None:
        # Scans immediately and on every pass, regardless of whether a
        # payload is ready yet -- that scan is what triggers macOS's
        # Bluetooth permission prompt. Gating it behind the first
        # successful CodexBar fetch (as an earlier version did) delayed
        # the prompt by however long that fetch took.
        while self._running:
            try:
                device = await ble.find_device(self.cfg.device_name_prefix)
                if device is None:
                    self._ble_status = "device not found (asleep?)"
                elif self._payload is None:
                    self._ble_status = "device found, waiting for first sync"
                else:
                    await ble.write_to_device(device, self._payload)
                    self._ble_status = "connected (wrote snapshot)"
                    state = State.load() or State()
                    state.merge_save(last_seen_device_at=now_iso())
            except Exception as e:
                self._ble_status = f"error: {e}"
            await self._interruptible_sleep(ble.RESCAN_DELAY_SECONDS)


def main() -> None:
    TokenSlateApp().run()


if __name__ == "__main__":
    main()

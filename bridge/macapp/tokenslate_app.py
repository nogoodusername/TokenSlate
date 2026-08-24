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

        self.status_item = rumps.MenuItem("BLE: stopped")
        self.sync_item = rumps.MenuItem("Last sync: never")
        self.toggle_item = rumps.MenuItem("Start Service")
        self.about_item = rumps.MenuItem("About TokenSlate")
        self.quit_item = rumps.MenuItem("Quit")

        self.menu = [
            self.status_item,
            self.sync_item,
            None,
            self.toggle_item,
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
        self.sync_item.title = f"Last sync: {self._last_sync or 'never'}"

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
        while self._running:
            if self._payload is None:
                self._ble_status = "waiting for first sync"
            else:
                try:
                    found = await ble.connect_and_write(
                        self.cfg.device_name_prefix, self._payload
                    )
                    if found:
                        self._ble_status = "connected (wrote snapshot)"
                        state = State.load() or State()
                        state.merge_save(last_seen_device_at=now_iso())
                    else:
                        self._ble_status = "device not found (asleep?)"
                except Exception as e:
                    self._ble_status = f"error: {e}"
            await self._interruptible_sleep(ble.RESCAN_DELAY_SECONDS)


def main() -> None:
    TokenSlateApp().run()


if __name__ == "__main__":
    main()

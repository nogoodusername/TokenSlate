from __future__ import annotations

import asyncio
import sys
from typing import Callable, Optional

from bleak import BleakClient, BleakScanner

# Must match firmware/src/ble/gatt_service.cpp (docs §7).
SERVICE_UUID = "7a2a0001-6b5f-4a9e-9c9d-1f2e3a4b5c6d"
SNAPSHOT_CHAR_UUID = "7a2a0002-6b5f-4a9e-9c9d-1f2e3a4b5c6d"

SCAN_TIMEOUT_SECONDS = 8.0
RESCAN_DELAY_SECONDS = 5.0


def log(msg: str) -> None:
    print(f"[tokenslate] {msg}", file=sys.stderr, flush=True)


async def find_device(device_name_prefix: str):
    # Match on the live advertisement's local_name, not device.name --
    # macOS/CoreBluetooth caches a peripheral's name from any prior
    # connection (e.g. earlier firmware on the same board) and can return
    # that stale value instead of what's actually being advertised now.
    return await BleakScanner.find_device_by_filter(
        lambda d, adv: bool(adv.local_name and adv.local_name.startswith(device_name_prefix)),
        timeout=SCAN_TIMEOUT_SECONDS,
    )


async def discover_named_devices(timeout: float = SCAN_TIMEOUT_SECONDS) -> list[str]:
    """All distinct advertised local_names currently in range -- backs a
    device picker UI (docs §9 macapp) since the device name is expected
    to change/not always be "TokenSlate-"."""
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    names = {adv.local_name for _, adv in found.values() if adv.local_name}
    return sorted(names)


async def write_to_device(device, payload: bytes) -> None:
    async with BleakClient(device) as client:
        await client.write_gatt_char(SNAPSHOT_CHAR_UUID, payload, response=True)
        log(f"wrote {len(payload)} bytes to {device.name} ({device.address})")


async def connect_and_write(device_name_prefix: str, payload: bytes) -> bool:
    """One scan -> connect -> write -> disconnect cycle. Returns True on
    a successful write (device found and reachable), False if the device
    wasn't found this pass (not an error -- it's probably asleep)."""
    device = await find_device(device_name_prefix)
    if device is None:
        return False
    await write_to_device(device, payload)
    return True


async def run_ble_loop(get_payload: Callable[[], Optional[bytes]], device_name_prefix: str,
                        on_write: Callable[[], None], stop_event: asyncio.Event) -> None:
    """Runs forever: scan for the device, and if found+awake, write the
    latest cached payload. Sleeps briefly between passes -- cheap, this
    runs on the host, not the battery-constrained device (docs §9).

    Scanning always happens, even before the first payload is ready --
    that scan is what triggers macOS's Bluetooth permission prompt, and
    gating it behind a successful CodexBar fetch first delayed that
    prompt by however long the fetch took."""
    while not stop_event.is_set():
        try:
            device = await find_device(device_name_prefix)
            if device is not None:
                payload = get_payload()
                if payload is not None:
                    await write_to_device(device, payload)
                    on_write()
        except Exception as e:
            log(f"BLE cycle failed: {e}")
        await asyncio.sleep(RESCAN_DELAY_SECONDS)

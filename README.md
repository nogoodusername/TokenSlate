# TokenSlate

A desk/pocket display showing live AI coding-tool usage (Codex, Claude, and more) on a
battery-powered **LilyGO T-Display-S3**, synced from **CodexBar** over **Bluetooth LE** — no
Wi-Fi, no open ports.

![Board](https://img.shields.io/badge/hardware-LilyGO_T--Display--S3-blue)
![BLE](https://img.shields.io/badge/link-BLE_5-green)
![Power](https://img.shields.io/badge/power-deep_sleep~263µA-green)

## Overview

- Pulls per-provider usage (session %, weekly %, reset countdown) from CodexBar.
- A lightweight host bridge (`tokenslate`) refreshes the CodexBar cache and pushes it to the
  device over BLE whenever the device is awake.
- The device deep-sleeps by default — it wakes on a button press, syncs, renders, and sleeps
  again. Two physical buttons only.
- Battery-powered by design; the sleep routine includes the upstream SLPIN fix that brings deep
  sleep down to ~263 µA.

## Repo layout

```
├── firmware/   PlatformIO firmware for the T-Display-S3 (NimBLE peripheral, LVGL/TFT UI)
└── bridge/     Host-side Python CLI (`tokenslate`) — refresh loop + BLE sync + service install
```

## Getting started

### 1. Flash the firmware

```sh
cd firmware
pio run -t upload        # requires PlatformIO + the LilyGO T-Display-S3 board support
```

### 2. Install the host bridge

```sh
pip install -e bridge/
tokenslate providers select     # pick which providers show on the device
tokenslate service install      # runs the bridge at login so the device can always sync
```

### 3. Use it

Press **KEY** to wake and cycle screens. Press **BOOT** to put the device to sleep. A long
**KEY** press forces an immediate resync.

## Documentation

The full implementation handoff — power design, BLE protocol, firmware and bridge architecture,
and the build milestones — lives in [`docs/DOCUMENTATION.md`](docs/DOCUMENTATION.md).

## License

MIT — see [LICENSE](LICENSE). Free to use, modify, and distribute.

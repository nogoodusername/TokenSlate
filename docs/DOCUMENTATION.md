# TokenSlate — LilyGO T-Display-S3, BLE, battery-powered

A desk/pocket display that shows live AI coding-tool usage (Codex, Claude, etc.) pulled from
CodexBar, on a battery-powered T-Display-S3, synced over Bluetooth LE instead of Wi-Fi.

This document is a complete, implementation-ready handoff — everything decided during design
is captured here so a fresh session (human or Claude Code) can build from it without needing
the original conversation.

---

## 1. Goal & constraints

- Show per-provider usage (session %, weekly %, reset countdown) on a small TFT screen.
- Two physical buttons only (a third is a hardware reset, not programmable).
- Battery-powered — low power consumption is a hard requirement, not a nice-to-have.
- BLE, not Wi-Fi/HTTP — no server ever opens a network port; the host machine talks to the
  device directly over Bluetooth.
- Data periodically refreshes on its own; a long button press forces an immediate refresh.

### Non-goals (v1)
- Touchscreen input (device is assumed to be the plain, non-touch, non-AMOLED T-Display-S3).
- Multi-host pairing, OTA firmware updates, companion mobile app.
- True hardware power-off (no power-latch circuit on stock board) — "shutdown" means deep sleep.

---

## 2. Hardware reference

Board: **LilyGO T-Display-S3** (confirm this is the plain variant — AMOLED/Touch variants use a
different display driver and pin_config, but the rest of this plan is unaffected).

| Spec | Value |
|---|---|
| MCU | ESP32-S3R8, dual-core Xtensa LX7 @ 240MHz |
| RAM | 8MB PSRAM (OPI), 16MB flash |
| Display | 1.9" ST7789V TFT, 170×320, RGB565, 8-bit parallel (I8080) bus |
| Radios | Wi-Fi b/g/n (unused in this design) + BLE 5 |
| Board support | `Xinyuan-LilyGO/T-Display-S3` — TFT_eSPI (`Setup206_LilyGo_T_Display_S3.h`) or Arduino_GFX |

### Pins that matter

| Pin | Name | Role |
|---|---|---|
| GPIO0 | `PIN_BUTTON_1` / BOOT | Sleep-now / wake |
| GPIO14 | `PIN_BUTTON_2` / KEY | Cycle screens (short press) / force refresh (long press) / wake |
| GPIO38 | `PIN_LCD_BL` | Backlight (PWM-capable, drive digital HIGH/LOW is enough for v1) |
| GPIO15 | `PIN_POWER_ON` | LCD/peripheral power rail enable — must be HIGH before using the display, LOW before sleep |
| GPIO4 | battery ADC | Battery voltage (only readable when USB is unplugged) |
| RST | — | Hardware reset line straight to EN. **Not GPIO-addressable, cannot be given custom behavior.** |

Both GPIO0 and GPIO14 are RTC-capable (ESP32-S3 RTC GPIOs span 0–21), so both can be deep-sleep
wake sources via `EXT1`.

---

## 3. Power design — the critical fix

**Do not skip this section when implementing the sleep routine.**

A naive deep-sleep implementation on this exact board (cut `PIN_POWER_ON` + backlight, arm
`EXT0` wake, call `esp_deep_sleep_start()`) measures **~6.4 mA** in deep sleep — far above the
ESP32-S3's own capability (single-digit-to-low-hundreds of µA). This was reported and fixed
upstream: [Xinyuan-LilyGO/T-Display-S3#289](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/289),
fixed by [@lewisxhe](https://github.com/lewisxhe) in two commits
([4b2edc1](https://github.com/Xinyuan-LilyGO/T-Display-S3/commit/4b2edc1e5df2d370173b4758c128c4b8ca4f5c1e),
[c9fd58a](https://github.com/Xinyuan-LilyGO/T-Display-S3/commit/c9fd58a18d01d0c7663496afa1b0eb2a2aaa5676)),
confirmed by the reporter at **~263 µA** afterward.

**The fix:** send the ST7789 **SLPIN** command (`0x10`) to the display controller *before*
cutting its power rail — not after, not instead of.

```cpp
// TFT_eSPI:
tft.writecommand(0x10);

// or, lower-level esp_lcd panel API:
esp_lcd_panel_io_tx_param(io_handle, 0x10, NULL, 0);
```

**Exact order going into sleep** (deviating from this order is the likely cause if you
re-measure high current):

```
1. tft.writecommand(0x10)              // SLPIN — let the panel park itself
2. digitalWrite(PIN_LCD_BL, LOW)        // backlight off
3. digitalWrite(PIN_POWER_ON, LOW)      // cut the LCD/peripheral rail
4. esp_sleep_enable_ext1_wakeup(bitmask(GPIO0, GPIO14), ESP_EXT1_WAKEUP_ANY_LOW)
5. esp_deep_sleep_start()
```

Likely mechanism: the parallel display bus pins (WR/RD/DC/CS/D0–D7) aren't RTC-capable and stay
actively driven; if the panel's rail drops while it's mid-operation, those pins can backfeed
current into the now-unpowered controller through its ESD diodes. SLPIN lets the controller shut
its internal charge pumps down cleanly first.

### Power budget (informed estimate, validate in Milestone 0)

| State | Current | 500 mAh battery |
|---|---|---|
| Deep sleep (fixed) | ~263 µA | ~79 days idle-only |
| Awake (BLE connect + render) | tens of mA, ~1–3s per cycle | dominates the real-world budget — see Milestone 8 |

Once the sleep floor is fixed, **battery life is governed by how often you personally wake the
device**, not by a background refresh cycle — see §5, no timer-driven wake exists in this design.

---

## 4. System architecture

```
AI coding tools (Codex, Claude, ...)
        |
        v
CodexBar CLI  (`codexbar dashboard --pretty`, one-shot JSON, no server)
        |
        v
Host BLE bridge (Python + bleak)          <-- runs continuously on the host, not power-constrained
  - background loop: refresh cache every N seconds (config, default 300)
  - BLE central: scans for the device, connects, writes the cached snapshot, disconnects
        |
        | BLE (not Wi-Fi/HTTP — no port ever opens)
        v
T-Display-S3 firmware (NimBLE peripheral)
  - deep-sleeps by default; wakes only on button press (no RTC timer wake)
  - on wake: advertises briefly, accepts one write from the bridge, renders, then either
    stays awake (idle timer running) or goes back to sleep
        |
        v
Provider screens (LVGL, one provider per screen, button-cycled)
```

---

## 5. Power states & wake model

Two states only — no light-sleep-while-connected, no RTC timer wake.

| State | Entered by | Display | BLE | CPU |
|---|---|---|---|---|
| **Awake** | any button press (including the wake edge itself) | on | advertises, accepts one write from the bridge | active |
| **Deep sleep** | idle timeout (no button activity, e.g. 20–30s, configurable), or BOOT pressed while awake | off, rail cut | off | deep sleep, `EXT1` wake on GPIO0 **or** GPIO14 |

Waking is automatic and free — pressing either button while asleep triggers `EXT1` wake, no
firmware logic required for "wake" itself, only for what happens next.
`esp_sleep_get_ext1_wakeup_status()` tells you which pin fired, so wake behavior can differ
(e.g., waking via KEY jumps straight to the next screen; waking via BOOT just resumes the last
screen).

---

## 6. Button behavior

| Button | Pin | Awake | Asleep |
|---|---|---|---|
| KEY | GPIO14 | short press = next provider screen (resets idle timer); long press = force a BLE resync now | wakes the device |
| BOOT | GPIO0 | press = sleep now | wakes the device |
| RST | — | hardware reset only, not programmable | hardware reset only |

Short vs. long press can't be measured while asleep (deep sleep only sees the wake edge) —
classify it after waking by sampling how long the pin stays held (a genuine long-press is still
physically held ~300–500ms after the wake ISR runs).

---

## 7. BLE protocol

**Roles:** T-Display-S3 = GATT **peripheral**/server. Host bridge = **central**/client. The host
does the scanning (it isn't battery-constrained); the device just needs to be found and written
to when it happens to be awake.

**Service** (replace placeholder UUIDs with generated ones before shipping):

| Name | UUID (placeholder) | Properties |
|---|---|---|
| TokenSlate service | `7a2a0001-6b5f-4a9e-9c9d-1f2e3a4b5c6d` | — |
| Snapshot characteristic | `7a2a0002-6b5f-4a9e-9c9d-1f2e3a4b5c6d` | Write (with response) |

Use **Write Request** (not Write-Without-Response) — the ATT-level acknowledgment is enough
delivery confirmation for this use case; no fragmentation/ACK protocol is needed unless the
provider count grows large enough to exceed one MTU.

Negotiate MTU up to 247 bytes on connect (usable payload ~240 bytes after ATT overhead) —
comfortably fits a handful of providers in one write.

**Payload** — compact JSON, short keys to save bytes:

```json
{
  "v": 1,
  "t": 1785508800,
  "p": [
    { "i": "codex",  "p1": 28, "p2": 41, "r1": 1785527700, "r2": 1785873600, "s": 0 },
    { "i": "claude", "p1": 12, "p2": 63, "r1": 1785484800, "r2": 1785960000, "s": 0 }
  ]
}
```

- `i` provider id, `p1`/`p2` primary/secondary window used-%, `r1`/`r2` reset epoch (nullable),
  `s` status (0 = ok, 1 = stale, 2 = error).
- Device parses this with ArduinoJson and caches the resulting struct array in RTC memory so it
  survives deep sleep and redraws instantly on wake, before any fresh sync completes.

---

## 8. Firmware design

**Stack:** PlatformIO + Arduino framework, `NimBLE-Arduino` (lighter than default Bluedroid,
Espressif's own `bluetooth/nimble/power_save` example targets this exact use case), TFT_eSPI or
LVGL for UI, `ArduinoJson` v7 for payload parsing, `OneButton` for debounced short/long press,
`Preferences` (NVS) for persisted settings.

**Boot flow:**

```
setup():
  wake_reason = esp_sleep_get_wakeup_cause()
  digitalWrite(PIN_POWER_ON, HIGH)
  digitalWrite(PIN_LCD_BL, HIGH)
  tft.begin(); init BLE peripheral; start advertising
  restore cached snapshot + screen index from RTC memory
  render current screen immediately (from cache — instant, no network wait)
  start idle timer

loop():
  on BLE write to snapshot characteristic:
    parse JSON -> update cached struct (also mirror into RTC memory)
    re-render current screen, mark state "live"
  button1.tick() / button2.tick():
    KEY short press  -> advance screen index, re-render, reset idle timer
    KEY long press   -> (re)advertise / nudge bridge to reconnect+resync, reset idle timer
    BOOT press       -> call enter_sleep() immediately
  if idle_timeout exceeded -> call enter_sleep()

enter_sleep():
  persist screen index + snapshot to RTC memory
  stop BLE advertising
  tft.writecommand(0x10)                 // SLPIN — see §3, do not omit
  digitalWrite(PIN_LCD_BL, LOW)
  digitalWrite(PIN_POWER_ON, LOW)
  esp_sleep_enable_ext1_wakeup(BIT(GPIO0)|BIT(GPIO14), ESP_EXT1_WAKEUP_ANY_LOW)
  esp_deep_sleep_start()
```

**UI:** landscape orientation (rotate 90° from native portrait — reads better as a desk display),
one provider per screen. Each screen: colored provider dot + name top-left, connection/last-sync
indicator top-right, large radial ring for the primary window (session) with numeric % in the
center, a horizontal bar for the secondary window (weekly) with reset countdown text below it,
page-dot indicator at the bottom showing position among configured providers. Visually distinct
states for live / stale / reconnecting / provider-error — never show a frozen stale number as if
it were current.

---

## 9. Host bridge — CLI design

**Package:** `tokenslate`, installable via `pip install -e bridge/` (a `pyproject.toml` entry point
exposes the `tokenslate` command). **Stack:** Python 3, `click` for the CLI, `bleak` for BLE
(cross-platform), `pyyaml` for config. Shells out to the `codexbar` CLI only — no `codexbar
serve`, no open port, strictly local.

### Commands

| Command | Purpose |
|---|---|
| `tokenslate run` | Foreground process: runs the background CodexBar refresh loop and the BLE scan/connect/write loop together, writing state to disk as it goes. This is what the installed service actually executes. |
| `tokenslate service install` | Generate and install a launchd (macOS) or systemd user unit that runs `tokenslate run` at login/boot |
| `tokenslate service uninstall` | Remove the installed unit |
| `tokenslate service start` / `stop` / `restart` | Wrap the underlying `launchctl` / `systemctl` calls |
| `tokenslate service status` | Whether the service is installed, running, and for how long |
| `tokenslate providers list` | Shell out to `codexbar config providers`; print what CodexBar has available/enabled |
| `tokenslate providers select` | Interactive multi-select over that list; writes the chosen subset to `config.yaml`'s `providers:` key |
| `tokenslate config show` | Print the resolved config |
| `tokenslate config set-refresh-interval <seconds>` | Update `refresh_interval_seconds` |
| `tokenslate config set-device-name <name>` | Update the BLE device name/prefix the bridge scans for |
| `tokenslate status` | Device connection state, last successful sync timestamp, cached provider count/age |

### Config file

```yaml
# config.yaml
refresh_interval_seconds: 300     # 5 min default; tokenslate config set-refresh-interval
providers: [codex, claude]        # tokenslate providers select
device_name_prefix: "TokenSlate-"   # tokenslate config set-device-name
```

### `tokenslate run` — the two loops

```
background_refresh_loop():        # independent of device connection state
  loop forever:
    raw = run("codexbar dashboard --pretty")
    cache = slim(raw, config.providers)   # keep only id, primary/secondary %, resetAt, status
    state.write(last_sync_at=now, last_sync_status="ok", cached_providers=config.providers)
    sleep(config.refresh_interval_seconds)

ble_loop():
  loop forever:
    device = scan_for(config.device_name_prefix)   # waits for the device to advertise (i.e. to be awake)
    if device found:
      connect(device)
      write(snapshot_characteristic, json.dumps(cache).encode())
      state.write(last_seen_device_at=now)
      disconnect()
    # loop continues scanning — cheap on a host machine, not power-constrained
```

### State file (backs `tokenslate status`)

`tokenslate run` writes its live state to `~/.config/tokenslate/state.json` on every cycle — this is
how the separate, short-lived `tokenslate status` invocation sees what the long-running process is
doing, without needing IPC:

```json
{
  "last_seen_device_at": "2026-08-24T10:03:12Z",
  "last_sync_at": "2026-08-24T10:03:14Z",
  "last_sync_status": "ok",
  "cached_providers": ["codex", "claude"],
  "cache_age_seconds": 42
}
```

`tokenslate status` reads this file and renders it, with a distinct "never synced yet" state if the
file doesn't exist.

### `tokenslate providers select` flow

```
raw = run("codexbar config providers")   # what CodexBar has, enabled/available
chosen = interactive_multiselect(raw)     # user picks the subset to put on the device screen
config.providers = chosen
config.save()
```

`tokenslate service install` should be the one to make the bridge always-available — a user
shouldn't need to remember to start it before the device can sync.

---

## 10. Repo layout

```
tokenslate-tdisplay-s3/
├── firmware/                          # PlatformIO project
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp
│       ├── ble/gatt_service.cpp .h    # NimBLE peripheral, snapshot characteristic handler
│       ├── power/sleep_states.cpp .h  # idle timer, EXT1 config, the SLPIN sleep sequence
│       ├── ui/screens.cpp .h          # LVGL/TFT_eSPI provider card rendering
│       ├── ui/theme.h                 # per-provider colors
│       ├── input/buttons.cpp .h       # OneButton wiring, short/long press classification
│       └── storage.cpp .h             # RTC_DATA_ATTR snapshot + screen-index persistence
└── bridge/                            # host-side, installable as the `tokenslate` CLI
    ├── pyproject.toml                 # entry point: tokenslate = tokenslate.cli:main
    ├── requirements.txt               # bleak, click, pyyaml
    ├── config.yaml
    └── tokenslate/
        ├── __init__.py
        ├── cli.py                     # click command group; wires up all subcommands
        ├── ble.py                     # scan/connect/write loop
        ├── codexbar.py                # wraps `codexbar dashboard` / `codexbar config providers`
        ├── config.py                  # load/save config.yaml
        ├── service.py                 # launchd/systemd generate/install/uninstall/status
        └── state.py                   # read/write ~/.config/tokenslate/state.json
```

---

## 11. Build milestones

0. **Power sanity check first.** Flash a minimal sketch that does exactly the §3 sleep sequence
   (SLPIN, rail cut, `EXT1` wake, no other peripherals active) and measure deep-sleep current on
   battery power with a multimeter/USB power meter. Target: low hundreds of µA. Don't proceed to
   app logic until this number is confirmed — it's the number the rest of the power budget
   depends on.
1. **Display bring-up.** LVGL/TFT_eSPI demo running on battery power (not just USB); confirm
   `PIN_POWER_ON`/backlight sequencing works on cold boot and after wake-from-sleep.
2. **BLE peripheral skeleton.** NimBLE service + snapshot characteristic advertising; confirm
   discoverable with a generic BLE scanner (e.g. nRF Connect) before wiring up the bridge.
3. **Host bridge CLI skeleton.** `tokenslate` package installable via `pip install -e`; `run`
   command doing config loading, `codexbar dashboard` invocation + slimming, the background
   refresh loop, and a connect-and-write cycle against the Milestone 2 skeleton; `providers`,
   `config`, and `status` commands wired to a real `config.yaml` and state file; `service
   install`/`status` tested against your actual OS (launchd or systemd).
4. **Screen UI.** Build the provider card screen against hardcoded placeholder data.
5. **Wire it live.** Connect the bridge's payload into the UI via the BLE write handler.
   End-to-end check: bridge refreshes its cache every 5 minutes; device wakes on a button press,
   connects, and renders real numbers within a couple seconds.
6. **Power states & buttons.** Idle timeout, BOOT sleep-now, KEY short/long press classification,
   `EXT1` wake-source dispatch, RTC-memory persistence across sleep cycles.
7. **Robustness.** Stale/error/reconnecting visuals; bridge-side reconnect/backoff handling if
   the device isn't found on a scan pass.
8. **Battery validation.** Run for several real days, log actual current draw per state, and
   compare against the §3 budget. Tune idle timeout if the awake-cycle cost is higher than
   expected — this is now the main lever on battery life, not the sleep floor.

---

## 12. Configuration defaults & open items

| Setting | Default | Notes |
|---|---|---|
| Refresh interval (bridge) | 300s (5 min) | `tokenslate config set-refresh-interval`; lives on the host, not in firmware |
| Idle timeout before sleep | 20–30s | Tune after Milestone 8 |
| Providers | `codex`, `claude` | `tokenslate providers select`; both have local/CLI/OAuth CodexBar sources with no extra API key |
| Battery capacity | not yet specified | Confirm cell/mAh to sanity-check the §3 budget table |
| Board variant | assumed plain, non-touch, non-AMOLED | Confirm — affects display driver/pin_config only |
| Build tool | PlatformIO | Assumed; swap for Arduino IDE if preferred, no other changes needed |

---

## 13. References

- [Xinyuan-LilyGO/T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3) — board repo, pin_config, TFT_eSPI setup
- [Deep sleep power issue #289](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/289) and the fix commits [4b2edc1](https://github.com/Xinyuan-LilyGO/T-Display-S3/commit/4b2edc1e5df2d370173b4758c128c4b8ca4f5c1e) / [c9fd58a](https://github.com/Xinyuan-LilyGO/T-Display-S3/commit/c9fd58a18d01d0c7663496afa1b0eb2a2aaa5676)
- [steipete/CodexBar](https://github.com/steipete/CodexBar) — usage data source, CLI docs at `docs/cli.md`
- `h2zero/NimBLE-Arduino` — BLE stack
- `bleak` — Python cross-platform BLE library for the host bridge

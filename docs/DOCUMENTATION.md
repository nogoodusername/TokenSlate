# TokenSlate — LilyGO T-Display-S3, BLE, battery-powered

A desk/pocket display that shows live AI coding-tool usage (Codex, Claude, etc.) pulled from
CodexBar, on a battery-powered T-Display-S3, synced over Bluetooth LE instead of Wi-Fi.

This document is a complete, implementation-ready handoff — everything decided during design
is captured here so a fresh session (human or Claude Code) can build from it without needing
the original conversation.

---

## 1. Goal & constraints

- Show per-provider usage (session %, weekly %, reset countdown) on a small TFT screen.
- On-screen battery percentage and an indicator for when the device is running on USB power (see §14).
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
| GPIO4 | battery ADC | Battery voltage (only readable when USB is unplugged — see §14) |
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

**Also watch out for:** on this board, `Serial` (native USB-CDC) blocks on write/flush when no
USB host has the port open — e.g. running on battery with no cable attached. Guard every `Serial`
call with `if (Serial) { ... }`, or a call inside the sleep routine can hang the firmware
indefinitely before it ever reaches the SLPIN/rail-cut sequence above. Confirmed on hardware
during Milestone 0 — see `firmware/src/main.cpp`.

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
indicator plus battery/USB-power icon top-right (see §14), large radial ring for the primary
window (session) with numeric % in the center, a horizontal bar for the secondary window (weekly)
with reset countdown text below it, page-dot indicator at the bottom showing position among
configured providers. Visually distinct states for live / stale / reconnecting / provider-error —
never show a frozen stale number as if it were current.

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
│       ├── power/battery.cpp .h       # ADC read, voltage->percent, USB-present heuristic (§14)
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

0. **Power sanity check first — done.** Flashed a minimal sketch (`firmware/src/main.cpp`) that
   does the §3 sleep sequence (SLPIN, rail cut, `EXT1` wake, no other peripherals active) and
   confirmed on hardware, on battery power: display init, 10s awake window, deep sleep entry
   (USB drops, screen off), and button wake all work correctly. Also found and fixed a
   battery-only-specific bug — `Serial` blocks on native USB-CDC with no host attached, see §3.
   The actual current measurement (multimeter/USB power meter, target low hundreds of µA) was
   **not done** — skipped for now; revisit before Milestone 8's battery validation if it still
   hasn't been measured by then.
1. **Display bring-up — done.** TFT_eSPI demo confirmed on battery power: `PIN_POWER_ON`/backlight
   sequencing works correctly on cold boot and after wake-from-sleep. Firmware split into
   `board_pins.h`, `power/battery.cpp .h`, `power/sleep_states.cpp .h`, `ui/screens.cpp .h`
   modules (§10). Battery ADC read (`power/battery.cpp`, GPIO4, §14) is wired into the boot flow
   and produces correct voltage/percentage readings.
   **Known open issue:** with USB attached, deep-sleep entry intermittently freezes after the
   ADC is read that boot — reproduced identically across three different ADC read APIs
   (`analogRead`, `analogReadMilliVolts`, direct esp-idf `adc1_get_raw`), so it isn't specific to
   one API. A live on-screen tick counter confirmed the CPU stays fully alive through the freeze
   window and the freeze happens specifically inside/around `enterSleep()`. Critically, the
   freeze reproduces only when a USB host is attached but **not actively reading** the serial
   port — with an active monitor attached (even just polling), the exact same firmware cycles
   through sleep/wake reliably every time. Points to a native-USB-CDC stack issue on this board
   (already flaky in several unrelated ways during Milestone 0), not an application bug; not
   resolved further without a hardware debugger (JTAG) or a second physical unit to rule out a
   board-specific fault. Does not appear to affect battery-only operation.
   `USB_PRESENT_THRESHOLD_V` (§14) still needs calibrating against a multimeter on your actual
   unit — the ESP32-S3 ADC is imprecise enough that the starting constant shouldn't be trusted
   blind.
2. **BLE peripheral skeleton — done.** Service + snapshot characteristic advertising
   (`firmware/src/ble/gatt_service.cpp .h`), confirmed on hardware with the `blew` macOS BLE CLI:
   device discoverable as `TokenSlate-`, service/characteristic UUIDs match §7 exactly, connect
   → write → `onWrite` fires with the correct byte count → disconnect → re-advertising resumes
   automatically. **Deviates from §8's original plan:** uses the framework's built-in Bluedroid
   BLE stack (`BLEDevice.h` etc.) instead of NimBLE-Arduino — matches the proven-working
   reference on this exact board (`T-Display-S3-PC-HW-Monitor`) rather than risking another
   from-scratch BLE bring-up debugging cycle. Revisit NimBLE for its lower power footprint once
   this path is proven end-to-end, folded into Milestone 8's battery validation. This milestone's
   firmware stays awake indefinitely (no idle timeout) so it can be found/connected to at a
   scanner's pace; BOOT still sleeps immediately per §6.
3. **Host bridge CLI skeleton — done.** `tokenslate` package (`bridge/`) installable via
   `pip install -e bridge/`; all commands (`run`, `service *`, `providers list/select`,
   `config show/set-*`, `status`) working against a real `config.yaml` and state file.
   Confirmed end-to-end on hardware: `tokenslate run` and the `launchd`-installed background
   service both fetch real `codexbar dashboard` data, slim it, and successfully write to the
   Milestone 2 device over BLE, repeatedly.
   **Deviates from §9's original assumptions**, since those were written before checking the
   actual installed CodexBar CLI:
   - The real command is `codexbar dashboard --timeout N` (not `codexbar dashboard --pretty`
     alone — `--pretty` still works, just adds formatting) and `codexbar config dump` (not
     `codexbar config providers`, which doesn't exist).
   - The real payload schema has a variable-length `windows[]` per provider (e.g. claude has
     session+weekly, cursor/commandcode have session+weekly+tertiary), not the fixed
     primary/secondary pair §7 assumed. `codexbar.slim()` currently maps the first two windows
     onto `p1`/`p2` as a reasonable approximation — revisit the wire protocol properly in
     Milestone 5 once real per-provider window semantics need to be shown correctly on screen.
   Two real bugs found and fixed along the way:
   - **BLE device-name caching**: matching on `BLEDevice.name` (bleak/macOS) returned a stale
     cached name from some prior connection to the same physical board instead of the live
     advertisement — fixed by matching on `AdvertisementData.local_name` instead
     (`bridge/tokenslate/ble.py`). Worth remembering if a scan-based tool ever again reports the
     "wrong" name for this hardware.
   - **launchd PATH**: launchd services run with a minimal PATH that excludes Homebrew's
     `/opt/homebrew/bin`, so the installed service couldn't find `codexbar` at all until
     `codexbar.py` resolved it explicitly via `shutil.which()` with fallback paths.

   **macOS companion app (`bridge/macapp/`) — added after the base plan.** A `rumps` menu bar
   app wrapping the same refresh + BLE loops, run in-process (not shelled out to the `tokenslate`
   CLI) specifically so macOS's Bluetooth permission prompt attributes to "TokenSlate" instead of
   a bare `python3` interpreter. Build with `cd bridge/macapp && python3 setup.py py2app`
   (requires `pip install -e bridge/` first, plus `rumps`/`py2app`). Two bugs found:
   `rumps.MenuItem(callback=...)` doesn't fire once bundled by py2app — use the
   `@rumps.clicked("label")` decorator instead; and `subprocess.run()` needs `encoding="utf-8"`
   explicitly, since py2app bundles run with `locale=C/POSIX` by default. The built app is only
   ad-hoc signed (no paid Apple Developer ID / notarization), so first launch needs a right-click
   → Open in Finder to get past Gatekeeper — plain `open`/double-click and direct binary
   execution both fail silently otherwise. CI: `.github/workflows/macapp-release.yml` builds and
   ad-hoc-signs the app on every `v*.*.*` tag push and attaches the zipped bundle to a GitHub
   Release (`workflow_dispatch` also builds, uploading a workflow artifact instead for testing
   without cutting a release).

   **Device name is configurable in the app** (not just via `tokenslate config
   set-device-name`) — "Set Device Name..." (text prompt) and "Scan for Devices..." (live BLE
   discovery, builds a "Found Devices" submenu of everything currently advertising; clicking one
   sets it) are both in the menu, since the advertised name is expected to change from
   `"TokenSlate-"` in the future. The scan-result submenu's items use `.set_callback()` rather
   than the `@rumps.clicked` decorator (which needs a title known at decoration time) — not yet
   confirmed this works in a py2app bundle the same way `@rumps.clicked` does, given the decorator
   bug already found here; verify on hardware before relying on it.

   **BLE scanning (and therefore the permission prompt) now starts immediately on launch**, not
   after the first CodexBar fetch completes. Both `run_ble_loop` (CLI) and the app's `_ble_loop`
   used to gate the entire scan behind having a payload ready to write, which delayed the
   permission prompt by however long that first fetch took (`codexbar dashboard` defaults to a
   30s timeout). Fixed by decoupling scanning from writing in `ble.py`
   (`find_device`/`write_to_device` as separate steps) — the scan (and permission trigger) now
   happens every pass regardless of payload state; only the write is skipped if there's nothing
   to send yet.
4. **Screen UI — done.** Provider card layout confirmed on hardware against 3 hardcoded
   providers covering live/stale/error states (`ui/screens.cpp`: `renderProviderCard()`,
   `ui/theme.h` for per-provider colors). Colored dot + name top-left, connection status
   top-right, battery/USB indicator top-center (moved there from top-right after visual
   review — reads better alongside the status text), session % as a radial ring
   (`TFT_eSPI::drawArc`) with the number centered inside, weekly % as a horizontal bar with
   reset countdown text below it, page dots at the bottom. KEY cycles providers, BOOT still
   sleeps immediately (§6). No idle-timeout auto-sleep in this milestone's firmware — that's
   Milestone 6.
5. **Wire it live — done.** Real BLE snapshot writes now parse (ArduinoJson v7) and render as
   provider cards, confirmed end-to-end on hardware with 3 real providers (claude/cursor/
   commandcode) cycling correctly via KEY. Two real bugs found and fixed:
   - **JSON parsing was in the BLE write callback's interrupt context.** Moved to `main.cpp`'s
     loop instead (`ble/gatt_service.cpp` now just copies bytes and sets a dirty flag,
     `bleHasNewSnapshot()`/`bleConsumeSnapshotJson()`), matching the pattern already documented
     in the `T-Display-S3-PC-HW-Monitor` reference ("this runs in interrupt context - keep it
     minimal").
   - **`BLE_SNAPSHOT_MAX_LEN` was sized for docs §7's single-MTU estimate (~240B), not for
     however many providers are actually configured.** A 3-provider payload is 272 bytes and got
     silently rejected. Bluedroid reassembles a long characteristic write into one `onWrite()`
     call regardless of how many ATT PDUs it took, so the real ceiling is provider count, not
     MTU size — bumped the buffer to 1024B (comfortably fits `MAX_PROVIDERS` = 8).
   - Also found (not a firmware bug): the macOS companion app only loaded `config.yaml` once at
     launch, so provider/config changes made via the CLI while the app was already running never
     took effect without a manual relaunch. Fixed by reloading config from disk at the top of
     every refresh cycle in `tokenslate_app.py`.
   Still a known limitation: `p1`/`p2` only cover a provider's first two `windows[]` (see
   Milestone 3's note) — revisit once providers with 3 windows (cursor, commandcode) need their
   full data shown, not just the first two.

   Also fixed: the battery/USB indicator was only sampled once at boot (before `initDisplay()`,
   per §3), so it went stale the moment the power source changed without a reboot (e.g.
   unplugging USB left it stuck showing "USB"). `main.cpp`'s loop now re-samples every 5s and
   re-renders. This is the first time the ADC has been read *repeatedly* while USB is attached
   (every prior test only ever called it once at boot) — confirmed stable on hardware over a 25s
   window with USB connected, no repeat of the Milestone 1 USB-attached hang, but flag it if
   instability ever resurfaces since that bug was never actually root-caused, only avoided.
6. **Power states & buttons — done.** Confirmed on hardware:
   - `storage.cpp .h` (new, per repo layout): `ProviderCardData[]` + reset-text buffers +
     provider count/current page all moved to `RTC_DATA_ATTR`, so the last-known screen redraws
     instantly on wake, before any fresh BLE sync arrives. Only lost on a true power-on reset.
   - Idle timeout: 25s of no button activity (§12 default range) triggers the same `enterSleep()`
     as BOOT. Confirmed firing right on schedule in a clean test window.
   - `EXT1` wake-source dispatch (`sleep_states.h`'s `getWakeSource()`): waking via KEY advances
     to the next screen, waking via BOOT (or a plain power-on) just resumes whatever was last
     shown, per §5.
   - KEY short/long press classification: measures actual hold duration (not just a fixed
     debounce) -- short press cycles screens, long press (≥500ms) calls the new
     `bleForceReadvertise()` (disconnects any lingering connection + restarts advertising, so the
     bridge's next scan pass finds the device sooner rather than waiting out the rest of the
     current interval — there's no separate "request" characteristic in this protocol, see §7).
   - Reconfirmed the spurious-wake quirk noted since Milestone 0/1: the device occasionally wakes
     with no button press logged, most likely BOOT (GPIO0) getting bumped during USB cable
     handling since it sits right next to the connector — not a regression, already priced into
     the design (EXT1 wake is "free" either way, per §5).
7. **Robustness.** Stale/error/reconnecting visuals; bridge-side reconnect/backoff handling if
   the device isn't found on a scan pass.
8. **Battery validation.** Run for several real days, log actual current draw per state, and
   compare against the §3 budget. Tune idle timeout if the awake-cycle cost is higher than
   expected — this is now the main lever on battery life, not the sleep floor. If the Milestone 0
   deep-sleep current was never measured, measure it here at the latest.

---

## 12. Configuration defaults & open items

| Setting | Default | Notes |
|---|---|---|
| Refresh interval (bridge) | 300s (5 min) | `tokenslate config set-refresh-interval`; lives on the host, not in firmware |
| Idle timeout before sleep | 20–30s | Tune after Milestone 8 |
| Providers | `codex`, `claude` | `tokenslate providers select`; both have local/CLI/OAuth CodexBar sources with no extra API key |
| Battery capacity | not yet specified | Confirm cell/mAh to sanity-check the §3 budget table |
| USB-present ADC threshold | ~4.4V (placeholder) | Must calibrate per-unit, see §14 and Milestone 1 |
| Low-battery warning threshold | 15% | Icon switches to warning color below this, see §14 |
| Board variant | assumed plain, non-touch, non-AMOLED | Confirm — affects display driver/pin_config only |
| Build tool | PlatformIO | Assumed; swap for Arduino IDE if preferred, no other changes needed |

---

## 13. References

- [Xinyuan-LilyGO/T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3) — board repo, pin_config, TFT_eSPI setup
- [Deep sleep power issue #289](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/289) and the fix commits [4b2edc1](https://github.com/Xinyuan-LilyGO/T-Display-S3/commit/4b2edc1e5df2d370173b4758c128c4b8ca4f5c1e) / [c9fd58a](https://github.com/Xinyuan-LilyGO/T-Display-S3/commit/c9fd58a18d01d0c7663496afa1b0eb2a2aaa5676)
- [steipete/CodexBar](https://github.com/steipete/CodexBar) — usage data source, CLI docs at `docs/cli.md`
- `h2zero/NimBLE-Arduino` — BLE stack
- `bleak` — Python cross-platform BLE library for the host bridge
- [T-Display-S3 issue #254](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/254) — no dedicated charge/VBUS pin exists
- [T-Display-S3 issue #230](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/230) — `BAT_ADC` divider behavior on battery vs. USB power

---

## 14. Battery meter & charging indicator

Added after the base plan — extends §1, §2, §8, §10, §11, §12 above rather than replacing them.

**Hardware constraint — read this before wiring anything up.** There is no pin exposed for
charge/VBUS status on this board; this has been asked and left unresolved upstream:
[Xinyuan-LilyGO/T-Display-S3#254](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/254).
The board does have a physical charge-status LED next to the USB-C port (flashes/dim = no
battery, solid = charging, off = charge complete), but it's wired directly to the charging IC,
not to any ESP32 GPIO — firmware cannot read it, and there's no way to reproduce that exact
tri-state distinction on screen without a hardware modification.

**`GPIO4` (`BAT_ADC`) behaves differently depending on power source** — per LilyGO's README and
confirmed independently in [#230](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/230):

- **On battery only:** reads true battery voltage through a ~2:1 divider — multiply the ADC
  voltage by 2.
- **On USB power:** no longer reflects the battery at all — reads roughly half of VBUS instead
  (~2.5V raw, ~5V once doubled). That's always higher than any real single-cell LiPo (max
  ~4.2V), which is exactly what makes it usable as a "USB present" signal instead.

**Design consequence:** the device can show an accurate battery percentage, or an accurate
"running on USB power" state, but **cannot distinguish "actively charging" from "on USB, battery
already full"** in software — that distinction only exists on the physical LED. Represent this
honestly on screen: a plug icon meaning "on external power," not a charging-bolt icon implying a
distinction the hardware doesn't actually expose.

### Detection logic

```cpp
float readBatteryVoltage() {
  int raw = analogRead(PIN_BAT_ADC);           // GPIO4
  return (raw / 4095.0f) * 3.3f * 2.0f;         // 2:1 divider
}

PowerState readPowerState() {
  float v = readBatteryVoltage();
  if (v > USB_PRESENT_THRESHOLD_V)              // e.g. 4.4V — no real LiPo reads this high
    return POWER_ON_USB;
  return POWER_ON_BATTERY;
}
```

`USB_PRESENT_THRESHOLD_V` needs calibrating against your actual unit — the ESP32-S3 ADC is
known to be imprecise and non-linear, not just a datasheet footnote (see the readings in
[#190](https://github.com/Xinyuan-LilyGO/T-Display-S3/issues/190)). Check it against a multimeter
during bring-up rather than trusting a single hardcoded constant — folded into Milestone 1 above.

### Voltage → percentage

Single-cell LiPo discharge curve — a reasonable starting table; recalibrate against your actual
battery if it drifts:

| Voltage | % |
|---|---|
| 4.20V | 100 |
| 3.98V | 80 |
| 3.87V | 60 |
| 3.79V | 40 |
| 3.68V | 20 |
| 3.45V | 0 (cutoff, before under-voltage protection) |

Linearly interpolate between rows.

### UI treatment

Lives in the header row already present on every provider screen (§8), next to the
connection/last-sync indicator:

- **On battery:** battery-fill icon + percentage. Below the low-battery threshold (default 15%,
  §12), the icon switches to a warning color — the same "never show a misleading number as if it
  were fine" principle already applied to stale provider data.
- **On USB:** a plug icon, no percentage — `GPIO4` isn't trustworthy here, so don't fabricate one.

### Firmware module

`power/battery.cpp .h` (§10) — owns `readBatteryVoltage()`, `readPowerState()`, the
voltage→percent table, and the low-battery threshold check. Read once per screen render (on wake,
on screen cycle, and once per idle-timeout tick if the icon should update while sitting awake) —
cheap enough not to need its own timer. 

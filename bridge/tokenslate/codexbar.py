from __future__ import annotations

import json
import shutil
import subprocess
import time
from datetime import datetime, timezone
from typing import Any

# launchd/systemd services run with a minimal PATH that usually excludes
# Homebrew's install location -- fall back to common locations if
# shutil.which() (the user's shell PATH) doesn't find it.
_FALLBACK_PATHS = ["/opt/homebrew/bin/codexbar", "/usr/local/bin/codexbar"]


class CodexBarError(RuntimeError):
    pass


def _codexbar_path() -> str:
    found = shutil.which("codexbar")
    if found:
        return found
    for path in _FALLBACK_PATHS:
        if shutil.which(path):
            return path
    raise CodexBarError("codexbar CLI not found on PATH or in " + ", ".join(_FALLBACK_PATHS))


def _run(args: list[str], timeout: float) -> str:
    try:
        result = subprocess.run(
            [_codexbar_path(), *args],
            capture_output=True,
            text=True,
            timeout=timeout + 5,
        )
    except FileNotFoundError as e:
        raise CodexBarError("codexbar CLI not found on PATH") from e
    except subprocess.TimeoutExpired as e:
        raise CodexBarError(f"codexbar {' '.join(args)} timed out") from e
    if result.returncode != 0:
        raise CodexBarError(f"codexbar {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def fetch_dashboard(timeout: float = 30) -> dict[str, Any]:
    """One-shot dashboard-v1 snapshot. See CodexBar docs/dashboard-api.md."""
    out = _run(["dashboard", "--timeout", str(int(timeout))], timeout=timeout)
    return json.loads(out)


def list_providers() -> list[dict[str, Any]]:
    """All providers CodexBar knows about, enabled or not (codexbar config dump)."""
    out = _run(["config", "dump"], timeout=15)
    data = json.loads(out)
    return data.get("providers", [])


def _epoch(iso_ts: str | None) -> int | None:
    if not iso_ts:
        return None
    try:
        dt = datetime.strptime(iso_ts, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        return int(dt.timestamp())
    except ValueError:
        return None


def slim(dashboard: dict[str, Any], provider_ids: list[str]) -> dict[str, Any]:
    """Compact BLE payload per docs §7.

    NOTE: the real dashboard schema has a variable-length `windows[]` per
    provider (session/weekly/tertiary, etc.), not the fixed two-window
    p1/p2 shape docs §7 originally assumed. This maps the first two
    windows onto p1/p2 as a reasonable approximation for Milestone 3's
    "invocation + slimming" skeleton -- the final wire protocol (handling
    providers with >2 windows, e.g. cursor/commandcode) is a Milestone 5
    design decision, not settled here.
    """
    wanted = set(provider_ids)
    out_providers = []
    for p in dashboard.get("providers", []):
        if p.get("id") not in wanted:
            continue
        windows = p.get("windows") or []
        w1 = windows[0] if len(windows) > 0 else {}
        w2 = windows[1] if len(windows) > 1 else {}
        status = 2 if p.get("error") else 0
        out_providers.append({
            "i": p.get("id"),
            "p1": w1.get("usedPercent"),
            "p2": w2.get("usedPercent"),
            "r1": _epoch(w1.get("resetAt")),
            "r2": _epoch(w2.get("resetAt")),
            "s": status,
        })
    return {
        "v": 1,
        "t": int(time.time()),
        "p": out_providers,
    }

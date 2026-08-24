from __future__ import annotations

import platform
import shutil
import subprocess
import sys
from pathlib import Path

LABEL = "com.tokenslate.bridge"


def _tokenslate_executable() -> str:
    exe = shutil.which("tokenslate")
    if exe:
        return exe
    # Fallback: run as a module with the current interpreter, in case the
    # console-script entry point isn't on PATH (e.g. a venv not activated
    # in the shell that installed the launchd/systemd unit).
    return sys.executable


# --- macOS (launchd) --------------------------------------------------

def _launchd_plist_path() -> Path:
    return Path.home() / "Library" / "LaunchAgents" / f"{LABEL}.plist"


def _launchd_log_dir() -> Path:
    d = Path.home() / "Library" / "Logs" / "tokenslate"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _install_launchd() -> str:
    exe = _tokenslate_executable()
    log_dir = _launchd_log_dir()
    program_args = [exe, "run"] if exe != sys.executable else [exe, "-m", "tokenslate.cli", "run"]
    args_xml = "\n".join(f"        <string>{a}</string>" for a in program_args)
    plist = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>{LABEL}</string>
    <key>ProgramArguments</key>
    <array>
{args_xml}
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>{log_dir}/stdout.log</string>
    <key>StandardErrorPath</key>
    <string>{log_dir}/stderr.log</string>
</dict>
</plist>
"""
    path = _launchd_plist_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(plist)
    subprocess.run(["launchctl", "load", "-w", str(path)], check=True)
    return f"installed launchd agent at {path}"


def _uninstall_launchd() -> str:
    path = _launchd_plist_path()
    if path.exists():
        subprocess.run(["launchctl", "unload", str(path)], check=False)
        path.unlink()
        return f"removed {path}"
    return "not installed"


def _start_launchd() -> str:
    subprocess.run(["launchctl", "start", LABEL], check=True)
    return "started"


def _stop_launchd() -> str:
    subprocess.run(["launchctl", "stop", LABEL], check=True)
    return "stopped"


def _restart_launchd() -> str:
    _stop_launchd()
    return _start_launchd()


def _status_launchd() -> str:
    path = _launchd_plist_path()
    if not path.exists():
        return "not installed"
    result = subprocess.run(["launchctl", "list", LABEL], capture_output=True, text=True)
    if result.returncode != 0:
        return "installed, not running"
    return f"installed, running\n{result.stdout.strip()}"


# --- Linux (systemd --user) --------------------------------------------
# Implemented per docs §9 but not exercised on this machine (macOS) --
# verify on a real Linux host before relying on it.

def _systemd_unit_path() -> Path:
    return Path.home() / ".config" / "systemd" / "user" / "tokenslate.service"


def _install_systemd() -> str:
    exe = _tokenslate_executable()
    exec_start = f"{exe} run" if exe != sys.executable else f"{exe} -m tokenslate.cli run"
    unit = f"""[Unit]
Description=TokenSlate BLE bridge

[Service]
ExecStart={exec_start}
Restart=on-failure

[Install]
WantedBy=default.target
"""
    path = _systemd_unit_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(unit)
    subprocess.run(["systemctl", "--user", "daemon-reload"], check=True)
    subprocess.run(["systemctl", "--user", "enable", "--now", "tokenslate.service"], check=True)
    return f"installed systemd user unit at {path}"


def _uninstall_systemd() -> str:
    path = _systemd_unit_path()
    subprocess.run(["systemctl", "--user", "disable", "--now", "tokenslate.service"], check=False)
    if path.exists():
        path.unlink()
        subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)
        return f"removed {path}"
    return "not installed"


def _start_systemd() -> str:
    subprocess.run(["systemctl", "--user", "start", "tokenslate.service"], check=True)
    return "started"


def _stop_systemd() -> str:
    subprocess.run(["systemctl", "--user", "stop", "tokenslate.service"], check=True)
    return "stopped"


def _restart_systemd() -> str:
    subprocess.run(["systemctl", "--user", "restart", "tokenslate.service"], check=True)
    return "restarted"


def _status_systemd() -> str:
    result = subprocess.run(
        ["systemctl", "--user", "status", "tokenslate.service"],
        capture_output=True, text=True,
    )
    return result.stdout.strip() or result.stderr.strip()


# --- dispatch ------------------------------------------------------------

def _is_macos() -> bool:
    return platform.system() == "Darwin"


def install() -> str:
    return _install_launchd() if _is_macos() else _install_systemd()


def uninstall() -> str:
    return _uninstall_launchd() if _is_macos() else _uninstall_systemd()


def start() -> str:
    return _start_launchd() if _is_macos() else _start_systemd()


def stop() -> str:
    return _stop_launchd() if _is_macos() else _stop_systemd()


def restart() -> str:
    return _restart_launchd() if _is_macos() else _restart_systemd()


def status() -> str:
    return _status_launchd() if _is_macos() else _status_systemd()

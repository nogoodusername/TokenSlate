from __future__ import annotations

import asyncio
import json
from datetime import datetime, timezone

import click

from . import ble, service
from .codexbar import CodexBarError, fetch_dashboard, list_providers, slim
from .config import Config
from .state import State, now_iso


def _log(msg: str) -> None:
    click.echo(f"[tokenslate] {msg}", err=True)


@click.group()
def main() -> None:
    """TokenSlate host bridge -- CodexBar to the T-Display-S3 device over BLE."""


# --- run -------------------------------------------------------------------

@main.command()
def run() -> None:
    """Foreground process: background CodexBar refresh loop + BLE scan/connect/write loop."""
    asyncio.run(_run_async())


async def _run_async() -> None:
    cfg = Config.load()
    cache: dict[str, bytes | None] = {"payload": None}
    stop_event = asyncio.Event()

    async def refresh_loop() -> None:
        while True:
            state = State.load() or State()
            try:
                dashboard = fetch_dashboard()
                payload = slim(dashboard, cfg.providers)
                cache["payload"] = json.dumps(payload).encode()
                state.merge_save(
                    last_sync_at=now_iso(),
                    last_sync_status="ok",
                    cached_providers=cfg.providers,
                )
                _log(f"refreshed cache for {cfg.providers}")
            except CodexBarError as e:
                state.merge_save(last_sync_status=f"error: {e}")
                _log(f"refresh failed: {e}")
            await asyncio.sleep(cfg.refresh_interval_seconds)

    def get_payload() -> bytes | None:
        return cache["payload"]

    def on_write() -> None:
        state = State.load() or State()
        state.merge_save(last_seen_device_at=now_iso())

    _log(f"starting: providers={cfg.providers} refresh={cfg.refresh_interval_seconds}s "
         f"device_name_prefix={cfg.device_name_prefix!r}")
    await asyncio.gather(
        refresh_loop(),
        ble.run_ble_loop(get_payload, cfg.device_name_prefix, on_write, stop_event),
    )


# --- service -----------------------------------------------------------

@click.group(name="service")
def service_cmd() -> None:
    """Install/manage the background service (launchd on macOS, systemd --user on Linux)."""


main.add_command(service_cmd)


@service_cmd.command("install")
def service_install() -> None:
    click.echo(service.install())


@service_cmd.command("uninstall")
def service_uninstall() -> None:
    click.echo(service.uninstall())


@service_cmd.command("start")
def service_start() -> None:
    click.echo(service.start())


@service_cmd.command("stop")
def service_stop() -> None:
    click.echo(service.stop())


@service_cmd.command("restart")
def service_restart() -> None:
    click.echo(service.restart())


@service_cmd.command("status")
def service_status() -> None:
    click.echo(service.status())


# --- providers -----------------------------------------------------------

@main.group()
def providers() -> None:
    """List/select which CodexBar providers show up on the device."""


@providers.command("list")
def providers_list() -> None:
    for p in list_providers():
        marker = "*" if p.get("enabled") else " "
        click.echo(f"[{marker}] {p['id']}")


@providers.command("select")
def providers_select() -> None:
    all_providers = list_providers()
    cfg = Config.load()
    click.echo("Select providers to show on the device (space-separated numbers):\n")
    for i, p in enumerate(all_providers):
        current = " (selected)" if p["id"] in cfg.providers else ""
        enabled = "" if p.get("enabled") else "  [disabled in CodexBar]"
        click.echo(f"  {i:>3}  {p['id']}{current}{enabled}")

    raw = click.prompt("\nNumbers")
    try:
        indices = [int(x) for x in raw.split()]
        chosen = [all_providers[i]["id"] for i in indices]
    except (ValueError, IndexError):
        raise click.ClickException("invalid selection")

    cfg.providers = chosen
    cfg.save()
    click.echo(f"providers set to: {chosen}")


# --- config -----------------------------------------------------------

@main.group()
def config() -> None:
    """View/update bridge configuration."""


@config.command("show")
def config_show() -> None:
    cfg = Config.load()
    click.echo(f"refresh_interval_seconds: {cfg.refresh_interval_seconds}")
    click.echo(f"providers: {cfg.providers}")
    click.echo(f"device_name_prefix: {cfg.device_name_prefix}")


@config.command("set-refresh-interval")
@click.argument("seconds", type=int)
def config_set_refresh_interval(seconds: int) -> None:
    cfg = Config.load()
    cfg.refresh_interval_seconds = seconds
    cfg.save()
    click.echo(f"refresh_interval_seconds set to {seconds}")


@config.command("set-device-name")
@click.argument("name")
def config_set_device_name(name: str) -> None:
    cfg = Config.load()
    cfg.device_name_prefix = name
    cfg.save()
    click.echo(f"device_name_prefix set to {name!r}")


# --- status -----------------------------------------------------------

@main.command()
def status() -> None:
    """Device connection state, last sync, cached provider count/age."""
    state = State.load()
    if state is None:
        click.echo("never synced yet (run `tokenslate run` or `tokenslate service start`)")
        return
    click.echo(f"last seen device at: {state.last_seen_device_at or 'never'}")
    click.echo(f"last sync at:        {state.last_sync_at or 'never'}")
    click.echo(f"last sync status:    {state.last_sync_status or 'unknown'}")
    click.echo(f"cached providers:    {state.cached_providers or []}")
    if state.last_sync_at:
        age = (datetime.now(timezone.utc) - datetime.strptime(
            state.last_sync_at, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)).total_seconds()
        click.echo(f"cache age:           {int(age)}s")

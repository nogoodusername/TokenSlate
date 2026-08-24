from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

DEFAULTS: dict[str, Any] = {
    "refresh_interval_seconds": 300,
    "providers": ["codex", "claude"],
    "device_name_prefix": "TokenSlate-",
}


def config_dir() -> Path:
    xdg = os.environ.get("XDG_CONFIG_HOME")
    base = Path(xdg) if xdg else Path.home() / ".config"
    return base / "tokenslate"


def config_path() -> Path:
    return config_dir() / "config.yaml"


@dataclass
class Config:
    refresh_interval_seconds: int = 300
    providers: list[str] = field(default_factory=lambda: list(DEFAULTS["providers"]))
    device_name_prefix: str = "TokenSlate-"

    @classmethod
    def load(cls) -> "Config":
        path = config_path()
        if not path.exists():
            cfg = cls()
            cfg.save()
            return cfg
        with path.open() as f:
            data = yaml.safe_load(f) or {}
        merged = {**DEFAULTS, **data}
        return cls(
            refresh_interval_seconds=int(merged["refresh_interval_seconds"]),
            providers=list(merged["providers"]),
            device_name_prefix=str(merged["device_name_prefix"]),
        )

    def save(self) -> None:
        config_dir().mkdir(parents=True, exist_ok=True)
        with config_path().open("w") as f:
            yaml.safe_dump(
                {
                    "refresh_interval_seconds": self.refresh_interval_seconds,
                    "providers": self.providers,
                    "device_name_prefix": self.device_name_prefix,
                },
                f,
                default_flow_style=False,
            )

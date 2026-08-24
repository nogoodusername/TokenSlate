from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Any, Optional

from .config import config_dir


def state_path():
    return config_dir() / "state.json"


@dataclass
class State:
    """cache_age_seconds (docs §9) is derived at display time from
    last_sync_at rather than stored -- a persisted value would just be
    stale the moment it's read back."""
    last_seen_device_at: Optional[str] = None
    last_sync_at: Optional[str] = None
    last_sync_status: Optional[str] = None
    cached_providers: list[str] | None = None

    @classmethod
    def load(cls) -> Optional["State"]:
        path = state_path()
        if not path.exists():
            return None
        with path.open() as f:
            data = json.load(f)
        return cls(**{k: data.get(k) for k in cls.__dataclass_fields__})

    def save(self) -> None:
        config_dir().mkdir(parents=True, exist_ok=True)
        with state_path().open("w") as f:
            json.dump(asdict(self), f, indent=2)

    def merge_save(self, **updates: Any) -> "State":
        for key, value in updates.items():
            setattr(self, key, value)
        self.save()
        return self


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

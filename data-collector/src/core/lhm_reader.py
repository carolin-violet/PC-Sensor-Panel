from __future__ import annotations

import json
from urllib.parse import urljoin
from typing import Any

import httpx

from src.config import settings

__all__ = ["LHMReader"]


class LHMReader:
    """LibreHardwareMonitor HTTP reader for phase-one validation."""

    def __init__(
        self,
        base_url: str | None = None,
        data_path: str | None = None,
        timeout_ms: int | None = None,
    ) -> None:
        self._base_url = (base_url or settings.LHM_BASE_URL).rstrip("/")
        self._data_path = data_path or settings.LHM_DATA_PATH
        self._timeout_seconds = (timeout_ms or settings.LHM_TIMEOUT_MS) / 1000
        self._data_url = urljoin(f"{self._base_url}/", self._data_path.lstrip("/"))
        self._validate_config()

    def _validate_config(self) -> None:
        if not self._base_url.startswith(("http://", "https://")):
            raise ValueError(
                "LHM_BASE_URL must start with http:// or https://. "
                f"Current value: {self._base_url}"
            )

    @property
    def data_url(self) -> str:
        return self._data_url

    def _fetch_payload(self) -> Any:
        try:
            with httpx.Client(timeout=self._timeout_seconds) as client:
                response = client.get(self._data_url)
                response.raise_for_status()
                payload = response.text
        except httpx.HTTPStatusError as exc:
            raise RuntimeError(
                f"LibreHardwareMonitor HTTP request failed with status {exc.response.status_code}: "
                f"{self._data_url}"
            ) from exc
        except httpx.RequestError as exc:
            raise RuntimeError(
                "Failed to connect to LibreHardwareMonitor remote web server. "
                f"Expected endpoint: {self._data_url}. "
                "Make sure LibreHardwareMonitor is running and its web server is enabled."
            ) from exc

        try:
            return json.loads(payload)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                "LibreHardwareMonitor returned invalid JSON. "
                f"Endpoint: {self._data_url}"
            ) from exc

    def read(self) -> dict[str, Any]:
        payload = self._fetch_payload()

        return {
            "ok": True,
            "source": "lhm_http",
            "data_url": self.data_url,
            "payload": payload,
        }

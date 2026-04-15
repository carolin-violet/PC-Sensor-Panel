from __future__ import annotations

import json
from datetime import datetime
from urllib.parse import urljoin
from typing import Any

import httpx

from src.core.metrics_normalizer import MetricsNormalizer, parse_numeric_value
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
        self._metrics_normalizer = MetricsNormalizer()
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

    def _clean_text(self, value: Any) -> str:
        return str(value or "").strip()

    def _build_path(
        self,
        path: list[str] | None,
        node_text: Any,
    ) -> list[str]:
        cleaned_text = self._clean_text(node_text)
        return [*(path or []), *([cleaned_text] if cleaned_text else [])]

    def _build_sensor_entry(
        self,
        node: dict[str, Any],
        *,
        path: list[str],
    ) -> dict[str, Any]:
        raw_value = node.get("Value")

        return {
            "sensor_id": node.get("SensorId"),
            "name": self._clean_text(node.get("Text")),
            "type": self._clean_text(node.get("Type")),
            "value": raw_value,
            "value_numeric": parse_numeric_value(raw_value),
            "min": node.get("Min"),
            "max": node.get("Max"),
            "path": path,
        }

    def _walk_nodes(
        self,
        node: Any,
        *,
        path: list[str] | None = None,
    ) -> list[dict[str, Any]]:
        if not isinstance(node, dict):
            return []

        current_path = self._build_path(path, node.get("Text"))
        sensors: list[dict[str, Any]] = []

        if node.get("SensorId") or node.get("Type"):
            sensors.append(self._build_sensor_entry(node, path=current_path))

        children = node.get("Children")
        if isinstance(children, list):
            for child in children:
                sensors.extend(self._walk_nodes(child, path=current_path))

        return sensors

    def _flatten_sensors(self, payload: Any) -> list[dict[str, Any]]:
        if isinstance(payload, dict):
            return self._walk_nodes(payload)
        if isinstance(payload, list):
            sensors: list[dict[str, Any]] = []
            for item in payload:
                sensors.extend(self._walk_nodes(item))
            return sensors
        return []

    def _normalize_metrics(self, sensors: list[dict[str, Any]]) -> dict[str, Any]:
        return self._metrics_normalizer.normalize(sensors)

    def _build_device_metrics(self, metrics: dict[str, Any]) -> dict[str, Any]:
        cpu = metrics.get("cpu") or {}
        memory = metrics.get("memory") or {}
        gpu = metrics.get("gpu") or {}
        network = metrics.get("network") or {}

        return {
            "cpu": {
                "usage_pct": cpu.get("usage_pct"),
                "temp_c": cpu.get("temp_c"),
            },
            "memory": {
                "used_mb": memory.get("used_mb"),
                "total_mb": memory.get("total_mb"),
                "usage_pct": memory.get("usage_pct"),
            },
            "gpu": {
                "usage_pct": gpu.get("usage_pct"),
                "temp_c": gpu.get("temp_c"),
            },
            "network": {
                "download_kBps": network.get("download_kBps"),
                "upload_kBps": network.get("upload_kBps"),
            },
        }

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

    def _build_common_metadata(self) -> dict[str, Any]:
        return {
            "ok": True,
            "source": "lhm_http",
            "data_url": self.data_url,
            "collected_at": datetime.now().astimezone().isoformat(),
            "metrics_schema_version": "1.0.0",
        }

    def read_latest(self) -> dict[str, Any]:
        payload = self._fetch_payload()
        sensors = self._flatten_sensors(payload)
        metrics = self._normalize_metrics(sensors)

        return {
            **self._build_common_metadata(),
            "metrics": self._build_device_metrics(metrics),
        }

    def read_raw(self) -> dict[str, Any]:
        payload = self._fetch_payload()
        sensors = self._flatten_sensors(payload)
        metrics = self._normalize_metrics(sensors)

        return {
            **self._build_common_metadata(),
            "metrics": metrics,
            "sensor_count": len(sensors),
            "sensors": sensors,
            "payload": payload,
        }

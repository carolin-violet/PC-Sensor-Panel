from __future__ import annotations

import ctypes
import re
from typing import Any

__all__ = ["MetricsNormalizer", "parse_numeric_value"]


def parse_numeric_value(raw_value: Any) -> float | None:
    if raw_value in (None, ""):
        return None

    if isinstance(raw_value, (int, float)):
        return float(raw_value)

    if not isinstance(raw_value, str):
        return None

    match = re.search(r"-?\d+(?:\.\d+)?", raw_value.replace(",", ""))
    if match is None:
        return None

    try:
        return float(match.group(0))
    except ValueError:
        return None


class _MemoryStatusEx(ctypes.Structure):
    _fields_ = [
        ("dwLength", ctypes.c_ulong),
        ("dwMemoryLoad", ctypes.c_ulong),
        ("ullTotalPhys", ctypes.c_ulonglong),
        ("ullAvailPhys", ctypes.c_ulonglong),
        ("ullTotalPageFile", ctypes.c_ulonglong),
        ("ullAvailPageFile", ctypes.c_ulonglong),
        ("ullTotalVirtual", ctypes.c_ulonglong),
        ("ullAvailVirtual", ctypes.c_ulonglong),
        ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
    ]


def read_windows_memory_status_mb() -> dict[str, float] | None:
    if not hasattr(ctypes, "windll"):
        return None

    memory_status = _MemoryStatusEx()
    memory_status.dwLength = ctypes.sizeof(_MemoryStatusEx)

    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory_status)):
        return None

    total_mb = round(memory_status.ullTotalPhys / 1024 / 1024, 2)
    available_mb = round(memory_status.ullAvailPhys / 1024 / 1024, 2)
    used_mb = round(total_mb - available_mb, 2)
    usage_pct = round(memory_status.dwMemoryLoad, 2)

    return {
        "used_mb": used_mb,
        "available_mb": available_mb,
        "total_mb": total_mb,
        "usage_pct": usage_pct,
    }


class MetricsNormalizer:
    """Normalize flattened LibreHardwareMonitor sensors into stable metrics."""

    def _sensor_matches(
        self,
        sensor: dict[str, Any],
        *,
        sensor_type: str | None = None,
        names: tuple[str, ...] = (),
        path_keywords: tuple[str, ...] = (),
    ) -> bool:
        name = str(sensor.get("name") or "").casefold()
        path_segments = [str(item).casefold() for item in sensor.get("path", [])]
        path_text = " ".join(path_segments)

        if sensor_type and str(sensor.get("type") or "").casefold() != sensor_type.casefold():
            return False

        if names and not any(keyword.casefold() in name for keyword in names):
            return False

        if path_keywords and not any(keyword.casefold() in path_text for keyword in path_keywords):
            return False

        return True

    def _matching_sensors(
        self,
        sensors: list[dict[str, Any]],
        *,
        sensor_type: str | None = None,
        names: tuple[str, ...] = (),
        path_keywords: tuple[str, ...] = (),
    ) -> list[dict[str, Any]]:
        return [
            sensor
            for sensor in sensors
            if self._sensor_matches(
                sensor,
                sensor_type=sensor_type,
                names=names,
                path_keywords=path_keywords,
            )
        ]

    def _first_numeric(
        self,
        sensors: list[dict[str, Any]],
        *,
        sensor_type: str | None = None,
        names: tuple[str, ...] = (),
        path_keywords: tuple[str, ...] = (),
    ) -> float | None:
        for sensor in self._matching_sensors(
            sensors,
            sensor_type=sensor_type,
            names=names,
            path_keywords=path_keywords,
        ):
            numeric_value = sensor.get("value_numeric")
            if not isinstance(numeric_value, (int, float)):
                numeric_value = parse_numeric_value(sensor.get("value"))
            if numeric_value is not None:
                return numeric_value

        return None

    def _max_numeric(
        self,
        sensors: list[dict[str, Any]],
        *,
        sensor_type: str | None = None,
        names: tuple[str, ...] = (),
        path_keywords: tuple[str, ...] = (),
    ) -> float | None:
        numeric_values: list[float] = []

        for sensor in self._matching_sensors(
            sensors,
            sensor_type=sensor_type,
            names=names,
            path_keywords=path_keywords,
        ):
            numeric_value = sensor.get("value_numeric")
            if isinstance(numeric_value, (int, float)):
                numeric_values.append(float(numeric_value))

        if not numeric_values:
            return None

        return max(numeric_values)

    def _normalize_memory_metrics(self, sensors: list[dict[str, Any]]) -> dict[str, float | None]:
        memory_used_gb = self._first_numeric(
            sensors,
            sensor_type="Data",
            names=("used memory",),
            path_keywords=("memory",),
        )
        memory_available_gb = self._first_numeric(
            sensors,
            sensor_type="Data",
            names=("available memory",),
            path_keywords=("memory",),
        )
        memory_load_pct = self._first_numeric(
            sensors,
            sensor_type="Load",
            names=("memory", "memory load"),
            path_keywords=("memory",),
        )

        total_mb = None
        if memory_used_gb is not None and memory_available_gb is not None:
            total_mb = round((memory_used_gb + memory_available_gb) * 1024, 2)

        used_mb = round(memory_used_gb * 1024, 2) if memory_used_gb is not None else None
        available_mb = (
            round(memory_available_gb * 1024, 2)
            if memory_available_gb is not None
            else None
        )

        if memory_load_pct is None and used_mb is not None and total_mb not in (None, 0):
            memory_load_pct = round(used_mb / total_mb * 100, 2)

        metrics = {
            "used_mb": used_mb,
            "available_mb": available_mb,
            "total_mb": total_mb,
            "usage_pct": memory_load_pct,
        }

        if metrics["used_mb"] is None or metrics["total_mb"] is None:
            fallback_metrics = read_windows_memory_status_mb()
            if fallback_metrics is not None:
                metrics["used_mb"] = fallback_metrics["used_mb"]
                metrics["available_mb"] = fallback_metrics["available_mb"]
                metrics["total_mb"] = fallback_metrics["total_mb"]
                if metrics["usage_pct"] is None:
                    metrics["usage_pct"] = fallback_metrics["usage_pct"]

        return metrics

    def _normalize_cpu_metrics(self, sensors: list[dict[str, Any]]) -> dict[str, float | None]:
        return {
            "usage_pct": self._first_numeric(
                sensors,
                sensor_type="Load",
                names=("cpu total", "total cpu"),
                path_keywords=("cpu",),
            ),
            "temp_c": self._first_numeric(
                sensors,
                sensor_type="Temperature",
                names=("cpu package", "package", "tdie", "ccd average"),
                path_keywords=("cpu",),
            ),
            "clock_mhz": self._first_numeric(
                sensors,
                sensor_type="Clock",
                names=("cpu core", "bus speed", "core #"),
                path_keywords=("cpu",),
            ),
            "power_w": self._first_numeric(
                sensors,
                sensor_type="Power",
                names=("package", "cpu package", "package power"),
                path_keywords=("cpu",),
            ),
        }

    def _normalize_gpu_metrics(self, sensors: list[dict[str, Any]]) -> dict[str, float | None]:
        return {
            "usage_pct": self._max_numeric(
                sensors,
                sensor_type="Load",
                names=("gpu core", "gpu total", "d3d 3d", "gpu"),
                path_keywords=("gpu",),
            ),
            "temp_c": self._max_numeric(
                sensors,
                sensor_type="Temperature",
                names=("gpu core", "gpu hotspot", "gpu hot spot", "hot spot"),
                path_keywords=("gpu",),
            ),
            "memory_used_mb": self._first_numeric(
                sensors,
                sensor_type="SmallData",
                names=("gpu memory used", "d3d dedicated memory used"),
                path_keywords=("gpu",),
            ),
            "power_w": self._first_numeric(
                sensors,
                sensor_type="Power",
                names=("gpu package", "board power", "gpu power"),
                path_keywords=("gpu",),
            ),
        }

    def _normalize_network_metrics(self, sensors: list[dict[str, Any]]) -> dict[str, float | None]:
        return {
            "download_kBps": self._max_numeric(
                sensors,
                sensor_type="Throughput",
                names=("download", "received"),
                path_keywords=("network", "wlan", "wi-fi", "wifi", "ethernet", "nic"),
            ),
            "upload_kBps": self._max_numeric(
                sensors,
                sensor_type="Throughput",
                names=("upload", "sent"),
                path_keywords=("network", "wlan", "wi-fi", "wifi", "ethernet", "nic"),
            ),
        }

    def _normalize_storage_metrics(self, sensors: list[dict[str, Any]]) -> dict[str, float | None]:
        return {
            "read_kBps": self._max_numeric(
                sensors,
                sensor_type="Throughput",
                names=("read",),
                path_keywords=("hdd", "drive", "storage", "disk"),
            ),
            "write_kBps": self._max_numeric(
                sensors,
                sensor_type="Throughput",
                names=("write",),
                path_keywords=("hdd", "drive", "storage", "disk"),
            ),
            "temp_c": self._max_numeric(
                sensors,
                sensor_type="Temperature",
                names=("temperature",),
                path_keywords=("hdd", "drive", "storage", "disk"),
            ),
        }

    def _normalize_system_metrics(self, sensors: list[dict[str, Any]]) -> dict[str, float | None]:
        return {
            "fan_rpm": self._max_numeric(
                sensors,
                sensor_type="Fan",
                names=("fan", "cpu", "system"),
                path_keywords=("mainboard", "lpc", "ec", "motherboard"),
            ),
        }

    def normalize(self, sensors: list[dict[str, Any]]) -> dict[str, Any]:
        return {
            "cpu": self._normalize_cpu_metrics(sensors),
            "memory": self._normalize_memory_metrics(sensors),
            "gpu": self._normalize_gpu_metrics(sensors),
            "network": self._normalize_network_metrics(sensors),
            "storage": self._normalize_storage_metrics(sensors),
            "system": self._normalize_system_metrics(sensors),
        }

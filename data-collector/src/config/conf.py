from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict

__all__ = ["settings", "Settings"]


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        case_sensitive=True,
        extra="ignore",
    )

    APP_NAME: str = "PC Sensor Panel Data Collector"
    APP_VERSION: str = "0.1.0"
    DEBUG: bool = True

    HOST: str = "0.0.0.0"
    PORT: int = 8787

    COLLECT_INTERVAL_MS: int = 500
    PUSH_INTERVAL_MS: int = 1000
    ENABLE_RAW_METRICS: bool = True

    LHM_DLL_PATH: str = "third_party/LibreHardwareMonitor/LibreHardwareMonitorLib.dll"

    CORS_ORIGINS: list[str] = Field(
        default_factory=lambda: [
            "http://localhost:8000",
            "http://127.0.0.1:8000",
        ]
    )

    DEVICE_DEFAULT_NAME: str = "PC Monitor"
    DEVICE_REFRESH_MS: int = 1000
    DEVICE_ROTATION: int = 1
    DEVICE_SHOW_CHART: bool = True
    DEVICE_SHOW_NETWORK: bool = True
    DEVICE_SHOW_DISK: bool = True


settings = Settings()

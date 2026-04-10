import logging
from fastapi import FastAPI

from src.config.conf import settings

__all__ = ["app"]


logging.getLogger().setLevel(logging.INFO)
logging.getLogger("src").setLevel(logging.INFO)

app = FastAPI(
    title=settings.APP_NAME,
    version=settings.APP_VERSION,
)


@app.get("/health")
async def health() -> dict[str, str | bool]:
    """最小健康检查接口。"""
    return {
        "ok": True,
        "app_name": settings.APP_NAME,
        "version": settings.APP_VERSION,
    }

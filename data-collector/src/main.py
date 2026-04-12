import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.responses import JSONResponse

from src.config import settings
from src.core.lhm_reader import LHMReader

__all__ = ["app"]


logging.getLogger().setLevel(logging.INFO)
logging.getLogger("src").setLevel(logging.INFO)


@asynccontextmanager
async def lifespan(app: FastAPI):
    try:
        app.state.lhm_reader = LHMReader()
        app.state.lhm_init_error = None
        logging.getLogger("src").info("LibreHardwareMonitor initialized successfully.")
    except Exception as exc:  # pragma: no cover - startup environment dependent
        app.state.lhm_reader = None
        app.state.lhm_init_error = str(exc)
        logging.getLogger("src").exception(
            "Failed to initialize LibreHardwareMonitor reader."
        )

    yield


app = FastAPI(
    title=settings.APP_NAME,
    version=settings.APP_VERSION,
    lifespan=lifespan,
)


@app.get("/health")
async def health() -> dict[str, str | bool]:
    """最小健康检查接口。"""
    reader = getattr(app.state, "lhm_reader", None)

    return {
        "ok": True,
        "app_name": settings.APP_NAME,
        "version": settings.APP_VERSION,
        "source_type": "lhm_http",
        "source_url": reader.data_url if reader is not None else "",
        "source_ready": reader is not None,
    }


@app.get("/api/metrics/latest")
async def latest_metrics() -> JSONResponse:
    reader = app.state.lhm_reader
    init_error = app.state.lhm_init_error

    if reader is None:
        return JSONResponse(
            status_code=503,
            content={
                "ok": False,
                "error": "LHM_READER_INIT_FAILED",
                "message": init_error or "LibreHardwareMonitor reader is unavailable.",
            },
        )

    try:
        return JSONResponse(status_code=200, content=reader.read())
    except Exception as exc:  # pragma: no cover - depends on host hardware/runtime
        logging.getLogger("src").exception("Failed to collect metrics.")
        return JSONResponse(
            status_code=500,
            content={
                "ok": False,
                "error": "LHM_READ_FAILED",
                "message": str(exc),
            },
        )

# PC-Sensor-Panel

## Python 采集端启动说明

`data-collector` 是 PC 端的数据采集服务，当前通过 LibreHardwareMonitor 暴露的 HTTP 接口读取硬件传感器，并通过 `FastAPI` 对外提供接口。

### 运行前提

- Windows
- Python `3.14+`
- 建议使用 [`uv`](https://docs.astral.sh/uv/) 管理依赖与运行环境
- 已安装并可运行 LibreHardwareMonitor

当前方案不再依赖 `pythonnet` 和 `LibreHardwareMonitorLib.dll`，而是直接请求 LibreHardwareMonitor 的远程 Web 接口。

LibreHardwareMonitor 官方地址：

- GitHub 仓库：<https://github.com/LibreHardwareMonitor/LibreHardwareMonitor>
- Releases：<https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases>

### 启动 LibreHardwareMonitor Web 服务

1. 打开 LibreHardwareMonitor
2. 在菜单中启用远程 Web 服务并设置端口
3. 确认 `http://127.0.0.1:8085/data.json` 可以访问

默认情况下，`data-collector` 会从 `http://127.0.0.1:8085/data.json` 拉取数据。

### 安装依赖

在项目根目录执行：

```powershell
cd data-collector
uv python install 3.14
uv sync
```

### 启动服务

当前项目实际入口文件是 `data-collector/src/main.py`，可使用下面命令启动：

```powershell
cd data-collector
uv run uvicorn src.main:app --host 0.0.0.0 --port 8787 --reload
```

说明：

- `src.main:app` 是当前 FastAPI 应用入口
- 默认端口可参考 `data-collector/src/config/conf.py` 中的 `PORT=8787`
- 默认上游数据源是 `http://127.0.0.1:8085/data.json`
- `--reload` 适合开发环境，生产或长期运行时可以去掉

### 启动后验证

健康检查：

```powershell
curl http://127.0.0.1:8787/health
```

查看最新采集结果：

```powershell
curl http://127.0.0.1:8787/api/metrics/latest
```

如果 `LibreHardwareMonitor` 数据拉取失败，`/api/metrics/latest` 会返回 `500`，可以优先检查以下内容：

- 是否在 Windows 环境运行
- LibreHardwareMonitor 是否已经启动
- LibreHardwareMonitor 的 Web 服务是否已经开启
- `http://127.0.0.1:8085/data.json` 是否能在浏览器中访问

### 可选：使用环境变量覆盖配置

`data-collector` 会从 `.env` 读取配置，你可以在 `data-collector/.env` 中覆盖默认值，例如：

```env
HOST=0.0.0.0
PORT=8787
LHM_BASE_URL=http://127.0.0.1:8085
LHM_DATA_PATH=/data.json
LHM_TIMEOUT_MS=3000
COLLECT_INTERVAL_MS=500
PUSH_INTERVAL_MS=1000
```

# ESP32-C3 PC 性能副屏方案

## 1. 方案目标

当前方案统一为：

`LibreHardwareMonitor -> Python Agent -> HTTP / WebSocket -> ESP32-C3 -> TFT`

核心目标：

- 使用 `Python` 在 PC 端集中处理硬件采集、标准化与传输
- 当前先通过 `HTTP` 打通最小可运行链路
- 后续再补齐 `WebSocket`、bootstrap、缓存和多设备能力
- 让 ESP32 只负责联网、收包、解析和显示，不承担 Windows 端采集逻辑

这比把采集逻辑塞进 ESP32 更清晰，也更方便调试和迭代协议。

## 2. 总体架构

采用四层结构：

`LibreHardwareMonitor -> Python Collector -> Transport API -> ESP32 Firmware`

职责划分如下。

### 2.1 LibreHardwareMonitor

仅负责从 Windows 主机读取硬件传感器数据，例如：

- CPU 占用率、温度、频率、功耗
- 内存负载和部分容量信息
- GPU 占用率、温度、显存占用、功耗
- 网络吞吐
- 存储温度和吞吐
- 主板风扇转速

它不直接与 ESP32 通信。

### 2.2 Python Agent

这是整个系统的核心中间层，对应仓库中的 `data-collector`。

当前职责：

- 通过 `LibreHardwareMonitor` 的远程 Web 接口读取 `data.json`
- 递归展开硬件树，拉平成统一传感器列表
- 将原始传感器映射为稳定字段
- 输出最小设备视图和完整调试视图两个接口
- 提供健康检查和错误信息

后续职责：

- 增加 `WebSocket` 实时推送
- 增加 bootstrap 配置接口
- 增加采集缓存、广播和历史能力
- 视需要演进为 `pythonnet + LibreHardwareMonitorLib.dll`

### 2.3 ESP32 固件

职责尽量收敛：

- 配网和保存配置
- 发现或连接 Python Agent
- 请求初始化配置
- 接收实时指标 JSON
- 做轻量解析和本地缓存
- 绘制 UI 和处理断线重连

原则：

- 不在 ESP32 上做硬件采集逻辑
- 不让 ESP32 依赖 `LibreHardwareMonitor` 内部结构
- 所有和 PC 平台绑定的逻辑都放在 Python Agent

## 3. PC 端技术路线

当前 PC 端推荐组合：

- 运行时：`Python 3.14+`
- 当前硬件采集：`LibreHardwareMonitor` 远程 Web 接口
- HTTP 服务：`FastAPI`
- HTTP 客户端：`httpx`
- ASGI Server：`uvicorn`
- 配置管理：`pydantic-settings`

后续可选路线：

- `pythonnet`
- `LibreHardwareMonitorLib.dll`

### 3.1 为什么先用 HTTP data.json

当前阶段优先采用：

`LibreHardwareMonitor data.json -> Python -> FastAPI`

原因：

- 实现简单，最适合先验证链路
- 调试成本低，浏览器就能看原始数据
- 不需要先处理 Python 与 .NET 的运行时兼容问题
- 便于先把标准化和接口协议稳定下来

### 3.2 当前已落地的采集方式

当前 `data-collector` 已落地的是基于 `LibreHardwareMonitor` 远程 Web 服务的最小链路：

`http://127.0.0.1:8085/data.json -> LHMReader -> MetricsNormalizer -> FastAPI`

当前流程：

1. `FastAPI` 启动时初始化 `LHMReader`
2. `LHMReader` 读取 `LHM_BASE_URL`、`LHM_DATA_PATH`、`LHM_TIMEOUT_MS`
3. 通过 `httpx` 请求 `data.json`
4. 递归遍历 `Children`
5. 拉平成标准传感器列表
6. 补齐 `value_numeric`
7. 调用 `MetricsNormalizer` 做字段映射
8. 对外返回 `latest` 或 `raw`

默认数据源地址：

```txt
http://127.0.0.1:8085/data.json
```

### 3.3 后续可演进为 pythonnet

后续如果需要摆脱 LHM 自带 Web 服务，可演进到：

- 项目中引入 `LibreHardwareMonitorLib.dll`
- 使用 `pythonnet` 直接加载 DLL
- 在 Python 中主动刷新硬件树和子硬件

但在当前阶段，不建议先切换实现，优先保持最小链路稳定。

## 4. 当前仓库实现

当前 `data-collector` 已落地的是一个最小可运行结构。

核心文件：

- `data-collector/src/main.py`
  - `FastAPI` 入口
  - 注册 `/health`、`/api/metrics/latest`、`/api/metrics/raw`
- `data-collector/src/core/lhm_reader.py`
  - 请求 `data.json`
  - 递归展开硬件树
  - 输出 `latest` 和 `raw`
- `data-collector/src/core/metrics_normalizer.py`
  - 负责统一字段映射
  - 负责数值解析
  - 负责内存 Windows API 兜底
- `data-collector/src/config/conf.py`
  - 服务端口、LHM 地址、超时、设备默认配置

当前目录更接近：

```txt
data-collector/
  pyproject.toml
  src/
    main.py
    config/
      conf.py
    core/
      lhm_reader.py
      metrics_normalizer.py
```

后续再按需要拆分为：

- `services/`
- `router/`
- `schemas/`
- `broadcast/`

## 5. 指标采集与标准化

`LibreHardwareMonitor` 输出的是大量原始传感器，不能直接原样发给 ESP32。

当前已经在 Python 侧做了一层标准化。

### 5.1 当前标准化分组

当前标准化结构分为：

- `cpu`
- `memory`
- `gpu`
- `network`
- `storage`
- `system`

### 5.2 当前已实现字段

当前 `MetricsNormalizer` 已实现如下字段：

```json
{
  "cpu": {
    "usage_pct": 37.2,
    "temp_c": 62.0,
    "clock_mhz": 4385,
    "power_w": 78.4
  },
  "memory": {
    "used_mb": 16422,
    "available_mb": 15494,
    "total_mb": 31916,
    "usage_pct": 51.4
  },
  "gpu": {
    "usage_pct": 71.0,
    "temp_c": 68.0,
    "memory_used_mb": 6240,
    "power_w": 182.3
  },
  "network": {
    "upload_kBps": 128.4,
    "download_kBps": 2340.8
  },
  "storage": {
    "read_kBps": null,
    "write_kBps": null,
    "temp_c": 43.0
  },
  "system": {
    "fan_rpm": 1280
  }
}
```

字段约定：

- 百分比统一使用 `_pct`
- 温度统一使用 `_c`
- 频率统一使用 `_mhz`
- 功耗统一使用 `_w`
- 转速统一使用 `_rpm`
- 容量统一使用 `_mb`
- 网络速率统一使用 `kBps`

### 5.3 当前映射规则

当前映射主要基于三类条件：

- 传感器类型，例如 `Load`、`Temperature`、`Power`、`Throughput`
- 名称关键词，例如 `cpu total`、`cpu package`、`gpu core`
- 路径关键词，例如 `cpu`、`gpu`、`wlan`、`ethernet`

例如：

- `CPU Total` -> `cpu.usage_pct`
- `CPU Package` -> `cpu.temp_c` / `cpu.power_w`
- `GPU Core` -> `gpu.temp_c`
- `GPU Memory Used` -> `gpu.memory_used_mb`
- `Upload Speed` / `Download Speed` -> `network.upload_kBps` / `network.download_kBps`

当前网络匹配已放宽为兼容：

- `network`
- `wlan`
- `wi-fi`
- `wifi`
- `ethernet`
- `nic`

### 5.4 当前内存兜底逻辑

当前已发现一个现实问题：

- 不同主机上 LHM 的内存原始传感器命名并不稳定
- 某些机器能拿到 `memory.usage_pct`
- 但 `Used Memory` / `Available Memory` 不一定能稳定命中

因此当前实现增加了一个 Windows 系统 API 兜底：

- 优先尝试从 LHM 提取 `used_mb`、`available_mb`、`total_mb`
- 如果缺失，则调用 `GlobalMemoryStatusEx`
- 保证 `memory.used_mb`、`memory.total_mb` 在设备接口中尽量稳定可用

这类兜底逻辑后续可以继续扩展到网络、磁盘等字段。

## 6. 接口设计

当前已落地的是最小 `HTTP` 接口集合。

### 6.1 `GET /health`

用途：

- 检查服务是否启动成功
- 查看当前数据源地址和初始化状态

响应示例：

```json
{
  "ok": true,
  "app_name": "PC Sensor Panel Data Collector",
  "version": "0.1.0",
  "source_type": "lhm_http",
  "source_url": "http://127.0.0.1:8085/data.json",
  "source_ready": true
}
```

说明：

- 这个接口不要求当前一次采集成功
- 它只反映服务进程和数据源初始化状态

### 6.2 `GET /api/metrics/latest`

用途：

- 返回设备侧最小指标视图
- 控制返回体积，避免把调试数据直接发给设备

当前公共元信息：

- `ok`
- `source`
- `data_url`
- `collected_at`
- `metrics_schema_version`

当前 `metrics` 字段只返回设备当前最小集：

```json
{
  "metrics": {
    "cpu": {
      "usage_pct": 32.9,
      "temp_c": 58.0
    },
    "memory": {
      "used_mb": 15599.2,
      "total_mb": 32621.36,
      "usage_pct": 51.2
    },
    "gpu": {
      "usage_pct": 28.0,
      "temp_c": 67.1
    },
    "network": {
      "download_kBps": 0.0,
      "upload_kBps": 0.1
    }
  }
}
```

当前最小集包括：

- `cpu.usage_pct`
- `cpu.temp_c`
- `memory.used_mb`
- `memory.total_mb`
- `memory.usage_pct`
- `gpu.usage_pct`
- `gpu.temp_c`
- `network.download_kBps`
- `network.upload_kBps`

### 6.3 `GET /api/metrics/raw`

用途：

- 返回当前完整调试结构
- 用于观察真实传感器名称、类型、路径和原始载荷

当前返回：

- 与 `latest` 相同的公共元信息
- 完整标准化 `metrics`
- `sensor_count`
- `sensors`
- `payload`

说明：

- `sensors` 是扁平化传感器列表
- `payload` 是原始 `data.json`
- 这个接口只建议开发期使用

### 6.4 当前错误处理

当前接口已实现：

- 启动阶段 `LHMReader` 初始化失败时返回 `503`
- 读取失败时返回 `500`
- 错误码区分为：
  - `LHM_READER_INIT_FAILED`
  - `LHM_READ_FAILED`

### 6.5 当前未实现接口

以下接口仍属于后续阶段：

- `GET /api/device/bootstrap`
- `WS /ws/metrics`

当前文档中涉及这些接口的描述，都应理解为后续目标，而非当前已落地逻辑。

## 7. 当前 Python Agent 主流程

当前已落地的主流程如下：

1. `FastAPI lifespan` 初始化 `LHMReader`
2. `LHMReader` 校验 `LHM_BASE_URL`
3. 每次请求接口时，通过 `httpx` 拉取最新 `data.json`
4. 递归遍历 `Children`
5. 拉平成传感器列表
6. 为每个传感器补齐：
   - `sensor_id`
   - `name`
   - `type`
   - `value`
   - `value_numeric`
   - `min`
   - `max`
   - `path`
7. 调用 `MetricsNormalizer` 做标准化
8. 根据接口类型输出：
   - `read_latest()`
   - `read_raw()`

这个阶段仍然是“按请求实时采集”，尚未引入后台周期缓存。

## 8. ESP32 侧接入方式

后续推荐流程仍然是：

1. ESP32 启动并联网
2. 请求 `GET /api/device/bootstrap`
3. 保存刷新间隔、布局和主题配置
4. 连接 `WS /ws/metrics`
5. 接收并解析 `metrics` 消息
6. 用本地缓存驱动 UI
7. 断开后进入重连状态

但在当前阶段，更适合先按下面顺序联调：

1. 先验证 `GET /health`
2. 再验证 `GET /api/metrics/latest`
3. 再对照 `GET /api/metrics/raw` 补映射规则
4. 等 HTTP 协议稳定后，再补 `bootstrap + WebSocket`

## 9. 当前阶段的开发建议

### 9.1 优先保证字段稳定

当前最重要的不是一次性覆盖所有指标，而是：

- 让设备端消费字段稳定
- 让调试端保留足够上下文
- 让不同主机上的命名差异尽量被映射层吸收

### 9.2 latest 和 raw 要继续分离

当前已经证明：

- `latest` 适合设备侧
- `raw` 适合开发调试

这个边界应继续保持，不建议再把原始 `payload` 或完整 `sensors` 混回 `latest`。

### 9.3 多来源兜底是合理方向

当前内存已经通过 Windows API 兜底。

后续如果遇到：

- 网络速率不稳定
- 存储吞吐命中率低
- 某些主板风扇字段缺失

也可以继续引入：

- `psutil`
- Windows 性能计数器
- 其他系统 API

原则是：

- 协议层不暴露底层来源
- ESP32 只认稳定字段

## 10. 当前配置结构

当前配置集中在 `data-collector/src/config/conf.py`，核心包括：

- `HOST`
- `PORT`
- `COLLECT_INTERVAL_MS`
- `PUSH_INTERVAL_MS`
- `LHM_BASE_URL`
- `LHM_DATA_PATH`
- `LHM_TIMEOUT_MS`
- `DEVICE_DEFAULT_NAME`
- `DEVICE_REFRESH_MS`
- `DEVICE_ROTATION`
- `DEVICE_SHOW_CHART`
- `DEVICE_SHOW_NETWORK`
- `DEVICE_SHOW_DISK`

当前配置示意：

```json
{
  "HOST": "0.0.0.0",
  "PORT": 8787,
  "COLLECT_INTERVAL_MS": 500,
  "PUSH_INTERVAL_MS": 1000,
  "LHM_BASE_URL": "http://127.0.0.1:8085",
  "LHM_DATA_PATH": "/data.json",
  "LHM_TIMEOUT_MS": 3000,
  "DEVICE_DEFAULT_NAME": "PC Monitor",
  "DEVICE_REFRESH_MS": 1000,
  "DEVICE_ROTATION": 1,
  "DEVICE_SHOW_CHART": true,
  "DEVICE_SHOW_NETWORK": true,
  "DEVICE_SHOW_DISK": true
}
```

## 11. 风险与注意事项

### 11.1 传感器名称不稳定

不同主板、CPU、GPU、网卡平台下，LHM 暴露的名称和路径可能不同。

因此映射逻辑应尽量基于：

- 传感器类型
- 名称关键词
- 路径关键词

并保留 `raw` 接口用于持续校准。

### 11.2 当前仍未引入后台缓存

当前是“接口请求时实时拉取数据”，优点是简单，缺点是：

- 每次请求都会访问 LHM
- 还没有统一采样时钟
- 也还没有数据老化判断

后续引入采集服务和缓存后，这些问题会更好处理。

### 11.3 JSON 体积仍需控制

虽然 `raw` 很适合调试，但对 ESP32-C3 来说不适合长期直接消费。

因此应继续坚持：

- 设备走 `latest`
- 调试走 `raw`

### 11.4 WebSocket 仍是后续项

当前还没有实现 `WS /ws/metrics`，所以所有实时联调仍应基于 HTTP 快照完成。

## 12. 建议的联调顺序

当前最合适的推进顺序是：

1. 先在 `data-collector` 中继续完善映射规则
2. 用浏览器或脚本验证 `GET /health`
3. 验证 `GET /api/metrics/latest`
4. 用 `GET /api/metrics/raw` 对照真实传感器名称
5. 等最小字段集稳定后，再接入 `esp-monitor`
6. 最后实现 `bootstrap + WebSocket`

这个顺序可以显著降低定位成本：

- 采集问题在 PC 端查
- 映射问题在 `raw` 对照查
- 显示问题在 ESP32 侧查

## 13. 当前结论

当前方案已经在 `data-collector` 中完成了第一阶段最小落地：

- `LibreHardwareMonitor data.json` 已成功接入
- 原始传感器树可以被拉平并调试
- 已有稳定的标准化字段
- 已区分 `latest` 与 `raw`
- 已补充内存系统兜底

一句话总结：

当前 `data-collector` 已经具备“可读、可调、可给设备提供最小快照”的能力，下一步重点应放在字段继续补全和传输层扩展，而不是推翻当前链路重做。

## 14. 后续文档建议

接下来可以继续补这几份文档：

- `docs/protocol-v1.md`
- `docs/python-agent-current-implementation.md`
- `docs/esp32-firmware-architecture.md`
- `docs/bootstrap-and-ws-plan.md`

### data-collector 基础信息

项目定位
- `data-collector` 是本仓库的 PC 端常驻采集服务，对应总体方案中的 `Python Agent`
- 它负责从 Windows 主机采集硬件指标、完成标准化处理，并通过 `HTTP / WebSocket` 向设备侧提供数据

职责范围
- 通过 `pythonnet` 加载并调用 `LibreHardwareMonitorLib.dll`
- 周期性刷新硬件树与传感器数据
- 将原始传感器映射为稳定的统一字段
- 缓存当前快照与必要的历史数据
- 提供健康检查、bootstrap、最新指标、原始调试数据等 `HTTP` 接口
- 通过 `WebSocket` 广播实时指标给 `esp-monitor`

不负责的内容
- 不负责具体副屏页面布局、像素坐标、动画与渲染细节
- 不在接口层直接编写传感器匹配逻辑
- 不耦合 ESP32 显示实现细节

推荐技术栈
- `Python 3.11+`
- `pythonnet`
- `LibreHardwareMonitorLib.dll`
- `FastAPI`
- `uvicorn`
- `Pydantic`

核心接口约定
- `GET /health`
  - 返回进程健康状态、版本、运行时长、最近采集耗时等信息
- `GET /api/device/bootstrap`
  - 返回设备可直接使用的完整默认配置
- `GET /api/metrics/latest`
  - 返回当前或最近一次标准化指标快照
- `GET /api/metrics/raw`
  - 返回原始传感器列表，仅供开发调试使用
- `WS /ws/metrics`
  - 按固定周期向设备推送实时指标

指标模型约定
- 推荐分组：`cpu`、`memory`、`gpu`、`network`、`storage`、`system`
- 百分比字段统一使用 `_pct`
- 温度字段统一使用 `_c`
- 频率字段统一使用 `_mhz`
- 功耗字段统一使用 `_w`
- 转速字段统一使用 `_rpm`
- 容量字段统一使用 `_mb`
- 网络速率统一使用 `kBps`

实现原则
- 采集层输出原始传感器
- 映射层负责标准化字段和单位
- 接口层只返回标准模型，不直接处理硬件匹配规则
- 字段名稳定，不依赖不同机器上的传感器显示名称
- 同类硬件厂商差异应在映射层吸收
- 获取不到的值允许为 `null`

建议目录方向
- `src/core`
  - 放置 LHM 读取、原始传感器刷新与多数据源采集逻辑
- `src/services`
  - 放置采集调度、缓存和广播服务
- `src/schemas`
  - 放置指标模型与 bootstrap 模型
- `src/router`
  - 放置 HTTP 与 WebSocket 路由
- `src/config`
  - 放置服务配置、设备默认配置与协议版本配置

开发目标
- 先打通 `LibreHardwareMonitor -> 标准 JSON -> HTTP / WebSocket`
- 优先保证可调试性、字段稳定性和长时间运行稳定性
- 为后续多设备、字段扩展和协议升级预留 `schema` 与 `device_id`

### 这是一个pc性能副屏项目

项目定位
- 这是一个基于 `Windows PC + Python Agent + ESP32-C3 + TFT` 的性能副屏项目
- 当前确定的总体方案为：`LibreHardwareMonitor -> Python Agent -> HTTP / WebSocket -> ESP32-C3 -> TFT`
- 项目目标是把 PC 端硬件采集、指标标准化与传输逻辑放在上位机，把 ESP32 侧职责收敛为联网、收包、解析和显示

架构概览
- `LibreHardwareMonitor`
  - 负责从 Windows 主机读取 CPU、内存、GPU、主板、存储、风扇等硬件传感器
- `Python Agent`
  - 负责通过 `pythonnet` 调用 `LibreHardwareMonitorLib.dll`
  - 负责传感器刷新、指标筛选、字段标准化、缓存和对外接口
- `Transport API`
  - 通过 `HTTP` 提供健康检查、启动配置和调试接口
  - 通过 `WebSocket` 持续推送实时指标
- `ESP32 Firmware`
  - 负责联网、请求 bootstrap、接收指标、维护本地状态并渲染副屏 UI

主要有两个子项目
- data-collector: 基于python的数据采集器
- esp-monitor: 基于ESP32-C3的硬件性能监控器

子项目职责
- `data-collector`
  - 对应方案中的 `Python Agent`
  - 负责 `LibreHardwareMonitor` 接入、统一指标模型、采集缓存、HTTP / WebSocket 服务
  - 只关心“如何从 PC 获取并提供标准化指标”，不耦合具体屏幕布局、像素坐标和动画实现
- `esp-monitor`
  - 对应方案中的 `ESP32 Firmware`
  - 负责配网、拉取配置、接收指标、解析协议、状态管理和副屏显示
  - 只关心“如何消费协议并稳定显示”，不承担 Windows 端硬件采集逻辑

推荐技术路线
- PC 端运行时：`Python 3.11+`
- .NET 互操作：`pythonnet`
- 硬件采集：`LibreHardwareMonitorLib.dll`
- 服务框架：`FastAPI`
- ASGI Server：`uvicorn`
- 数据模型：`Pydantic`
- 固件端：`ESP32-C3`

协议与接口约定
- 初始化和调试走 `HTTP`
- 实时指标推送走 `WebSocket`
- 指标协议使用统一 JSON 结构，字段名稳定，不依赖原始传感器显示名
- 所有时间字段使用带时区偏移的 `ISO 8601`
- 所有接口和指标模型应预留 `schema` 或 `metrics_schema_version`
- 获取不到的指标允许返回 `null`

推荐统一指标分组
- `cpu`
- `memory`
- `gpu`
- `network`
- `storage`
- `system`

开发边界
- 不要在 `esp-monitor` 中编写 Windows 传感器采集逻辑
- 不要在 `data-collector` 中耦合具体屏幕 UI 布局与动画实现
- PC 平台绑定逻辑统一放在 `data-collector`
- 显示与设备侧状态机逻辑统一放在 `esp-monitor`

工具映射表
 
| 操作            | 使用工具     | 禁止                            |
|-----------------|--------------|---------------------------------|
| PowerShell 版本 | 7.x.x 及以上 | 5.x.x / Windows 自带 PowerShell |

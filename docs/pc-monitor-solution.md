# ESP32-C3 PC 性能副屏方案

## 1. 方案目标

这版方案将 PC 端统一改为：

`LibreHardwareMonitor -> Python Agent -> HTTP / WebSocket -> ESP32-C3 -> TFT`

核心目标：

- 使用 `Python` 直接调用 `LibreHardwareMonitor` 读取硬件指标
- 由 `Python Agent` 对指标做标准化、缓存和对外传输
- 通过 `HTTP` 提供启动配置和调试接口
- 通过 `WebSocket` 持续推送实时指标给 ESP32
- 让 ESP32 只负责联网、收包、解析和显示，不承担复杂业务逻辑

这比把采集逻辑塞进 ESP32 更清晰，也更方便后续调试、扩展和替换协议。

## 2. 总体架构

采用四层结构：

`LibreHardwareMonitor -> Python Collector -> Transport API -> ESP32 Firmware`

职责划分如下。

### 2.1 LibreHardwareMonitor

仅负责从 Windows 主机读取硬件传感器数据，例如：

- CPU 占用率、温度、频率、功耗
- 内存占用
- GPU 占用率、温度、热点温度、显存占用、功耗
- 主板或 SSD 温度
- 风扇转速

它不直接与 ESP32 通信。

### 2.2 Python Agent

这是整个系统的核心中间层，建议长期常驻运行。

职责：

- 通过 `pythonnet` 调用 `LibreHardwareMonitorLib.dll`
- 周期性扫描并刷新硬件传感器
- 从原始传感器列表中筛选出我们关心的指标
- 将不同硬件厂商和传感器命名差异映射成统一字段
- 缓存当前快照和最近一段历史数据
- 对外提供 `HTTP` 和 `WebSocket` 接口
- 提供健康状态、版本信息、调试输出和配置管理

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
- 不让 ESP32 依赖 `LibreHardwareMonitor` 的内部结构
- 所有和 PC 平台绑定的逻辑都放在 Python Agent

## 3. PC 端技术路线

PC 端推荐采用以下技术组合：

- 运行时：`Python 3.11+`
- .NET 互操作：`pythonnet`
- 硬件采集：`LibreHardwareMonitorLib.dll`
- HTTP / WebSocket 服务：`FastAPI`
- ASGI Server：`uvicorn`
- 数据模型：`Pydantic`

### 3.1 为什么用 Python

你的目标是“用 Python 调 LibreHardwareMonitor 并写接口传输指标”，这条路线非常适合原型和正式版前期开发：

- `Python` 写接口和数据处理速度快
- `FastAPI` 做 REST 和 WebSocket 很顺手
- 方便本地调试、打印、抓包和快速改字段
- 后续如果协议稳定，再决定是否迁移到别的语言

### 3.2 Python 调用 LibreHardwareMonitor 的方式

推荐方式：

1. 项目中放入 `LibreHardwareMonitorLib.dll`
2. 安装 `pythonnet`
3. 在 Python 中通过 `clr.AddReference(...)` 加载 DLL
4. 创建 `Computer()` 对象并启用需要的硬件类别
5. 遍历硬件树和传感器树
6. 将传感器值转换成统一的业务指标

示意代码：

```python
import clr
from pathlib import Path

dll_path = Path("third_party/LibreHardwareMonitor/LibreHardwareMonitorLib.dll").resolve()
clr.AddReference(str(dll_path))

from LibreHardwareMonitor.Hardware import Computer

computer = Computer()
computer.IsCpuEnabled = True
computer.IsGpuEnabled = True
computer.IsMemoryEnabled = True
computer.IsMotherboardEnabled = True
computer.IsStorageEnabled = True
computer.IsControllerEnabled = True
computer.Open()

for hardware in computer.Hardware:
    hardware.Update()
    print(hardware.Name, hardware.HardwareType)
    for sensor in hardware.Sensors:
        print(sensor.Name, sensor.SensorType, sensor.Value)
```

注意：

- 某些传感器值需要先执行 `hardware.Update()`
- 某些子硬件需要递归更新
- 不同机器的传感器名称可能不同，不能把显示名当稳定接口

## 4. 推荐的软件模块拆分

建议 Python Agent 按下面拆分：

- `collector/lhm_reader.py`
  - 负责加载 DLL、初始化 `Computer`、刷新硬件树、输出原始传感器
- `collector/sensor_mapper.py`
  - 负责把原始传感器映射成统一字段
- `services/metrics_service.py`
  - 负责周期采集、缓存最新快照、维护历史数据
- `api/http_routes.py`
  - 提供 HTTP 接口
- `api/ws_routes.py`
  - 提供 WebSocket 推送
- `models/metrics.py`
  - 定义统一 JSON 数据模型
- `config/settings.py`
  - 维护采样周期、推送周期、端口、主题配置
- `main.py`
  - 应用入口，启动采集循环和 API 服务

### 4.1 与当前仓库结构的对应关系

根据当前仓库约定，这套方案应直接映射到以下两个子项目：

- `data-collector`
  - 对应 PC 端常驻采集服务
  - 负责 `LibreHardwareMonitor` 接入、指标标准化、HTTP / WebSocket 服务
- `esp-monitor`
  - 对应 ESP32-C3 固件工程
  - 负责联网、拉取配置、接收指标、渲染副屏 UI

也就是说，文档里的抽象名称和仓库目录应这样理解：

- `Python Agent` = `data-collector`
- `ESP32 Firmware` = `esp-monitor`

这样做的好处是职责边界天然清晰：

- `data-collector` 只关心“如何从 PC 获取并提供标准化指标”
- `esp-monitor` 只关心“如何消费协议并稳定显示”

不建议把以下内容混在同一个子项目中：

- 在 `esp-monitor` 中写 Windows 传感器采集逻辑
- 在 `data-collector` 中耦合具体屏幕布局、像素坐标和动画实现

### 4.2 建议的数据采集端目录

如果后续直接在 `data-collector` 中落地，建议目录更贴近仓库现状，而不是额外再套一层 `pc-agent/`：

```txt
data-collector/
  main.py
  requirements.txt
  app/
    api/
      http_routes.py
      ws_routes.py
    collector/
      lhm_reader.py
      sensor_mapper.py
      fallback_sources.py
    models/
      metrics.py
      bootstrap.py
    services/
      metrics_service.py
      broadcast_service.py
    config/
      settings.py
      device_profile.py
  third_party/
    LibreHardwareMonitor/
      LibreHardwareMonitorLib.dll
  tests/
    test_sensor_mapper.py
    test_metrics_service.py
    test_protocol_examples.py
```

其中建议补一个 `fallback_sources.py`，专门承接 `psutil`、Windows 性能计数器等非 `LibreHardwareMonitor` 数据源，避免这些逻辑散落在主流程里。

### 4.3 建议的固件端目录

如果在 `esp-monitor` 中落地，建议至少按职责拆到下面几个模块：

```txt
esp-monitor/
  src/
    main.cpp
    app/
      network/
        wifi_manager.cpp
        agent_client.cpp
      protocol/
        bootstrap_parser.cpp
        metrics_parser.cpp
      state/
        app_state.cpp
        metrics_cache.cpp
      ui/
        screen_renderer.cpp
        page_dashboard.cpp
        page_offline.cpp
      system/
        config_store.cpp
        heartbeat.cpp
```

核心原则仍然是：

- `network` 负责连接和收发
- `protocol` 负责 JSON 到结构体的转换
- `state` 负责本地缓存和状态机
- `ui` 只负责显示

这样后续更换屏幕驱动、切换 UI 风格，或者把 WebSocket 改为别的传输层时，改动面都会更可控。

## 5. 指标采集与标准化

`LibreHardwareMonitor` 输出的是大量原始传感器，不能直接原样发给 ESP32。

需要在 Python Agent 内做一层标准化。

### 5.1 推荐统一后的指标结构

建议聚合成如下逻辑分组：

- `cpu`
- `memory`
- `gpu`
- `network`
- `storage`
- `system`

### 5.2 推荐字段

```json
{
  "type": "metrics",
  "schema": 1,
  "timestamp": "2026-04-09T20:30:00+08:00",
  "host": {
    "name": "DESKTOP-01"
  },
  "cpu": {
    "usage_pct": 37.2,
    "temp_c": 62.0,
    "clock_mhz": 4385,
    "power_w": 78.4,
    "fan_rpm": 1280
  },
  "memory": {
    "usage_pct": 51.4,
    "used_mb": 16422,
    "total_mb": 31916
  },
  "gpu": {
    "usage_pct": 71.0,
    "temp_c": 68.0,
    "hotspot_c": 81.0,
    "mem_used_mb": 6240,
    "mem_total_mb": 12288,
    "power_w": 182.3
  },
  "network": {
    "upload_kBps": 128.4,
    "download_kBps": 2340.8
  },
  "storage": {
    "ssd_temp_c": 43.0
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
- 网络速率这里统一使用 `kBps`，避免和 `kbps` 混淆

### 5.3 标准化规则

- 字段名稳定，不依赖传感器显示名称
- 不同厂商的同类指标映射到相同字段
- 获取不到的值允许为 `null`
- 数值单位在 Python 端统一处理
- ESP32 只认协议字段，不认原始传感器名字

例如：

- CPU Package 温度 -> `cpu.temp_c`
- Total CPU Usage -> `cpu.usage_pct`
- GPU Core 温度 -> `gpu.temp_c`
- GPU Hot Spot -> `gpu.hotspot_c`
- Memory Used / Available -> 计算 `memory.used_mb` 和 `memory.usage_pct`

## 6. 接口设计

推荐通信模型：

- 初始化和调试：`HTTP`
- 实时数据：`WebSocket`

### 6.1 HTTP 接口

#### `GET /health`

用于检查 Python Agent 是否正常运行。

响应示例：

```json
{
  "ok": true,
  "agent_version": "0.1.0",
  "uptime_s": 3600,
  "last_collect_ms": 42
}
```

#### `GET /api/device/bootstrap`

用于 ESP32 启动时获取配置。

响应示例：

```json
{
  "device_name": "My PC Monitor",
  "agent_version": "0.1.0",
  "refresh_ms": 1000,
  "theme": {
    "bg": "#0B1220",
    "fg": "#E5ECF6",
    "accent": "#7CF29A",
    "warn": "#FFB347",
    "danger": "#FF6B6B"
  },
  "layout": {
    "rotation": 1,
    "show_chart": true,
    "show_network": true,
    "show_disk": true
  },
  "metrics_schema_version": 1
}
```

#### `GET /api/metrics/latest`

用于浏览器、调试工具或开发阶段快速查看当前快照。

#### `GET /api/metrics/raw`

建议额外保留一个调试接口，输出原始传感器列表，仅供开发期使用。

这个接口非常有价值，因为不同机器上传感器命名差异很大，调试时需要先看原始值。

### 6.2 WebSocket 接口

#### `WS /ws/metrics`

ESP32 建立长连接后，Python Agent 按固定周期推送指标。

推送策略建议：

- 采集周期：`500ms`
- 推送周期：`1000ms`

这样可以兼顾平滑度和资源占用。

消息示例：

```json
{
  "type": "metrics",
  "schema": 1,
  "timestamp": "2026-04-09T20:30:00+08:00",
  "cpu": {
    "usage_pct": 37.2,
    "temp_c": 62.0
  },
  "memory": {
    "usage_pct": 51.4
  },
  "gpu": {
    "usage_pct": 71.0,
    "temp_c": 68.0
  }
}
```

### 6.3 状态接口或状态消息

建议保留状态消息，用于显示链路状态。

示例：

```json
{
  "type": "status",
  "schema": 1,
  "timestamp": "2026-04-09T20:31:10+08:00",
  "agent_online": true,
  "data_age_ms": 180,
  "warnings": []
}
```

### 6.4 推荐增加的接口约束

为了让 `data-collector` 和 `esp-monitor` 后续能独立演进，建议在文档阶段就明确下面几个协议约束：

- 所有接口都返回 `schema` 或 `metrics_schema_version`
- 所有时间字段使用 ISO 8601，且带时区偏移
- 所有数值字段默认使用 `number | null`
- 字段缺失表示“该版本协议不存在此字段”
- 字段存在但值为 `null` 表示“该字段存在，但本次采集未获得有效值”

建议补充以下响应头或语义约定：

- `GET /health` 不依赖采集成功，只反映服务进程是否可用
- `GET /api/metrics/latest` 如果当前没有有效数据，返回最近一次快照并附带 `data_age_ms`
- `GET /api/device/bootstrap` 应返回当前设备可直接使用的完整默认配置，避免 ESP32 侧拼装默认值

这几个约束看起来细，但对后面做兼容升级很重要。

### 6.5 推荐的设备标识与鉴权预留

当前阶段可以先不做复杂鉴权，但建议预留设备标识字段，避免后面扩展多设备时推倒重来。

推荐最小约定：

- ESP32 请求 bootstrap 时带上 `device_id`
- WebSocket 连接时带上 `device_id` 和 `firmware_version`
- `data-collector` 记录最近连接设备的基础信息和连接时间

例如：

- `GET /api/device/bootstrap?device_id=esp32c3-living-room`
- `WS /ws/metrics?device_id=esp32c3-living-room&fw=0.1.0`

这样后续即使只服务一台设备，也可以提前把日志、配置覆盖和问题排查链路铺好。

## 7. Python Agent 实现建议

### 7.1 采集流程

推荐流程：

1. 启动时加载 `LibreHardwareMonitorLib.dll`
2. 初始化 `Computer`
3. 每 `500ms` 执行一次采集任务
4. 递归更新所有硬件节点
5. 拉平所有传感器
6. 做字段映射和单位标准化
7. 更新最新快照缓存
8. 按推送周期广播给 WebSocket 客户端

### 7.2 递归更新子硬件

`LibreHardwareMonitor` 中有些数据挂在 `SubHardware` 上，建议递归更新。

示意代码：

```python
def update_hardware(hardware):
    hardware.Update()
    for sub in hardware.SubHardware:
        update_hardware(sub)
```

### 7.3 映射层不要写死在接口层

不要在 WebSocket 或 HTTP 路由里直接写传感器匹配逻辑。

正确做法是：

- 采集层负责输出原始传感器
- 映射层负责统一字段
- 接口层只负责返回标准模型

这样后续换字段、换 UI、换前端时不会牵一发动全身。

## 8. ESP32 侧接入方式

推荐流程：

1. ESP32 启动并联网
2. 请求 `GET /api/device/bootstrap`
3. 保存刷新间隔、主题和布局配置
4. 连接 `WS /ws/metrics`
5. 接收并解析 `metrics` 消息
6. 用本地缓存驱动 UI
7. 如果断开连接，则进入重连状态

断线降级建议：

- `1~3 秒` 无新数据：保留最后值并标记 `stale`
- `3~10 秒` 无新数据：显示 `reconnecting`
- `10 秒以上`：切到离线页

### 8.1 ESP32 本地状态机建议

为了让固件逻辑更稳定，建议把联网和数据接收过程建模成有限状态机，而不是在 `loop()` 里堆叠条件判断。

推荐状态：

- `BOOT`
- `WIFI_CONNECTING`
- `BOOTSTRAP_LOADING`
- `WS_CONNECTING`
- `ONLINE`
- `STALE`
- `RECONNECTING`
- `OFFLINE`

推荐状态迁移：

1. 上电后进入 `BOOT`
2. Wi-Fi 未连接时进入 `WIFI_CONNECTING`
3. Wi-Fi 连通后请求 bootstrap，进入 `BOOTSTRAP_LOADING`
4. bootstrap 成功后建立 WebSocket，进入 `WS_CONNECTING`
5. 收到第一帧 `metrics` 后进入 `ONLINE`
6. 超时未收到新数据时进入 `STALE` 或 `RECONNECTING`
7. 连续失败达到阈值后进入 `OFFLINE`

这样设计的好处是：

- UI 可以直接根据状态切换页面或提示文案
- 网络层错误不会污染渲染层逻辑
- 后续增加配网页、错误提示页时更容易扩展

### 8.2 建议的重连策略

建议采用指数退避上限策略，避免网络异常时频繁重连：

- 第 1 次失败：`1s`
- 第 2 次失败：`2s`
- 第 3 次失败：`5s`
- 后续失败：固定 `10s`

同时建议区分两类失败：

- Wi-Fi 未连接：优先恢复 Wi-Fi，不要急于重连 Agent
- Wi-Fi 正常但 Agent 不可达：优先重试 HTTP / WebSocket

如果后续要做更好的用户体验，可以在离线页上显示：

- 当前 Wi-Fi SSID
- Agent 地址
- 最近一次成功收包时间
- 错误码或错误原因摘要

## 9. 开发阶段建议

### 阶段 1：Python 读取 LibreHardwareMonitor

目标：

- 成功从 Python 加载 `LibreHardwareMonitorLib.dll`
- 在控制台打印 CPU / 内存 / GPU 关键传感器

验收：

- 能稳定看到传感器名称、类型和值

### 阶段 2：统一指标模型

目标：

- 完成从原始传感器到标准 JSON 的映射
- 能输出 `GET /api/metrics/latest`

验收：

- 浏览器访问接口可看到结构稳定的 JSON

### 阶段 3：WebSocket 推送

目标：

- ESP32 或测试客户端能持续收到 `metrics`

验收：

- 连接 10 分钟以上无明显异常断流

### 阶段 4：ESP32 UI 接入

目标：

- 副屏正常显示 CPU / RAM / GPU 信息
- 断线状态可见

### 阶段 5：联调与长期运行验证

目标：

- `data-collector` 连续运行 `24h` 无明显内存泄漏
- `esp-monitor` 连续显示 `24h` 无明显卡死、花屏或重连风暴
- Wi-Fi 短暂断开后，链路可自动恢复

验收：

- 服务端内存占用波动可控
- WebSocket 长连接可恢复
- ESP32 在断网、重启 Agent、PC 息屏后都能给出合理状态反馈

## 10. 项目目录建议

如果后续按这个方案落地，PC 端项目建议类似这样组织：

```txt
pc-agent/
  main.py
  requirements.txt
  app/
    api/
      http_routes.py
      ws_routes.py
    collector/
      lhm_reader.py
      sensor_mapper.py
    models/
      metrics.py
    services/
      metrics_service.py
    config/
      settings.py
  third_party/
    LibreHardwareMonitor/
      LibreHardwareMonitorLib.dll
```

不过结合当前仓库实际，更推荐直接把该目录结构吸收到 `data-collector/` 下，而不是再新建一个并列的 `pc-agent/` 目录，否则会让仓库出现职责重复的入口。

## 10.1 配置文件建议

建议将运行参数拆成“服务配置”和“设备展示配置”两类。

服务配置建议包括：

- Agent 监听地址与端口
- 采集周期
- 推送周期
- 日志级别
- 是否启用原始传感器调试接口
- LHM DLL 路径

设备展示配置建议包括：

- 刷新频率
- 页面布局
- 是否显示网络信息
- 是否显示图表
- 颜色主题
- 屏幕旋转方向

建议示例：

```json
{
  "agent": {
    "host": "0.0.0.0",
    "port": 8787,
    "collect_interval_ms": 500,
    "push_interval_ms": 1000,
    "enable_raw_metrics": true
  },
  "device_defaults": {
    "refresh_ms": 1000,
    "layout": {
      "rotation": 1,
      "show_chart": true,
      "show_network": true,
      "show_disk": true
    }
  }
}
```

后续如果需要支持多台 ESP32，可以继续扩展成：

- 默认配置
- 按 `device_id` 覆盖配置

这样不会破坏现有接口设计。

## 11. 风险与注意事项

### 11.1 传感器名称不稳定

不同主板、CPU、GPU 平台下，`LibreHardwareMonitor` 暴露的名称可能不同。

所以映射逻辑应尽量基于：

- 硬件类型
- 传感器类型
- 名称关键词

并允许配置覆盖，而不是完全写死。

### 11.2 网络指标可能不适合完全依赖 LibreHardwareMonitor

如果 `LibreHardwareMonitor` 对网络吞吐支持不稳定，可以改由 Python 直接结合：

- `psutil`
- Windows 性能计数器

再统一并入同一份指标 JSON。

### 11.3 Python 与 .NET 运行时兼容

`pythonnet`、Python 版本、.NET 运行时版本之间需要实际验证。

建议尽早在目标 Windows 机器上做一次最小可运行验证。

### 11.4 JSON 体积与 ESP32 内存压力

虽然 JSON 调试方便，但对 ESP32-C3 来说仍要注意内存占用。

建议控制策略：

- 默认只推送当前页面需要的核心字段
- 历史图表数据不要每次整包下发
- 尽量避免长字段名无限扩张
- bootstrap 返回静态配置，metrics 只返回动态值

如果后面发现 JSON 解析和内存占用成为瓶颈，再考虑：

- 精简字段
- 分主题推送
- 改为 MessagePack 等二进制协议

但在第一阶段，优先保证可调试性，不建议过早优化协议复杂度。

### 11.5 多来源指标的一致性问题

如果后续网络、磁盘、系统运行时间等指标部分来自 `psutil`，部分来自 `LibreHardwareMonitor`，要注意采样时间和单位统一。

建议约束：

- 同一帧 `metrics` 中的字段应尽量来自同一次采样周期
- 单位换算在 `data-collector` 内完成
- 协议层不暴露“这个值来自哪个采集库”

这样 ESP32 侧才不会随着数据源调整而反复改解析逻辑。

## 12. 建议的联调顺序

为了降低联调成本，建议按下面顺序推进，而不是一开始就把全链路同时写完：

1. 先在 `data-collector` 中打通 LHM 读取与标准 JSON 输出
2. 用浏览器或脚本验证 `GET /health`、`GET /api/metrics/latest`
3. 用 PC 上的 WebSocket 测试客户端验证推流稳定性
4. 再接入 `esp-monitor` 的 bootstrap 与 WebSocket
5. 最后再做 UI 页面、美化和动画

这个顺序能明显降低问题定位难度：

- 采集错了，就在 PC 端查
- 协议错了，就在 JSON 层查
- 显示错了，就在 ESP32 侧查

## 13. 最终建议

这版方案建议正式确定为：

`LibreHardwareMonitor -> Python Agent -> HTTP bootstrap + WebSocket metrics -> ESP32-C3 -> TFT`

其中最关键的设计原则是：

- 用 `Python` 负责采集、清洗、标准化和传输
- 用 `HTTP` 提供配置和调试接口
- 用 `WebSocket` 负责实时指标推送
- 用统一 JSON 协议隔离 PC 端采集实现和 ESP32 显示实现

一句话总结：

先把 `Python + LibreHardwareMonitor + 统一指标接口` 这层做好，ESP32 只做一个稳定的显示客户端。

## 14. 后续文档建议

接下来可以继续补这几份文档：

- `docs/python-pc-agent-design.md`
- `docs/protocol-v1.md`
- `docs/esp32-firmware-architecture.md`
- `docs/ui-wireframe.md`

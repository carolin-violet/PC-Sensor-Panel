# Python 获取 PC 指标的最小可行链路

## 1. 目标

先在 `data-collector` 中打通一条最小可运行链路，验证下面这件事已经成立：

`LibreHardwareMonitorLib.dll -> pythonnet -> Python -> FastAPI -> JSON`

当前阶段不追求完整架构，不引入 WebSocket、数据库、复杂状态管理和完整指标标准化，只先确认：

- Python 能成功加载 `LibreHardwareMonitorLib.dll`
- 能从 PC 读取到原始硬件传感器
- 能通过 HTTP 接口把数据返回出来

这一步的核心目的是验证运行环境、依赖和采集路径，而不是一次性做完整产品。

## 2. 最小范围

第一阶段只做下面几个能力：

- 启动 `FastAPI` 服务
- 加载 `LibreHardwareMonitorLib.dll`
- 刷新硬件树并读取原始传感器
- 暴露 `GET /health`
- 暴露 `GET /api/metrics/latest`

当前阶段先不做：

- WebSocket 推流
- 数据库存储
- 指标历史缓存
- 多设备管理
- 完整的统一字段映射
- ESP32 联调

## 3. 建议链路

最小链路如下：

1. Python 启动时初始化 `LHMReader`
2. `LHMReader` 通过 `pythonnet` 加载 `LibreHardwareMonitorLib.dll`
3. 创建 `Computer()` 并启用 CPU / GPU / Memory / Motherboard / Storage
4. 每次访问接口时刷新一次硬件树
5. 收集原始传感器列表
6. 通过 `FastAPI` 返回 JSON

一句话理解：

先把“读到数据”这件事做出来，再把“字段清洗和协议稳定化”放到下一阶段。

## 4. 当前仓库中的最小落地结构

建议只使用下面几个文件：

```txt
data-collector/
  pyproject.toml
  src/
    main.py
    config/
      conf.py
    core/
      LibreHardwareMonitorLib.dll
      lhm_reader.py
```

说明：

- `src/config/conf.py`
  - 放基础配置，例如端口、采集周期、DLL 路径
- `src/core/lhm_reader.py`
  - 放与 `LibreHardwareMonitor` 的最小集成逻辑
- `src/main.py`
  - 放 `FastAPI` 应用和最小 HTTP 接口
- `src/core/LibreHardwareMonitorLib.dll`
  - 放 `LibreHardwareMonitor` 的 DLL 文件

## 5. 依赖建议

最小依赖建议如下：

- `fastapi`
- `uvicorn`
- `pythonnet`
- `pydantic`
- `pydantic-settings`

其中：

- `pythonnet` 用于加载 .NET DLL
- `FastAPI` 用于快速暴露调试接口
- `pydantic-settings` 用于读取运行配置

## 6. DLL 放置建议

当前约定把 `LibreHardwareMonitorLib.dll` 放在：

```txt
data-collector/src/core/LibreHardwareMonitorLib.dll
```

并在配置中提供路径字段：

- `LHM_DLL_PATH`

这样后续如果 DLL 位置变化，只需要改配置，不需要改采集代码。

## 7. 最小接口设计

### 7.1 `GET /health`

用途：

- 检查服务是否启动成功

返回示例：

```json
{
  "ok": true,
  "app_name": "PC Sensor Panel Data Collector",
  "version": "0.1.0"
}
```

### 7.2 `GET /api/metrics/latest`

用途：

- 返回当前一次采集得到的原始传感器数据

当前阶段建议直接返回原始传感器列表，不做复杂协议包装，便于先确认采集是否正常。

返回示例：

```json
{
  "ok": true,
  "sensors": [
    {
      "hardware": "Intel Core i7-13700K",
      "hardware_type": "Cpu",
      "sensor": "CPU Package",
      "sensor_type": "Temperature",
      "value": 62.0
    }
  ]
}
```

## 8. 最小采集实现思路

### 8.1 初始化步骤

初始化时完成以下动作：

1. 读取 `LHM_DLL_PATH`
2. 调用 `clr.AddReference(...)`
3. 导入 `LibreHardwareMonitor.Hardware.Computer`
4. 启用需要的硬件类别
5. 调用 `Open()`

### 8.2 刷新步骤

每次读取时执行：

1. 遍历 `computer.Hardware`
2. 对每个硬件调用 `Update()`
3. 递归更新 `SubHardware`
4. 遍历 `Sensors`
5. 收集名称、类型和值

### 8.3 当前阶段为什么先保留原始传感器

因为不同机器上的传感器名称差异很大，当前最重要的是先观察真实输出，例如：

- `CPU Package`
- `CPU Total`
- `GPU Core`
- `GPU Hot Spot`
- `Used Memory`

只有先看到这些原始值，下一步才能准确设计映射规则。

## 9. 建议的阶段拆分

### 阶段 1：打通 DLL 与原始采集

目标：

- Python 能成功加载 `LibreHardwareMonitorLib.dll`
- 能返回原始传感器列表

验收：

- 访问 `GET /api/metrics/latest` 时能看到 CPU / 内存 / GPU 相关原始传感器

### 阶段 2：做最小字段标准化

目标：

- 从原始传感器中提炼几个最关键字段

建议先只做：

- `cpu.usage_pct`
- `cpu.temp_c`
- `memory.used_mb`
- `memory.total_mb`
- `memory.usage_pct`
- `gpu.usage_pct`
- `gpu.temp_c`

验收：

- 返回结构稳定的最小指标 JSON

### 阶段 3：引入推送与设备侧联调

目标：

- 增加 `WebSocket`
- 与 `esp-monitor` 联调

这一步放在原始采集验证稳定之后再开始。

## 10. 运行与验证建议

建议按下面顺序验证：

1. 先单独运行 `FastAPI`
2. 打开浏览器访问 `GET /health`
3. 再访问 `GET /api/metrics/latest`
4. 观察是否能持续返回数据
5. 记录当前机器上的关键原始传感器名称

建议本阶段重点观察：

- 是否能成功加载 DLL
- 是否存在空值
- 是否存在某些硬件类别读取不到
- 是否需要递归更新 `SubHardware`

## 11. 当前阶段的成功标准

满足下面两个条件，就说明最小链路已经跑通：

- 服务能稳定启动，并能访问 `GET /health`
- `GET /api/metrics/latest` 能返回原始硬件传感器 JSON

达到这个标准后，下一步再做字段映射和协议稳定化，成本会低很多。

## 12. 下一步建议

最小链路打通后，优先继续做下面两件事：

1. 新增 `sensor_mapper.py`，把原始传感器映射为稳定字段
2. 为 `data-collector` 定义第一版 `metrics` JSON 协议

不建议在最小链路还未跑通前就提前接入：

- ESP32 显示逻辑
- WebSocket 推流
- 多页面 UI
- 长期运行优化

先证明“能稳定读到数据”，再推进后续架构，整体效率最高。

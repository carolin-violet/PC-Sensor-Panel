# esp-monitor

当前 `esp-monitor` 使用 `ESP-IDF + esp_lcd + ST7789` 驱动一块 `240x320 SPI TFT` 屏幕。

已验证可工作的组合：
- 开发板：`AirM2M CORE ESP32-C3`
- 屏幕：`TFT SPI 240x320 GMT024-10 V2.1`
- 工程框架：`ESP-IDF`
- 当前显示驱动实现：`esp_lcd_panel_st7789`

## 当前方案

当前工程不是 `Arduino + TFT_eSPI` 方案，而是：
- `framework = espidf`
- 使用 `ESP-IDF` 原生 `spi_master`
- 使用 `ESP-IDF` 原生 `esp_lcd`
- 面板驱动为 `ST7789`

这意味着：
- 屏幕初始化逻辑在 [src/main.cpp](/d:/study/code/PC-Sensor-Panel/esp-monitor/src/main.cpp)
- 屏幕引脚也在 [src/main.cpp](/d:/study/code/PC-Sensor-Panel/esp-monitor/src/main.cpp)
- [platformio.ini](/d:/study/code/PC-Sensor-Panel/esp-monitor/platformio.ini) 主要负责板卡、串口、flash 容量等工程配置

## 接线对应关系

当前代码里使用的引脚如下：

| 屏幕引脚 | 连接到 ESP32-C3 | 代码常量 |
|---|---|---|
| `GND` | `GND` | - |
| `VCC` | `3.3V` | - |
| `SCK` | `GPIO5` | `kPinSclk` |
| `SDA` | `GPIO4` | `kPinMosi` |
| `CS` | `GPIO12` | `kPinCs` |
| `DC` | `GPIO18` | `kPinDc` |
| `RST` | `GPIO19` | `kPinRst` |

当前实现里没有使用这些线：
- `MISO`
- `BL`
- `TE`

如果你的屏幕排针上还有这些引脚，当前版本可以先不接。

## 引脚含义说明

下面这些名字不只是“接到哪里”，它们本身也有明确意义：

| 引脚名 | 含义 | 当前作用 |
|---|---|---|
| `GND` | 地线，也可以理解为电源负极 / 公共参考电位 | 必须和开发板 `GND` 共地，否则信号没有参考基准，屏幕通常不会正常工作 |
| `VCC` | 电源正极 | 当前接 `3.3V`，给屏幕模块供电 |
| `SCK` | SPI 时钟线 | 由主控输出时钟，告诉屏幕“现在该采样数据了” |
| `SDA` | 屏幕丝印的数据输入线 | 在这块 SPI 屏上等价于 `MOSI`，用于主控向屏幕发送命令和像素数据 |
| `CS` | Chip Select，片选 | 用来选中当前屏幕设备；有效时，屏幕才会响应 SPI 通信 |
| `DC` | Data / Command 选择线 | 用来区分当前发过去的是“命令”还是“数据” |
| `RST` | Reset 复位线 | 用来硬件复位屏幕控制器，确保它进入一个已知状态 |
| `MISO` | Master In Slave Out | SPI 回读线；当前屏幕初始化不需要，所以未使用 |
| `BL` | Backlight 背光控制 | 只负责背光亮灭，不等于屏幕控制器工作正常；当前版本未使用 |
| `TE` | Tearing Effect 信号 | 一些屏用于同步刷新，当前版本未使用 |

可以把最关键的几根线简单记成：
- `GND`：电源负极
- `VCC`：电源正极
- `SCK`：时钟
- `SDA/MOSI`：发数据
- `CS`：选中设备
- `DC`：区分命令和数据
- `RST`：复位屏幕

## 当前硬件参数

当前这套已验证组合里，和屏幕 bring-up 直接相关的参数是：

| 参数 | 当前值 | 说明 |
|---|---|---|
| 屏幕分辨率 | `240x320` | 当前代码按竖屏分辨率初始化 |
| 通信方式 | `SPI` | 使用 ESP-IDF 原生 `spi_master` |
| 驱动方式 | `esp_lcd_panel_st7789` | 当前项目使用的面板驱动实现 |
| SPI Host | `SPI2_HOST` | 当前代码中使用的 SPI 控制器 |
| SPI 时钟 | `20MHz` | 当前用于稳定 bring-up，先以稳定优先 |
| 像素格式 | `RGB565 / 16-bit` | 当前按 16 位颜色数据发送 |
| 刷屏方式 | 分块刷屏 | 每次按 20 行缓冲区写入，减少单次传输压力 |

## 接线注意事项

### 1. 供电必须是 `3.3V`

当前这块屏按 `3.3V` 供电验证通过。  
如果供电不稳，常见现象是：
- 白屏
- 偶发花屏
- 复位后偶尔亮、偶尔不亮

### 2. `SDA` 在 SPI 屏上通常就是 `MOSI`

这块屏幕丝印写的是 `SDA`，但在 SPI 模式下它接的是主机发送线，也就是：
- `SDA -> GPIO4 -> MOSI`

不要把它按 I2C 的 `SDA` 去理解。

### 3. 当前没有使用背光控制

你提供的接线表里没有 `BL`，而当前代码也没有控制背光引脚。  
所以现在看到“屏亮了”只说明背光亮，不代表控制器已经初始化成功。

换句话说：
- 背光亮但一直白屏，不一定是代码没跑
- 更常见的是屏幕控制器没有收到正确初始化命令

### 4. `RST` 要可靠连接

当前代码会通过 `GPIO19` 控制屏幕硬复位。  
如果 `RST` 没接好，常见表现是：
- 上电一直白屏
- 偶尔第一次能亮，复位后又不行
- 串口日志正常，但屏幕始终没变化

### 5. 这套接线是按当前代码固定写死的

当前版本没有做“从配置文件动态读取引脚”，而是直接在 [src/main.cpp](/d:/study/code/PC-Sensor-Panel/esp-monitor/src/main.cpp) 里固定为：
- `MOSI = GPIO4`
- `SCLK = GPIO5`
- `CS = GPIO12`
- `DC = GPIO18`
- `RST = GPIO19`

如果你改了接线，代码也要一起改。

## 目前关键配置

[platformio.ini](/d:/study/code/PC-Sensor-Panel/esp-monitor/platformio.ini) 当前保留的是已验证可用配置：

- `board = airm2m_core_esp32c3`
- `framework = espidf`
- `upload_port = COM3`
- `monitor_port = COM3`
- `monitor_speed = 115200`
- `board_build.flash_size = 2MB`

说明：
- 实物 flash 已验证为 `2MB`
- 如果不显式覆盖，可能出现 `Expected 4MB, found 2MB`

## 白屏排查建议

如果后面 снова出现白屏，优先按这个顺序检查：

1. 先看串口日志里有没有 `st7789 bring-up start`
2. 再看日志里有没有持续输出 `fill screen: ...`
3. 确认 `VCC/GND` 稳定
4. 确认 `SCK/SDA/CS/DC/RST` 没接反
5. 重点复查 `RST` 和 `DC`
6. 如果日志正常但屏幕仍白，优先怀疑 `ST7789` 初始化方向、偏移或屏幕变体

## 常用命令

构建：

```powershell
C:\Users\we\.platformio\penv\Scripts\platformio.exe run
```

烧录：

```powershell
C:\Users\we\.platformio\penv\Scripts\platformio.exe run --target upload
```

串口监视：

```powershell
C:\Users\we\.platformio\penv\Scripts\platformio.exe device monitor --port COM3 --baud 115200
```

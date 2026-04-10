### esp-monitor 基础信息

项目定位
- `esp-monitor` 是本仓库的设备侧固件工程，对应总体方案中的 `ESP32 Firmware`
- 它负责连接 `data-collector`、获取配置、接收实时指标，并将数据稳定显示到 `TFT` 副屏

职责范围
- 设备配网与配置保存
- 请求 `bootstrap` 初始化配置
- 建立并维护 `WebSocket` 连接
- 解析标准化指标 JSON
- 维护本地状态机与指标缓存
- 根据当前状态渲染在线、陈旧、重连中、离线等页面

不负责的内容
- 不负责 Windows 端硬件采集
- 不依赖 `LibreHardwareMonitor` 的内部结构
- 不直接识别 PC 原始传感器名称
- 不承担协议字段标准化逻辑

对接对象
- 上位机服务为 `data-collector`
- 初始化接口使用 `HTTP`
- 实时指标通道使用 `WebSocket`
- 仅消费统一协议字段，不依赖采集实现细节

启动与运行流程
1. 启动设备并联网
2. 请求 `GET /api/device/bootstrap`
3. 保存刷新频率、布局、主题等默认配置
4. 连接 `WS /ws/metrics`
5. 接收 `metrics` 消息并更新本地缓存
6. 用缓存驱动 UI 渲染
7. 断线后按策略进入重连或离线状态

推荐状态机
- `BOOT`
- `WIFI_CONNECTING`
- `BOOTSTRAP_LOADING`
- `WS_CONNECTING`
- `ONLINE`
- `STALE`
- `RECONNECTING`
- `OFFLINE`

协议消费约定
- 所有消息按统一 JSON 协议解析
- 关注 `schema` 或 `metrics_schema_version` 做版本兼容
- 字段存在但值为 `null` 表示当前采集未获取到有效值
- 字段缺失表示当前协议版本未提供该字段
- 时间字段应按带时区偏移的 `ISO 8601` 处理

推荐模块边界
- `network`
  - 负责 Wi-Fi、HTTP、WebSocket 连接与重连
- `protocol`
  - 负责 bootstrap 和 metrics 的 JSON 解析
- `state`
  - 负责本地缓存、连接状态、超时判断和状态迁移
- `ui`
  - 负责页面绘制、主题应用和离线态展示
- `system`
  - 负责配置存储、心跳与设备运行时辅助逻辑

实现原则
- 只消费协议字段，不消费原始传感器名字
- 网络层、协议层、状态层、UI 层分离
- 断线重连和页面切换优先通过状态机驱动
- 优先保证稳定显示、可恢复性和低耦合

重连与降级建议
- `1~3 秒` 无新数据时保留最后值并标记为 `stale`
- `3~10 秒` 无新数据时进入 `reconnecting`
- `10 秒以上` 无新数据时切换到离线页
- 建议采用指数退避上限策略，避免异常情况下重连风暴

开发目标
- 先完成 `bootstrap + metrics` 的稳定接入
- 再补齐在线、断线、离线等状态页面
- 最终保证在 Wi-Fi 断开、Agent 重启等场景下可自动恢复

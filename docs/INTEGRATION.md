# 集成边界

`src/protocol` 的解析器依赖 C++ 标准库、`esp_log.h` 和 `Uart1Communicator::Uart1Send`。主机测试只替换日志与字节发送接口，生产解析器源码保持原有逻辑。

接收版本为 `03`、发送版本为 `00`；头 `55 AA`，负载长度为两字节大端，尾部为累加和低 8 位。接收单帧负载上限 512B，TLV 使用 `id/type/length/value`；value 为 32 位大端。布尔值解析保留原始字节；业务层应处理非 0/1 的异常上报。单帧多个 TLV 顺序回调，后续 TLV 损坏不会撤销之前的回调。

饮水机和制冰机目录是不同产品的适配，具有同名头/类，集成时选择对应目录，不能把两个目录同时加入无区分的 include 路径。所需外部接口包括原工程的 UART、MCP server、Application/Board、FreeRTOS 与 JSON/设备状态服务。

产测依赖 Board、显示/LED、音频输入模式切换、NVS、Wi-Fi 与项目 config 宏。实际测试 SSID/密码配置未随仓库提供。

音频补丁各自使用对应版本的小智和 ESP-SR/ESP-IDF。请以补丁中的依赖变化和原工程锁定文件为准；这里未复制完整依赖清单或模型。

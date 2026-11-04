#ifndef POTEK_MCU_PROTOCOL_H_
#define POTEK_MCU_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

// ============================================================================
// 制冰机制冰机串口协议（集贤 UAY058 · V1.0.1）
//
// 帧格式（短帧，6 字节定长）：
//   55 AA | 功能码 | 数据 | 校验(ADD8) | A5
//   校验 = (功能码 + 数据) & 0xFF
//
// 全属性上报帧（18 字节长帧）：
//   55 AA | 03 | 13 字节数据 | 校验(ADD8) | A5
//   校验 = sum(byte1 ~ byte16) & 0xFF
//
// 功能码：
//   0x01 语音→MCU 控制下发     0x02 MCU→语音 状态/事件上报
//   0x03 全属性查询/上报       0x05 大模型/WIFI 管理
//
// 注：文档 V1.0.1 中 05 02 08 / 05 03 09 / 05 06 0C 三条 WIFI 帧的校验字节
// 比 ADD8 规则计算值多 1（应为 07 / 08 / 0B），已确认为文档笔误。
// 本实现按 ADD8 规则严格收发（如配网指令 55 AA 05 06 0B A5）。
// ============================================================================

// 功能码。
enum class McuCommand : uint8_t {
    kControl = 0x01,   // 语音→MCU：控制下发
    kEvent = 0x02,     // MCU→语音：状态/事件上报
    kQueryAll = 0x03,  // 全属性查询（语音→MCU）/ 全属性上报（MCU→语音，18 字节）
    kWifi = 0x05,      // 大模型/WIFI 管理（双向）
};

// 功能码 0x01：语音→MCU 控制命令码。
enum class McuControlCode : uint8_t {
    kClearIce = 0x02,  // 透明冰模式
    kQuickIce = 0x03,  // 快速冰模式
    kClean = 0x04,     // 清洗模式（清洗时间 15 分钟）
    kPowerOff = 0x05,  // 关机
    kLightOn = 0x06,   // 灯光 / UV 消毒 开
    kLightOff = 0x07,  // 灯光 / UV 消毒 关
    // 预约关机：0x08 + (N - 1)，N ∈ [1, 24]，即 0x08~0x1F
    kDelayShutdownBase = 0x08,
    // 预约开机：0x20 + (N - 1)，N ∈ [1, 24]，即 0x20~0x37
    kDelayPowerOnBase = 0x20,
    kVoiceOn = 0x38,   // 开启语音
    kVoiceOff = 0x39,  // 关闭语音
    // 冰厚档位：0x50 + (level + 5)，level ∈ [-5, 5]，即 0x50~0x5A
    kIceThicknessBase = 0x50,
};

// 功能码 0x02：MCU→语音 事件码。
enum class McuEventCode : uint8_t {
    kClearIceStarted = 0x02,  // 透明冰启动确认
    kPowerOff = 0x03,         // 关机确认
    kIceFull = 0x04,          // 冰块已满
    kWaterShortage = 0x05,    // 水箱缺水
    kIceBatchDone = 0x06,     // 一轮冰制好
    kWaterPoor = 0x07,        // 水质较差
    kWaterVeryPoor = 0x08,    // 水质很差
    kFaultE1 = 0x09,          // 故障 E1
    kFaultE2 = 0x0A,          // 故障 E2
    kFaultE3 = 0x0B,          // 故障 E3
    kFaultE4 = 0x0C,          // 故障 E4
    kFaultE5 = 0x0D,          // 故障 E5
};

// 功能码 0x05：大模型/WIFI 命令码（双向）。
enum class McuWifiCode : uint8_t {
    kWifiOn = 0x02,              // 打开 WIFI（MCU→语音）
    kWifiOff = 0x03,             // 关闭 WIFI（MCU→语音）
    kWifiConnected = 0x04,       // WIFI 已连接（语音→MCU）
    kWifiDisconnected = 0x05,    // WIFI 已断开（语音→MCU）
    kStartNetworkConfig = 0x06,  // 开启配网（MCU→语音，播报引导语）
};

// 功能码 0x03：全属性上报（18 字节）解析结果。
// 数据区共 13 字节（byte4 ~ byte16）：
//   工作模式 / UV 开关 / 定时关机 / 定时开机 / TDS(2B) /
//   制冰剩余 / 清洗剩余 / 制冰数量(2B) / 故障状态(2B) / 冰厚档位
struct McuAllStatus {
    uint8_t work_mode = 0;              // 0=关机 1=快速冰 2=透明冰 3=清洗
    bool uv_on = false;                 // UV 消毒开关
    uint8_t delay_shutdown_hours = 0;   // 定时关机 0-24h（0=无预约）
    uint8_t delay_power_on_hours = 0;   // 定时开机 0-24h（0=无预约）
    uint16_t tds = 0;                   // TDS 水质值 0-999
    uint8_t remaining_ice_minutes = 0;  // 制冰剩余时间 0-255（分钟）
    uint8_t remaining_clean_minutes = 0;// 清洗剩余时间 0-255（分钟）
    uint16_t ice_count = 0;             // 制冰数量 0-65535（颗）
    uint8_t fault_flags = 0;            // bit1=冰满 bit2=缺水 bit3-7=E1~E5 bit8=预留
    uint8_t ice_thickness_index = 0;    // 1-11 对应 -5~+5（1 对应 -5 … 11 对应 +5）

    // 便捷访问：冰厚档位（-5~+5）。
    int8_t IceThickness() const {
        if (ice_thickness_index < 1) {
            return 0;
        }
        return static_cast<int8_t>(static_cast<int>(ice_thickness_index) - 6);
    }

    // 故障位：冰满 / 缺水 / 故障码（E1~E5，返回 0 表示无故障码）。
    bool IsIceFull() const { return (fault_flags & 0x01) != 0; }
    bool IsWaterShortage() const { return (fault_flags & 0x02) != 0; }
    uint8_t FaultCode() const { return (fault_flags >> 2) & 0x1F; }
};

class McuProtocol {
public:
    using EventCallback = std::function<void(McuEventCode)>;
    using WifiCallback = std::function<void(McuWifiCode)>;
    using AllStatusCallback = std::function<void(const McuAllStatus&)>;

    static McuProtocol& GetInstance();

    McuProtocol(const McuProtocol&) = delete;
    McuProtocol& operator=(const McuProtocol&) = delete;

    // Accepts arbitrary UART chunks. It handles incomplete frames, multiple
    // frames in one chunk, checksum validation, and header resynchronization.
    size_t ParseMcuData(const uint8_t* data, size_t length);
    void ResetParser();

    // 语音→MCU 控制下发（功能码 0x01）。
    bool SendControl(McuControlCode code);

    // 语音→MCU WIFI 状态通知（功能码 0x05，如 WIFI 已连接/已断开）。
    bool SendWifi(McuWifiCode code);

    // 语音→MCU 查询全属性（功能码 0x03，55 AA 03 00 03 A5）。
    bool QueryAllStatus();

    void SetEventCallback(EventCallback callback);
    void SetWifiCallback(WifiCallback callback);
    void SetAllStatusCallback(AllStatusCallback callback);

private:
    McuProtocol() = default;

    bool SendFrame(McuCommand command, uint8_t data);
    void ProcessFrame(const uint8_t* frame, size_t length);
    void ProcessAllStatus(const uint8_t* data, size_t length);
    void NotifyEvent(McuEventCode event);
    void NotifyWifi(McuWifiCode code);
    void NotifyAllStatus(const McuAllStatus& status);

    std::vector<uint8_t> rx_buffer_;
    std::mutex send_mutex_;
    std::mutex callback_mutex_;
    EventCallback event_callback_;
    WifiCallback wifi_callback_;
    AllStatusCallback all_status_callback_;
};

#endif  // POTEK_MCU_PROTOCOL_H_

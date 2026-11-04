#ifndef POTEK_MCU_PROTOCOL_H_
#define POTEK_MCU_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

enum class McuCommand : uint8_t {
    kHeartbeat = 0x00,        // 心跳：AI 模组发送，MCU 返回心跳状态
    kFunctionControl = 0x06,  // 功能控制/查询：AI 模组发送给 MCU
    kFunctionStatus = 0x07,   // 功能状态/响应：MCU 发送给 AI 模组
    kQueryAllStatus = 0x08,   // 查询全部状态：AI 模组发送给 MCU
};

enum class McuDataType : uint8_t {
    kBool = 0x01,   // 布尔型，数据长度固定为 1 字节
    kValue = 0x02,  // 数值型，数据长度固定为 4 字节无符号大端整数
    kEnum = 0x04,   // 枚举型，数据长度固定为 1 字节
};

enum class McuFunctionId : uint8_t {
    kPower = 0x01,                // 开关机，bool，AI→MCU / MCU→AI
    kWorkState = 0x02,            // 工作状态，enum，仅 MCU→AI
    kTargetTemperature = 0x03,    // 目标出水温度，value，25~100℃，双向
    kTargetWaterVolume = 0x04,    // 目标出水量，value，50~1000mL，双向
    kCurrentTemperature = 0x05,   // 当前出水温度，value，0~110℃，仅 MCU→AI
    kCurrentWaterVolume = 0x06,   // 当前/本次出水量，value，0~1000mL，仅 MCU→AI
    kWaterControl = 0x07,         // 出水控制，bool，0停止、1开始，双向
    kChildLock = 0x08,            // 童锁，bool，0解锁、1锁定，双向
    kTdsQuery = 0x09,             // TDS 查询触发，bool，发送1触发，仅 AI→MCU
    kTdsValue = 0x0A,             // TDS 值，value，0~999PPM，仅 MCU→AI
    kAutoCleanControl = 0x0B,     // 自动清洗控制，enum，双向
    kCleanReminder = 0x0C,        // 清洗提醒，bool，0无需、1需要，仅 MCU→AI
    kRemainingHeatingTime = 0x0D, // 剩余可加热时间，value，单位分钟，仅 MCU→AI
    kFaultCode = 0x0E,            // 故障码，enum，仅 MCU→AI
    kBeverageMode = 0x0F,         // 饮品模式，enum，双向
    kNetworkConfig = 0x10,        // 配网指令，enum，仅 MCU→AI
    kAiModuleState = 0x11,        // AI 模组状态，enum，仅 AI→MCU
    kInterruptBroadcast = 0x12,   // 打断播报，enum，仅 MCU→AI
};

// 功能 ID 0x02：MCU 当前工作状态。
enum class McuWorkState : uint8_t {
    kStandby = 0x00,       // 待机
    kDispensing = 0x01,    // 出水中
    kHeating = 0x02,       // 加热中
    kAutoCleaning = 0x03,  // 自动清洗中
    kFault = 0x04,         // 故障
};

// 功能 ID 0x0B：自动清洗控制/状态。
enum class McuAutoCleanState : uint8_t {
    kStopped = 0x00,  // 停止或空闲
    kStarted = 0x01,  // 开始清洗
};

// 功能 ID 0x0E：MCU 故障码。
enum class McuFaultCode : uint8_t {
    kNone = 0x00,                   // 正常
    kE1WaterShortage = 0x01,        // E1：缺水
    kE3FreezeProtection = 0x02,     // E3：防冻保护
    kE4OverTemperature = 0x03,      // E4：高温保护
    kE5HotSensorDisconnected = 0x04,  // E5：热水温感断开
    kE6HotSensorOffset = 0x05,        // E6：热水温感偏移
    kE7NormalSensorDisconnected = 0x06, // E7：常温温感断开
    kE8NormalSensorOffset = 0x07,   // E8：常温温感偏移
    kL3SensorGroupAlarm = 0x08,     // L3：温感集体报警
    kF7CleanReminder = 0x09,        // F7：清洗提醒
};

// 功能 ID 0x0F：饮品模式。预设模式默认出水量为 250mL。
enum class McuBeverageMode : uint8_t {
    kNormalWater = 0x00,  // 常温水，按常温水控制
    kGreenTea = 0x01,     // 绿茶，90℃
    kMilk = 0x02,         // 冲奶，45℃
    kCoffee = 0x03,       // 咖啡，95℃
    kBlackTea = 0x04,     // 红茶，100℃
    kLemonWater = 0x05,   // 柠檬水，60℃
    kHoney = 0x06,        // 蜂蜜，55℃
    kSesamePaste = 0x07,  // 芝麻糊，85℃
    kCustom = 0x08,       // 自定义，温度和水量由功能 ID 3、4 指定
};

// 功能 ID 0x10：MCU 控制 AI 模组进入或退出配网模式。
enum class McuNetworkConfigCommand : uint8_t {
    kExit = 0x00,
    kEnter = 0x01,
};

// 功能 ID 0x11：AI 模组向 MCU 上报当前状态。
enum class AiModuleState : uint8_t {
    kStandby = 0x00,
    kListening = 0x01,
    kBroadcasting = 0x02,
};

// 功能 ID 0x12：MCU 请求 AI 模组打断当前播报。
enum class McuBroadcastCommand : uint8_t {
    kInterrupt = 0x01,
};

struct McuFunctionData {
    McuFunctionId function_id;  // 收到的功能 ID
    McuDataType data_type;      // 收到的数据类型
    // 统一解析后的值：bool/enum 使用低 8 位，value 使用完整 32 位。
    uint32_t value;
};

class McuProtocol {
public:
    using FunctionDataCallback = std::function<void(const McuFunctionData&)>;
    using HeartbeatCallback = std::function<void(uint8_t heartbeat_state)>;

    static McuProtocol& GetInstance();

    McuProtocol(const McuProtocol&) = delete;
    McuProtocol& operator=(const McuProtocol&) = delete;

    // Accepts arbitrary UART chunks. It handles incomplete frames, multiple
    // frames in one chunk, checksum validation, and header resynchronization.
    size_t ParseMcuData(const uint8_t* data, size_t length);
    void ResetParser();

    bool SendBool(uint8_t function_id, bool value);
    bool SendEnum(uint8_t function_id, uint8_t value);
    bool SendValue(uint8_t function_id, uint32_t value);

    bool SendBool(McuFunctionId function_id, bool value) {
        return SendBool(static_cast<uint8_t>(function_id), value);
    }
    bool SendEnum(McuFunctionId function_id, uint8_t value) {
        return SendEnum(static_cast<uint8_t>(function_id), value);
    }
    bool SendEnum(McuFunctionId function_id, McuAutoCleanState value) {
        return SendEnum(function_id, static_cast<uint8_t>(value));
    }
    bool SendEnum(McuFunctionId function_id, McuBeverageMode value) {
        return SendEnum(function_id, static_cast<uint8_t>(value));
    }
    bool SendValue(McuFunctionId function_id, uint32_t value) {
        return SendValue(static_cast<uint8_t>(function_id), value);
    }

    bool SendHeartbeat();
    bool QueryAllStatus();

    void SetFunctionDataCallback(FunctionDataCallback callback);
    void SetHeartbeatCallback(HeartbeatCallback callback);

private:
    McuProtocol() = default;

    bool SendFunctionData(uint8_t function_id, McuDataType data_type,
                          const uint8_t* data, size_t length);
    bool SendFrame(McuCommand command, const uint8_t* data, size_t length);
    void ProcessFrame(const uint8_t* frame, size_t length);
    bool ProcessFunctionData(const uint8_t* data, size_t length);
    void NotifyFunctionData(const McuFunctionData& data);
    void NotifyHeartbeat(uint8_t heartbeat_state);

    std::vector<uint8_t> rx_buffer_;
    std::mutex send_mutex_;
    std::mutex callback_mutex_;
    FunctionDataCallback function_data_callback_;
    HeartbeatCallback heartbeat_callback_;
};

#endif  // POTEK_MCU_PROTOCOL_H_

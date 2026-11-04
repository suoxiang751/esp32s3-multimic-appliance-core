#ifndef POTEK_MCU_DP_H_
#define POTEK_MCU_DP_H_

#include <cstdint>
#include <functional>
#include <mutex>

#include "mcu_protocol.h"

// DP 状态缓存：AI 下发成功时乐观更新，MCU 上报时以实际值覆盖。
// valid_mask 用于区分“缓存尚无此值”与“此值有效且恰好为 0”。
struct McuDpState {
    uint16_t valid_mask = 0;

    bool power = false;  // DP 01：开关机
    McuWorkState work_state = McuWorkState::kStandby;  // DP 02：工作状态
    uint32_t target_temperature = 0;  // DP 03：目标温度，℃
    uint32_t target_water_volume = 0;  // DP 04：目标水量，mL
    uint32_t current_temperature = 0;  // DP 05：当前温度，℃
    uint32_t current_water_volume = 0;  // DP 06：当前/本次水量，mL
    bool water_running = false;  // DP 07：是否正在出水
    bool child_lock = false;  // DP 08：童锁
    bool tds_query_triggered = false;  // DP 09：TDS 查询触发标志
    uint32_t tds = 0;  // DP 10：TDS，PPM
    McuAutoCleanState auto_clean =
        McuAutoCleanState::kStopped;  // DP 11：自动清洗状态
    bool clean_required = false;  // DP 12：是否需要清洗
    uint32_t remaining_heating_minutes = 0;  // DP 13：剩余加热时间
    McuFaultCode fault_code = McuFaultCode::kNone;  // DP 14：故障码
    McuBeverageMode beverage_mode =
        McuBeverageMode::kNormalWater;  // DP 15：饮品模式

    bool HasValue(McuFunctionId function_id) const;
};

class McuDp {
public:
    // 回调参数包含本次变化的 DP ID 和更新后的完整状态快照。
    using StateChangedCallback =
        std::function<void(McuFunctionId, const McuDpState&)>;

    static McuDp& GetInstance();

    McuDp(const McuDp&) = delete;
    McuDp& operator=(const McuDp&) = delete;

    // UART1 启动前调用一次，用于注册协议层接收回调。
    void Start();

    // AI 模组可下发的 DP。
    bool SetPower(bool on);                                  // DP 01
    bool SetTargetTemperature(uint32_t temperature_celsius); // DP 03：25~100℃
    bool SetTargetWaterVolume(uint32_t volume_ml);           // DP 04：50~1000mL
    bool SetWaterControl(bool start);                        // DP 07
    bool SetChildLock(bool locked);                          // DP 08
    bool TriggerTdsQuery();                                  // DP 09：发送 1
    bool SetAutoClean(McuAutoCleanState state);              // DP 11
    bool SetBeverageMode(McuBeverageMode mode);              // DP 15
    bool ReportAiModuleState(AiModuleState state);            // DP 17

    bool SendHeartbeat();
    bool QueryAllStatus();

    McuDpState GetState() const;
    bool IsMcuOnline() const;
    uint8_t GetHeartbeatState() const;
    void SetStateChangedCallback(StateChangedCallback callback);

private:
    McuDp() = default;

    void HandleFunctionData(const McuFunctionData& data);
    void HandleDp01Power(const McuFunctionData& data);
    void HandleDp02WorkState(const McuFunctionData& data);
    void HandleDp03TargetTemperature(const McuFunctionData& data);
    void HandleDp04TargetWaterVolume(const McuFunctionData& data);
    void HandleDp05CurrentTemperature(const McuFunctionData& data);
    void HandleDp06CurrentWaterVolume(const McuFunctionData& data);
    void HandleDp07WaterControl(const McuFunctionData& data);
    void HandleDp08ChildLock(const McuFunctionData& data);
    void HandleDp09TdsQuery(const McuFunctionData& data);
    void HandleDp10TdsValue(const McuFunctionData& data);
    void HandleDp11AutoClean(const McuFunctionData& data);
    void HandleDp12CleanReminder(const McuFunctionData& data);
    void HandleDp13RemainingHeatingTime(const McuFunctionData& data);
    void HandleDp14FaultCode(const McuFunctionData& data);
    void HandleDp15BeverageMode(const McuFunctionData& data);
    void HandleDp16NetworkConfig(const McuFunctionData& data);
    void HandleDp18InterruptBroadcast(const McuFunctionData& data);
    void HandleHeartbeat(uint8_t heartbeat_state);
    bool ValidateData(const McuFunctionData& data, McuDataType expected_type,
                      uint32_t minimum, uint32_t maximum) const;
    void NotifyStateChanged(McuFunctionId function_id);

    mutable std::mutex mutex_;
    bool started_ = false;
    bool mcu_online_ = true;
    uint8_t heartbeat_state_ = 0;
    McuDpState state_;
    StateChangedCallback state_changed_callback_;
};

#endif  // POTEK_MCU_DP_H_

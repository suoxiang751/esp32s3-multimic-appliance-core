#ifndef POTEK_MCU_DP_H_
#define POTEK_MCU_DP_H_

#include <cstdint>
#include <functional>
#include <mutex>

#include "mcu_protocol.h"

// 制冰机 DP 标识：用于 valid_mask 与状态变化回调。
enum class McuDpId : uint8_t {
    kWorkMode = 0x01,          // 工作模式（关机/快速冰/透明冰/清洗）
    kUvLight = 0x02,           // UV 消毒开关
    kDelayShutdown = 0x03,     // 预约关机（小时）
    kDelayPowerOn = 0x04,      // 预约开机（小时）
    kTds = 0x05,               // TDS 水质值
    kRemainingIceTime = 0x06,  // 制冰剩余时间（分钟）
    kRemainingCleanTime = 0x07,// 清洗剩余时间（分钟）
    kIceCount = 0x08,          // 制冰数量（颗）
    kFault = 0x09,             // 故障状态
    kIceThickness = 0x0A,      // 冰厚档位
    kIceFull = 0x0B,           // 冰满事件
    kWaterShortage = 0x0C,     // 缺水事件
    kIceBatchDone = 0x0D,      // 一轮冰完成事件
};

// AI 模组状态：主应用 application.cc（CONFIG_BOARD_TYPE_POTEK_PTE21S_V1）
// 通过 ReportAiModuleState() 通知 MCU。新制冰机协议暂无对应上报帧，
// 接口保留仅为兼容主应用调用链。
enum class AiModuleState : uint8_t {
    kStandby = 0x00,
    kListening = 0x01,
    kBroadcasting = 0x02,
};

// DP 状态缓存：AI 下发成功时乐观更新，MCU 上报时以实际值覆盖。
// valid_mask 用于区分“缓存尚无此值”与“此值有效且恰好为 0”。
struct McuDpState {
    uint16_t valid_mask = 0;

    uint8_t work_mode = 0;             // DP：0=关机 1=快速冰 2=透明冰 3=清洗
    bool uv_on = false;                // DP：UV 消毒开关
    uint8_t delay_shutdown_hours = 0;  // DP：预约关机 0-24h
    uint8_t delay_power_on_hours = 0;  // DP：预约开机 0-24h
    uint16_t tds = 0;                  // DP：TDS 水质值 0-999
    uint8_t remaining_ice_minutes = 0; // DP：制冰剩余时间（分钟）
    uint8_t remaining_clean_minutes = 0; // DP：清洗剩余时间（分钟）
    uint16_t ice_count = 0;            // DP：制冰数量（颗）
    uint8_t fault_flags = 0;           // DP：bit1=冰满 bit2=缺水 bit3-7=E1~E5
    int8_t ice_thickness = 0;          // DP：冰厚档位 -5~+5
    bool ice_full = false;             // DP：冰满事件
    bool water_shortage = false;       // DP：缺水事件
    bool ice_batch_done = false;       // DP：一轮冰完成事件

    bool HasValue(McuDpId dp_id) const;
};

class McuDp {
public:
    // 回调参数包含本次变化的 DP ID 和更新后的完整状态快照。
    using StateChangedCallback =
        std::function<void(McuDpId, const McuDpState&)>;

    static McuDp& GetInstance();

    McuDp(const McuDp&) = delete;
    McuDp& operator=(const McuDp&) = delete;

    // UART1 启动前调用一次，用于注册协议层接收回调。
    void Start();

    // AI 模组可下发的控制（功能码 0x01）。
    bool SetClearIce();                            // 透明冰模式
    bool SetQuickIce();                            // 快速冰模式
    bool StartClean();                             // 清洗模式
    bool PowerOff();                               // 关机
    bool SetUvLight(bool on);                      // 灯光 / UV 消毒
    bool SetDelayShutdown(uint8_t hours);          // 预约关机 1-24h
    bool SetDelayPowerOn(uint8_t hours);           // 预约开机 1-24h
    bool SetIceThickness(int8_t thickness);        // 冰厚档位 -5~+5

    // WIFI 状态通知（功能码 0x05，语音→MCU）。
    bool ReportWifiConnected(bool connected);

    // 上报 AI 模组状态（兼容主应用调用；新协议暂无对应帧，仅记录日志）。
    bool ReportAiModuleState(AiModuleState state);

    // 查询全属性（功能码 0x03），MCU 回复后刷新缓存。
    bool QueryAllStatus();

    McuDpState GetState() const;
    void SetStateChangedCallback(StateChangedCallback callback);

private:
    McuDp() = default;

    void HandleEvent(McuEventCode event);
    void HandleWifi(McuWifiCode code);
    void HandleAllStatus(const McuAllStatus& status);
    bool HandleDelayCommand(uint8_t hours, uint8_t base_code,
                            McuDpId dp_id, uint8_t& cached);
    void NotifyStateChanged(McuDpId dp_id);

    mutable std::mutex mutex_;
    bool started_ = false;
    McuDpState state_;
    StateChangedCallback state_changed_callback_;
};

#endif  // POTEK_MCU_DP_H_

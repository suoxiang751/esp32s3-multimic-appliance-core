#include "mcu_dp.h"

#include <string>
#include <utility>

#include <esp_log.h>

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "config.h"
#include "wifi_board.h"
#include "wifi_manager.h"

namespace {

constexpr char kTag[] = "McuDp";
constexpr uint8_t kFirstDpId = 0x01;
constexpr uint8_t kLastDpId = 0x0D;

// 工作模式取值（全属性上报 byte4）。
constexpr uint8_t kModeOff = 0;       // 关机
constexpr uint8_t kModeQuickIce = 1;  // 快速冰
constexpr uint8_t kModeClearIce = 2;  // 透明冰
constexpr uint8_t kModeClean = 3;     // 清洗

// 预约开关机 / 冰厚档位命令码基数。
constexpr uint8_t kDelayShutdownCodeBase = 0x08;
constexpr uint8_t kDelayPowerOnCodeBase = 0x20;
constexpr uint8_t kIceThicknessCodeBase = 0x50;

// 故障码位（全属性 fault_flags，bit3-7 为 E1~E5）。
constexpr uint8_t kFaultBitIceFull = 0x01;
constexpr uint8_t kFaultBitWaterShortage = 0x02;
constexpr uint8_t kFaultCodeMask = 0x1F;

void RespondToMcuDp(const std::string& announcement) {
#if MCU_DP_USE_LOCAL_SOUND_RESPONSE
    (void)announcement;
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_DI_01);
#else
    Application::GetInstance().SendText(announcement);
#endif
}

uint16_t FunctionMask(McuDpId dp_id) {
    const uint8_t id = static_cast<uint8_t>(dp_id);
    if (id < kFirstDpId || id > kLastDpId) {
        return 0;
    }
    return static_cast<uint16_t>(1U << (id - kFirstDpId));
}

const char* WorkModeName(uint8_t mode) {
    switch (mode) {
        case kModeOff:
            return "关机";
        case kModeQuickIce:
            return "快速冰";
        case kModeClearIce:
            return "透明冰";
        case kModeClean:
            return "清洗";
        default:
            return "未知";
    }
}

const char* FaultCodeName(uint8_t fault_code) {
    switch (fault_code) {
        case 0:
            return "无";
        case 1:
            return "E1";
        case 2:
            return "E2";
        case 3:
            return "E3";
        case 4:
            return "E4";
        case 5:
            return "E5";
        default:
            return "未知";
    }
}

const char* EventAnnouncement(McuEventCode event) {
    switch (event) {
        case McuEventCode::kIceFull:
            return "冰块已满了，可以享用了";
        case McuEventCode::kWaterShortage:
            return "水箱缺水了，请加水";
        case McuEventCode::kIceBatchDone:
            return "一轮冰已经制好了";
        case McuEventCode::kWaterPoor:
            return "当前水质较差，请及时换水";
        case McuEventCode::kWaterVeryPoor:
            return "当前水质已经很差了，请立即换水";
        case McuEventCode::kFaultE1:
        case McuEventCode::kFaultE2:
        case McuEventCode::kFaultE3:
        case McuEventCode::kFaultE4:
        case McuEventCode::kFaultE5:
            return "机器工作异常，请取出冰块并关机后重试";
        default:
            return nullptr;
    }
}

uint8_t FaultCodeBit(McuEventCode event) {
    switch (event) {
        case McuEventCode::kFaultE1:
            return 0x04;  // bit3
        case McuEventCode::kFaultE2:
            return 0x08;  // bit4
        case McuEventCode::kFaultE3:
            return 0x10;  // bit5
        case McuEventCode::kFaultE4:
            return 0x20;  // bit6
        case McuEventCode::kFaultE5:
            return 0x40;  // bit7
        default:
            return 0;
    }
}

}  // namespace

bool McuDpState::HasValue(McuDpId dp_id) const {
    const uint16_t mask = FunctionMask(dp_id);
    return mask != 0 && (valid_mask & mask) != 0;
}

McuDp& McuDp::GetInstance() {
    static McuDp instance;
    return instance;
}

void McuDp::Start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            return;
        }
        started_ = true;
    }

    auto& protocol = McuProtocol::GetInstance();
    protocol.SetEventCallback(
        [this](McuEventCode event) { HandleEvent(event); });
    protocol.SetWifiCallback(
        [this](McuWifiCode code) { HandleWifi(code); });
    protocol.SetAllStatusCallback(
        [this](const McuAllStatus& status) { HandleAllStatus(status); });
}

bool McuDp::SetClearIce() {
    const bool sent =
        McuProtocol::GetInstance().SendControl(McuControlCode::kClearIce);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.work_mode = kModeClearIce;
            state_.valid_mask |= FunctionMask(McuDpId::kWorkMode);
        }
        ESP_LOGI(kTag, "TX clear-ice mode");
        NotifyStateChanged(McuDpId::kWorkMode);
    } else {
        ESP_LOGE(kTag, "TX clear-ice mode failed");
    }
    return sent;
}

bool McuDp::SetQuickIce() {
    const bool sent =
        McuProtocol::GetInstance().SendControl(McuControlCode::kQuickIce);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.work_mode = kModeQuickIce;
            state_.valid_mask |= FunctionMask(McuDpId::kWorkMode);
        }
        ESP_LOGI(kTag, "TX quick-ice mode");
        NotifyStateChanged(McuDpId::kWorkMode);
    } else {
        ESP_LOGE(kTag, "TX quick-ice mode failed");
    }
    return sent;
}

bool McuDp::StartClean() {
    const bool sent =
        McuProtocol::GetInstance().SendControl(McuControlCode::kClean);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.work_mode = kModeClean;
            state_.valid_mask |= FunctionMask(McuDpId::kWorkMode);
        }
        ESP_LOGI(kTag, "TX clean mode");
        NotifyStateChanged(McuDpId::kWorkMode);
    } else {
        ESP_LOGE(kTag, "TX clean mode failed");
    }
    return sent;
}

bool McuDp::PowerOff() {
    const bool sent =
        McuProtocol::GetInstance().SendControl(McuControlCode::kPowerOff);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.work_mode = kModeOff;
            state_.valid_mask |= FunctionMask(McuDpId::kWorkMode);
        }
        ESP_LOGI(kTag, "TX power off");
        NotifyStateChanged(McuDpId::kWorkMode);
    } else {
        ESP_LOGE(kTag, "TX power off failed");
    }
    return sent;
}

bool McuDp::SetUvLight(bool on) {
    const bool sent = McuProtocol::GetInstance().SendControl(
        on ? McuControlCode::kLightOn : McuControlCode::kLightOff);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.uv_on = on;
            state_.valid_mask |= FunctionMask(McuDpId::kUvLight);
        }
        ESP_LOGI(kTag, "TX UV light: %s", on ? "ON" : "OFF");
        NotifyStateChanged(McuDpId::kUvLight);
    } else {
        ESP_LOGE(kTag, "TX UV light failed: %s", on ? "ON" : "OFF");
    }
    return sent;
}

bool McuDp::HandleDelayCommand(uint8_t hours, uint8_t base_code,
                               McuDpId dp_id, uint8_t& cached) {
    if (hours < 1 || hours > 24) {
        ESP_LOGE(kTag, "Delay hours out of range: %u",
                 static_cast<unsigned>(hours));
        return false;
    }
    const auto code = static_cast<McuControlCode>(base_code + hours - 1);
    const bool sent = McuProtocol::GetInstance().SendControl(code);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cached = hours;
            state_.valid_mask |= FunctionMask(dp_id);
        }
        ESP_LOGI(kTag, "TX delay command 0x%02X (%u h)", base_code + hours - 1,
                 static_cast<unsigned>(hours));
        NotifyStateChanged(dp_id);
    } else {
        ESP_LOGE(kTag, "TX delay command 0x%02X failed",
                 base_code + hours - 1);
    }
    return sent;
}

bool McuDp::SetDelayShutdown(uint8_t hours) {
    return HandleDelayCommand(hours, kDelayShutdownCodeBase,
                              McuDpId::kDelayShutdown,
                              state_.delay_shutdown_hours);
}

bool McuDp::SetDelayPowerOn(uint8_t hours) {
    return HandleDelayCommand(hours, kDelayPowerOnCodeBase,
                              McuDpId::kDelayPowerOn,
                              state_.delay_power_on_hours);
}

bool McuDp::SetIceThickness(int8_t thickness) {
    if (thickness < -5 || thickness > 5) {
        ESP_LOGE(kTag, "Ice thickness out of range: %d",
                 static_cast<int>(thickness));
        return false;
    }
    const auto code = static_cast<McuControlCode>(
        kIceThicknessCodeBase + static_cast<uint8_t>(thickness + 5));
    const bool sent = McuProtocol::GetInstance().SendControl(code);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.ice_thickness = thickness;
            state_.valid_mask |= FunctionMask(McuDpId::kIceThickness);
        }
        ESP_LOGI(kTag, "TX ice thickness: %d",
                 static_cast<int>(thickness));
        NotifyStateChanged(McuDpId::kIceThickness);
    } else {
        ESP_LOGE(kTag, "TX ice thickness failed: %d",
                 static_cast<int>(thickness));
    }
    return sent;
}

bool McuDp::ReportWifiConnected(bool connected) {
    const bool sent = McuProtocol::GetInstance().SendWifi(
        connected ? McuWifiCode::kWifiConnected
                  : McuWifiCode::kWifiDisconnected);
    if (sent) {
        ESP_LOGI(kTag, "TX wifi state: %s",
                 connected ? "CONNECTED" : "DISCONNECTED");
    } else {
        ESP_LOGE(kTag, "TX wifi state failed: %s",
                 connected ? "CONNECTED" : "DISCONNECTED");
    }
    return sent;
}

bool McuDp::ReportAiModuleState(AiModuleState state) {
    // 新制冰机协议（集贤 UAY058 V1.0.1）无 AI 模组状态上报帧，
    // 保留此接口仅用于兼容主应用调用链，不向 MCU 发送任何数据。
    ESP_LOGI(kTag, "AI module state (no wire frame in new protocol): 0x%02X",
             static_cast<uint8_t>(state));
    return true;
}

bool McuDp::QueryAllStatus() {
    const bool sent = McuProtocol::GetInstance().QueryAllStatus();
    if (sent) {
        ESP_LOGI(kTag, "TX query-all-status");
    } else {
        ESP_LOGE(kTag, "TX query-all-status failed");
    }
    return sent;
}

McuDpState McuDp::GetState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void McuDp::SetStateChangedCallback(StateChangedCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_changed_callback_ = std::move(callback);
}

void McuDp::HandleEvent(McuEventCode event) {
    const char* announcement = EventAnnouncement(event);
    const uint8_t fault_bit = FaultCodeBit(event);

    switch (event) {
        case McuEventCode::kClearIceStarted: {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.work_mode = kModeClearIce;
            state_.valid_mask |= FunctionMask(McuDpId::kWorkMode);
        }
            ESP_LOGI(kTag, "RX event: clear-ice started");
            NotifyStateChanged(McuDpId::kWorkMode);
            return;

        case McuEventCode::kPowerOff: {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.work_mode = kModeOff;
            state_.valid_mask |= FunctionMask(McuDpId::kWorkMode);
        }
            ESP_LOGI(kTag, "RX event: power off");
            NotifyStateChanged(McuDpId::kWorkMode);
            return;

        case McuEventCode::kIceFull: {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.ice_full = true;
            state_.fault_flags |= kFaultBitIceFull;
            state_.valid_mask |= FunctionMask(McuDpId::kIceFull) |
                                 FunctionMask(McuDpId::kFault);
        }
            ESP_LOGI(kTag, "RX event: ice full");
            NotifyStateChanged(McuDpId::kIceFull);
            break;

        case McuEventCode::kWaterShortage: {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.water_shortage = true;
            state_.fault_flags |= kFaultBitWaterShortage;
            state_.valid_mask |= FunctionMask(McuDpId::kWaterShortage) |
                                 FunctionMask(McuDpId::kFault);
        }
            ESP_LOGI(kTag, "RX event: water shortage");
            NotifyStateChanged(McuDpId::kWaterShortage);
            break;

        case McuEventCode::kIceBatchDone: {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.ice_batch_done = true;
            state_.valid_mask |= FunctionMask(McuDpId::kIceBatchDone);
        }
            ESP_LOGI(kTag, "RX event: ice batch done");
            NotifyStateChanged(McuDpId::kIceBatchDone);
            break;

        case McuEventCode::kFaultE1:
        case McuEventCode::kFaultE2:
        case McuEventCode::kFaultE3:
        case McuEventCode::kFaultE4:
        case McuEventCode::kFaultE5: {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.fault_flags |= fault_bit;
            state_.valid_mask |= FunctionMask(McuDpId::kFault);
        }
            ESP_LOGI(kTag, "RX event: fault 0x%02X",
                     static_cast<uint8_t>(event));
            NotifyStateChanged(McuDpId::kFault);
            break;

        default:
            break;
    }

    if (announcement != nullptr) {
        RespondToMcuDp(announcement);
    }
}

void McuDp::HandleWifi(McuWifiCode code) {
    switch (code) {
        case McuWifiCode::kWifiOn:
            ESP_LOGI(kTag, "RX wifi command: turn on wifi (module-managed)");
            break;

        case McuWifiCode::kWifiOff:
            ESP_LOGI(kTag, "RX wifi command: turn off wifi (module-managed)");
            break;

        case McuWifiCode::kStartNetworkConfig:
            ESP_LOGI(kTag, "RX wifi command: enter network config");
            // UART 协议回调运行在 UART 任务中，配网状态变更统一调度到应用任务。
            Application::GetInstance().Schedule([]() {
                auto& wifi_board =
                    static_cast<WifiBoard&>(Board::GetInstance());
                if (!wifi_board.IsInWifiConfigMode()) {
                    wifi_board.EnterWifiConfigMode();
                }
                // 协议要求：开启配网时播报引导语。
                RespondToMcuDp("请使用京东小家帮我配网吧");
            });
            break;

        default:
            ESP_LOGW(kTag, "RX unexpected wifi command: 0x%02X",
                     static_cast<uint8_t>(code));
            break;
    }
}

void McuDp::HandleAllStatus(const McuAllStatus& status) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.work_mode = status.work_mode;
        state_.uv_on = status.uv_on;
        state_.delay_shutdown_hours = status.delay_shutdown_hours;
        state_.delay_power_on_hours = status.delay_power_on_hours;
        state_.tds = status.tds;
        state_.remaining_ice_minutes = status.remaining_ice_minutes;
        state_.remaining_clean_minutes = status.remaining_clean_minutes;
        state_.ice_count = status.ice_count;
        state_.fault_flags = status.fault_flags;
        state_.ice_thickness = status.IceThickness();
        state_.ice_full = status.IsIceFull();
        state_.water_shortage = status.IsWaterShortage();
        state_.valid_mask |= FunctionMask(McuDpId::kWorkMode) |
                             FunctionMask(McuDpId::kUvLight) |
                             FunctionMask(McuDpId::kDelayShutdown) |
                             FunctionMask(McuDpId::kDelayPowerOn) |
                             FunctionMask(McuDpId::kTds) |
                             FunctionMask(McuDpId::kRemainingIceTime) |
                             FunctionMask(McuDpId::kRemainingCleanTime) |
                             FunctionMask(McuDpId::kIceCount) |
                             FunctionMask(McuDpId::kFault) |
                             FunctionMask(McuDpId::kIceThickness);
    }

    ESP_LOGI(kTag, "RX all status: mode=%s(%u) uv=%s tds=%u iceLeft=%u min "
                   "cleanLeft=%u min iceCount=%u fault=%s(0x%02X) thick=%d",
             WorkModeName(status.work_mode), status.work_mode,
             status.uv_on ? "ON" : "OFF", status.tds,
             status.remaining_ice_minutes, status.remaining_clean_minutes,
             status.ice_count, FaultCodeName(status.FaultCode()),
             status.fault_flags, status.IceThickness());

    NotifyStateChanged(McuDpId::kWorkMode);
    NotifyStateChanged(McuDpId::kFault);
}

void McuDp::NotifyStateChanged(McuDpId dp_id) {
    StateChangedCallback callback;
    McuDpState snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = state_changed_callback_;
        snapshot = state_;
    }

    if (callback) {
        callback(dp_id, snapshot);
    }
}

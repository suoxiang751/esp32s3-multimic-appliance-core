#include "mcu_dp.h"

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
constexpr uint8_t kFirstFunctionId = 0x01;
constexpr uint8_t kLastFunctionId = 0x0F;

void RespondToMcuDp(const std::string& announcement) {
#if MCU_DP_USE_LOCAL_SOUND_RESPONSE
    (void)announcement;
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_DI_01);
#else
    Application::GetInstance().SendText(announcement);
#endif
}

uint16_t FunctionMask(McuFunctionId function_id) {
    const uint8_t id = static_cast<uint8_t>(function_id);
    if (id < kFirstFunctionId || id > kLastFunctionId) {
        return 0;
    }
    return static_cast<uint16_t>(1U << (id - kFirstFunctionId));
}

const char* WorkStateName(McuWorkState state) {
    switch (state) {
        case McuWorkState::kStandby:
            return "standby";
        case McuWorkState::kDispensing:
            return "dispensing";
        case McuWorkState::kHeating:
            return "heating";
        case McuWorkState::kAutoCleaning:
            return "auto cleaning";
        case McuWorkState::kFault:
            return "fault";
        default:
            return "unknown";
    }
}

const char* WorkStateAnnouncement(McuWorkState state) {
    switch (state) {
        case McuWorkState::kStandby:
            return "饮水机已进入待机状态";
        case McuWorkState::kDispensing:
            return "饮水机正在出水";
        case McuWorkState::kHeating:
            return "饮水机正在加热";
        case McuWorkState::kAutoCleaning:
            return "饮水机正在自动清洗";
        case McuWorkState::kFault:
            return "饮水机发生故障";
        default:
            return nullptr;
    }
}

const char* AutoCleanStateName(McuAutoCleanState state) {
    return state == McuAutoCleanState::kStarted ? "started" : "stopped";
}

const char* FaultCodeName(McuFaultCode fault) {
    switch (fault) {
        case McuFaultCode::kNone:
            return "normal";
        case McuFaultCode::kE1WaterShortage:
            return "E1 water shortage";
        case McuFaultCode::kE3FreezeProtection:
            return "E3 freeze protection";
        case McuFaultCode::kE4OverTemperature:
            return "E4 over-temperature";
        case McuFaultCode::kE5HotSensorDisconnected:
            return "E5 hot-water sensor disconnected";
        case McuFaultCode::kE6HotSensorOffset:
            return "E6 hot-water sensor offset";
        case McuFaultCode::kE7NormalSensorDisconnected:
            return "E7 normal-water sensor disconnected";
        case McuFaultCode::kE8NormalSensorOffset:
            return "E8 normal-water sensor offset";
        case McuFaultCode::kL3SensorGroupAlarm:
            return "L3 sensor group alarm";
        case McuFaultCode::kF7CleanReminder:
            return "F7 clean reminder";
        default:
            return "unknown";
    }
}

const char* BeverageModeName(McuBeverageMode mode) {
    switch (mode) {
        case McuBeverageMode::kNormalWater:
            return "normal water";
        case McuBeverageMode::kGreenTea:
            return "green tea";
        case McuBeverageMode::kMilk:
            return "milk";
        case McuBeverageMode::kCoffee:
            return "coffee";
        case McuBeverageMode::kBlackTea:
            return "black tea";
        case McuBeverageMode::kLemonWater:
            return "lemon water";
        case McuBeverageMode::kHoney:
            return "honey";
        case McuBeverageMode::kSesamePaste:
            return "sesame paste";
        case McuBeverageMode::kCustom:
            return "custom";
        default:
            return "unknown";
    }
}

const char* FaultCodeAnnouncement(McuFaultCode fault) {
    switch (fault) {
        case McuFaultCode::kNone:
            return nullptr;
        case McuFaultCode::kE1WaterShortage:
            return "饮水机发生E1缺水故障";
        case McuFaultCode::kE3FreezeProtection:
            return "饮水机触发E3防冻保护";
        case McuFaultCode::kE4OverTemperature:
            return "饮水机触发E4高温保护";
        case McuFaultCode::kE5HotSensorDisconnected:
            return "饮水机发生E5热水温感断开故障";
        case McuFaultCode::kE6HotSensorOffset:
            return "饮水机发生E6热水温感偏移故障";
        case McuFaultCode::kE7NormalSensorDisconnected:
            return "饮水机发生E7常温温感断开故障";
        case McuFaultCode::kE8NormalSensorOffset:
            return "饮水机发生E8常温温感偏移故障";
        case McuFaultCode::kL3SensorGroupAlarm:
            return "饮水机发生L3温感集体报警";
        case McuFaultCode::kF7CleanReminder:
            return "饮水机发出F7清洗提醒";
        default:
            return nullptr;
    }
}

const char* BeverageModeChineseName(McuBeverageMode mode) {
    switch (mode) {
        case McuBeverageMode::kNormalWater:
            return "常温水";
        case McuBeverageMode::kGreenTea:
            return "绿茶";
        case McuBeverageMode::kMilk:
            return "冲奶";
        case McuBeverageMode::kCoffee:
            return "咖啡";
        case McuBeverageMode::kBlackTea:
            return "红茶";
        case McuBeverageMode::kLemonWater:
            return "柠檬水";
        case McuBeverageMode::kHoney:
            return "蜂蜜";
        case McuBeverageMode::kSesamePaste:
            return "芝麻糊";
        case McuBeverageMode::kCustom:
            return "自定义";
        default:
            return nullptr;
    }
}

}  // namespace

bool McuDpState::HasValue(McuFunctionId function_id) const {
    const uint16_t mask = FunctionMask(function_id);
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
    protocol.SetFunctionDataCallback(
        [this](const McuFunctionData& data) { HandleFunctionData(data); });
    protocol.SetHeartbeatCallback(
        [this](uint8_t state) { HandleHeartbeat(state); });
}

bool McuDp::SetPower(bool on) {
    const bool sent =
        McuProtocol::GetInstance().SendBool(McuFunctionId::kPower, on);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.power = on;
            state_.valid_mask |= FunctionMask(McuFunctionId::kPower);
        }
        ESP_LOGI(kTag, "DP01 TX power: %s", on ? "ON" : "OFF");
        NotifyStateChanged(McuFunctionId::kPower);
    } else {
        ESP_LOGE(kTag, "DP01 TX power failed: %s", on ? "ON" : "OFF");
    }
    return sent;
}

bool McuDp::SetTargetTemperature(uint32_t temperature_celsius) {
    if (temperature_celsius < 25 || temperature_celsius > 100) {
        ESP_LOGE(kTag, "Target temperature out of range: %lu",
                 static_cast<unsigned long>(temperature_celsius));
        return false;
    }
    const bool sent = McuProtocol::GetInstance().SendValue(
        McuFunctionId::kTargetTemperature, temperature_celsius);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.target_temperature = temperature_celsius;
            state_.valid_mask |=
                FunctionMask(McuFunctionId::kTargetTemperature);
        }
        ESP_LOGI(kTag, "DP03 TX target temperature: %lu C",
                 static_cast<unsigned long>(temperature_celsius));
        NotifyStateChanged(McuFunctionId::kTargetTemperature);
    } else {
        ESP_LOGE(kTag, "DP03 TX target temperature failed: %lu C",
                 static_cast<unsigned long>(temperature_celsius));
    }
    return sent;
}

bool McuDp::SetTargetWaterVolume(uint32_t volume_ml) {
    if (volume_ml < 50 || volume_ml > 1000) {
        ESP_LOGE(kTag, "Target water volume out of range: %lu",
                 static_cast<unsigned long>(volume_ml));
        return false;
    }
    const bool sent = McuProtocol::GetInstance().SendValue(
        McuFunctionId::kTargetWaterVolume, volume_ml);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.target_water_volume = volume_ml;
            state_.valid_mask |=
                FunctionMask(McuFunctionId::kTargetWaterVolume);
        }
        ESP_LOGI(kTag, "DP04 TX target water volume: %lu mL",
                 static_cast<unsigned long>(volume_ml));
        NotifyStateChanged(McuFunctionId::kTargetWaterVolume);
    } else {
        ESP_LOGE(kTag, "DP04 TX target water volume failed: %lu mL",
                 static_cast<unsigned long>(volume_ml));
    }
    return sent;
}

bool McuDp::SetWaterControl(bool start) {
    const bool sent = McuProtocol::GetInstance().SendBool(
        McuFunctionId::kWaterControl, start);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.water_running = start;
            state_.valid_mask |= FunctionMask(McuFunctionId::kWaterControl);
        }
        ESP_LOGI(kTag, "DP07 TX water control: %s",
                 start ? "START" : "STOP");
        NotifyStateChanged(McuFunctionId::kWaterControl);
    } else {
        ESP_LOGE(kTag, "DP07 TX water control failed: %s",
                 start ? "START" : "STOP");
    }
    return sent;
}

bool McuDp::SetChildLock(bool locked) {
    const bool sent = McuProtocol::GetInstance().SendBool(
        McuFunctionId::kChildLock, locked);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.child_lock = locked;
            state_.valid_mask |= FunctionMask(McuFunctionId::kChildLock);
        }
        ESP_LOGI(kTag, "DP08 TX child lock: %s",
                 locked ? "LOCKED" : "UNLOCKED");
        NotifyStateChanged(McuFunctionId::kChildLock);
    } else {
        ESP_LOGE(kTag, "DP08 TX child lock failed: %s",
                 locked ? "LOCKED" : "UNLOCKED");
    }
    return sent;
}

bool McuDp::TriggerTdsQuery() {
    const bool sent = McuProtocol::GetInstance().SendBool(
        McuFunctionId::kTdsQuery, true);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.tds_query_triggered = true;
            state_.valid_mask |= FunctionMask(McuFunctionId::kTdsQuery);
        }
        ESP_LOGI(kTag, "DP09 TX TDS query triggered");
        NotifyStateChanged(McuFunctionId::kTdsQuery);
    } else {
        ESP_LOGE(kTag, "DP09 TX TDS query failed");
    }
    return sent;
}

bool McuDp::SetAutoClean(McuAutoCleanState state) {
    if (state != McuAutoCleanState::kStopped &&
        state != McuAutoCleanState::kStarted) {
        ESP_LOGE(kTag, "Invalid auto-clean state: 0x%02X",
                 static_cast<uint8_t>(state));
        return false;
    }
    const bool sent = McuProtocol::GetInstance().SendEnum(
        McuFunctionId::kAutoCleanControl, state);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.auto_clean = state;
            state_.valid_mask |=
                FunctionMask(McuFunctionId::kAutoCleanControl);
        }
        ESP_LOGI(kTag, "DP11 TX auto clean: %s", AutoCleanStateName(state));
        NotifyStateChanged(McuFunctionId::kAutoCleanControl);
    } else {
        ESP_LOGE(kTag, "DP11 TX auto clean failed: %s",
                 AutoCleanStateName(state));
    }
    return sent;
}

bool McuDp::SetBeverageMode(McuBeverageMode mode) {
    if (mode < McuBeverageMode::kNormalWater ||
        mode > McuBeverageMode::kCustom) {
        ESP_LOGE(kTag, "Invalid beverage mode: 0x%02X",
                 static_cast<uint8_t>(mode));
        return false;
    }
    const bool sent = McuProtocol::GetInstance().SendEnum(
        McuFunctionId::kBeverageMode, mode);
    if (sent) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.beverage_mode = mode;
            state_.valid_mask |=
                FunctionMask(McuFunctionId::kBeverageMode);
        }
        ESP_LOGI(kTag, "DP15 TX beverage mode: %s (0x%02X)",
                 BeverageModeName(mode), static_cast<uint8_t>(mode));
        NotifyStateChanged(McuFunctionId::kBeverageMode);
    } else {
        ESP_LOGE(kTag, "DP15 TX beverage mode failed: %s (0x%02X)",
                 BeverageModeName(mode), static_cast<uint8_t>(mode));
    }
    return sent;
}

bool McuDp::ReportAiModuleState(AiModuleState state) {
    if (state < AiModuleState::kStandby ||
        state > AiModuleState::kBroadcasting) {
        ESP_LOGE(kTag, "Invalid AI module state: 0x%02X",
                 static_cast<uint8_t>(state));
        return false;
    }

    const bool sent = McuProtocol::GetInstance().SendEnum(
        McuFunctionId::kAiModuleState, static_cast<uint8_t>(state));
    if (sent) {
        ESP_LOGI(kTag, "DP17 TX AI module state: 0x%02X",
                 static_cast<uint8_t>(state));
    } else {
        ESP_LOGE(kTag, "DP17 TX AI module state failed: 0x%02X",
                 static_cast<uint8_t>(state));
    }
    return sent;
}

bool McuDp::SendHeartbeat() {
    const bool sent = McuProtocol::GetInstance().SendHeartbeat();
    if (sent) {
        ESP_LOGI(kTag, "Heartbeat TX");
    } else {
        ESP_LOGE(kTag, "Heartbeat TX failed");
    }
    return sent;
}

bool McuDp::QueryAllStatus() {
    const bool sent = McuProtocol::GetInstance().QueryAllStatus();
    if (sent) {
        ESP_LOGI(kTag, "Query-all-status TX");
    } else {
        ESP_LOGE(kTag, "Query-all-status TX failed");
    }
    return sent;
}

McuDpState McuDp::GetState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool McuDp::IsMcuOnline() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mcu_online_;
}

uint8_t McuDp::GetHeartbeatState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return heartbeat_state_;
}

void McuDp::SetStateChangedCallback(StateChangedCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_changed_callback_ = std::move(callback);
}

void McuDp::HandleFunctionData(const McuFunctionData& data) {
    switch (data.function_id) {
        case McuFunctionId::kPower:
            HandleDp01Power(data);
            return;

        case McuFunctionId::kWorkState:
            HandleDp02WorkState(data);
            return;

        case McuFunctionId::kTargetTemperature:
            HandleDp03TargetTemperature(data);
            return;

        case McuFunctionId::kTargetWaterVolume:
            HandleDp04TargetWaterVolume(data);
            return;

        case McuFunctionId::kCurrentTemperature:
            HandleDp05CurrentTemperature(data);
            return;

        case McuFunctionId::kCurrentWaterVolume:
            HandleDp06CurrentWaterVolume(data);
            return;

        case McuFunctionId::kWaterControl:
            HandleDp07WaterControl(data);
            return;

        case McuFunctionId::kChildLock:
            HandleDp08ChildLock(data);
            return;

        case McuFunctionId::kTdsQuery:
            HandleDp09TdsQuery(data);
            return;

        case McuFunctionId::kTdsValue:
            HandleDp10TdsValue(data);
            return;

        case McuFunctionId::kAutoCleanControl:
            HandleDp11AutoClean(data);
            return;

        case McuFunctionId::kCleanReminder:
            HandleDp12CleanReminder(data);
            return;

        case McuFunctionId::kRemainingHeatingTime:
            HandleDp13RemainingHeatingTime(data);
            return;

        case McuFunctionId::kFaultCode:
            HandleDp14FaultCode(data);
            return;

        case McuFunctionId::kBeverageMode:
            HandleDp15BeverageMode(data);
            return;

        case McuFunctionId::kNetworkConfig:
            HandleDp16NetworkConfig(data);
            return;

        case McuFunctionId::kInterruptBroadcast:
            HandleDp18InterruptBroadcast(data);
            return;

        default:
            ESP_LOGW(kTag, "Unknown MCU DP: 0x%02X",
                     static_cast<uint8_t>(data.function_id));
            return;
    }
}

void McuDp::HandleDp01Power(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kBool, 0, 1)) {
        return;
    }
    const bool power_on = data.value != 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.power = power_on;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP01 RX power: %s", power_on ? "ON" : "OFF");

    const std::string announcement =
        power_on ? "饮水机已打开" : "饮水机已关闭";
    RespondToMcuDp(announcement);

    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp02WorkState(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kEnum, 0, 4)) {
        return;
    }
    const auto work_state = static_cast<McuWorkState>(data.value);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.work_state = work_state;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP02 RX work state: %s (0x%02X)",
             WorkStateName(work_state),
             static_cast<uint8_t>(data.value));

    const char* announcement = WorkStateAnnouncement(work_state);
    if (announcement != nullptr) {
        RespondToMcuDp(announcement);
    }

    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp03TargetTemperature(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kValue, 25, 100)) {
        return;
    }
    const uint32_t target_temperature = data.value;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.target_temperature = target_temperature;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP03 RX target temperature: %lu C",
             static_cast<unsigned long>(target_temperature));

    const std::string announcement =
        "饮水机温度已设置为" +
        std::to_string(target_temperature) + "摄氏度";
    RespondToMcuDp(announcement);

    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp04TargetWaterVolume(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kValue, 50, 1000)) {
        return;
    }
    const uint32_t target_water_volume = data.value;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.target_water_volume = target_water_volume;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP04 RX target water volume: %lu mL",
             static_cast<unsigned long>(target_water_volume));

    const std::string announcement =
        "饮水机出水量已设置为" +
        std::to_string(target_water_volume) + "毫升";
    RespondToMcuDp(announcement);

    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp05CurrentTemperature(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kValue, 0, 110)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.current_temperature = data.value;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP05 RX current temperature: %lu C",
             static_cast<unsigned long>(data.value));
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp06CurrentWaterVolume(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kValue, 0, 1000)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.current_water_volume = data.value;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP06 RX current water volume: %lu mL",
             static_cast<unsigned long>(data.value));
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp07WaterControl(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kBool, 0, 1)) {
        return;
    }
    const bool water_running = data.value != 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.water_running = water_running;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP07 RX water control: %s",
             water_running ? "STARTED" : "STOPPED");

#if MCU_DP_USE_LOCAL_SOUND_RESPONSE
    Application::GetInstance().PlaySound(
        water_running ? Lang::Sounds::OGG_START_IZUMI
                      : Lang::Sounds::OGG_STOP_IZUMI);
#else
    RespondToMcuDp(
        water_running ? "饮水机已开始出水" : "饮水机已停止出水");
#endif

    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp08ChildLock(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kBool, 0, 1)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.child_lock = data.value != 0;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP08 RX child lock: %s",
             data.value != 0 ? "LOCKED" : "UNLOCKED");
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp09TdsQuery(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kBool, 0, 1)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.tds_query_triggered = data.value != 0;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP09 RX TDS query flag: %s",
             data.value != 0 ? "TRIGGERED" : "IDLE");
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp10TdsValue(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kValue, 0, 999)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.tds = data.value;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP10 RX TDS: %lu PPM",
             static_cast<unsigned long>(data.value));
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp11AutoClean(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kEnum, 0, 1)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.auto_clean = static_cast<McuAutoCleanState>(data.value);
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP11 RX auto clean: %s",
             AutoCleanStateName(static_cast<McuAutoCleanState>(data.value)));
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp12CleanReminder(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kBool, 0, 1)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.clean_required = data.value != 0;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP12 RX clean reminder: %s",
             data.value != 0 ? "REQUIRED" : "NOT REQUIRED");
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp13RemainingHeatingTime(
    const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kValue, 0, 21600)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.remaining_heating_minutes = data.value;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP13 RX remaining heating time: %lu min",
             static_cast<unsigned long>(data.value));
    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp14FaultCode(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kEnum, 0, 9)) {
        return;
    }
    const auto fault_code = static_cast<McuFaultCode>(data.value);
    const uint16_t function_mask = FunctionMask(data.function_id);
    bool fault_was_known = false;
    McuFaultCode previous_fault = McuFaultCode::kNone;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fault_was_known = (state_.valid_mask & function_mask) != 0;
        previous_fault = state_.fault_code;
        state_.fault_code = fault_code;
        state_.valid_mask |= function_mask;
    }
    ESP_LOGI(kTag, "DP14 RX fault: %s (0x%02X)",
             FaultCodeName(fault_code),
             static_cast<uint8_t>(data.value));

    if (fault_code == McuFaultCode::kNone) {
        if (fault_was_known && previous_fault != McuFaultCode::kNone) {
            RespondToMcuDp("饮水机故障已解除");
        }
    } else {
        const char* announcement = FaultCodeAnnouncement(fault_code);
        if (announcement != nullptr) {
            RespondToMcuDp(announcement);
        }
    }

    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp15BeverageMode(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kEnum, 0, 8)) {
        return;
    }
    const auto beverage_mode = static_cast<McuBeverageMode>(data.value);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.beverage_mode = beverage_mode;
        state_.valid_mask |= FunctionMask(data.function_id);
    }
    ESP_LOGI(kTag, "DP15 RX beverage mode: %s (0x%02X)",
             BeverageModeName(beverage_mode),
             static_cast<uint8_t>(data.value));

    const char* mode_name = BeverageModeChineseName(beverage_mode);
    if (mode_name != nullptr) {
        const std::string announcement =
            std::string("饮水机已切换到") + mode_name + "模式";
        RespondToMcuDp(announcement);
    }

    NotifyStateChanged(data.function_id);
}

void McuDp::HandleDp16NetworkConfig(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kEnum, 0, 1)) {
        return;
    }

    const auto command = static_cast<McuNetworkConfigCommand>(data.value);
    ESP_LOGI(kTag, "DP16 RX network config: %s",
             command == McuNetworkConfigCommand::kEnter ? "ENTER" : "EXIT");

    // UART protocol callbacks run in the UART protocol task. Network and
    // application state changes are serialized through the application task.
    Application::GetInstance().Schedule([command]() {
        auto& wifi_board = static_cast<WifiBoard&>(Board::GetInstance());
        if (command == McuNetworkConfigCommand::kEnter) {
            if (!wifi_board.IsInWifiConfigMode()) {
                wifi_board.EnterWifiConfigMode();
            }
            return;
        }

        auto& wifi_manager = WifiManager::GetInstance();
        if (wifi_manager.IsConfigMode()) {
            wifi_manager.StopConfigAp();
        }
    });
}

void McuDp::HandleDp18InterruptBroadcast(const McuFunctionData& data) {
    if (!ValidateData(data, McuDataType::kEnum, 1, 1)) {
        return;
    }

    ESP_LOGI(kTag, "DP18 RX interrupt current broadcast");
    Application::GetInstance().Schedule([]() {
        auto& app = Application::GetInstance();
        app.ToggleChatState();
    });
}

void McuDp::HandleHeartbeat(uint8_t heartbeat_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    heartbeat_state_ = heartbeat_state;
    mcu_online_ = true;
    ESP_LOGI(kTag, "Heartbeat RX state: 0x%02X", heartbeat_state);
}

bool McuDp::ValidateData(const McuFunctionData& data,
                         McuDataType expected_type, uint32_t minimum,
                         uint32_t maximum) const {
    if (data.data_type != expected_type) {
        ESP_LOGW(kTag, "DP 0x%02X type mismatch: expected 0x%02X, got 0x%02X",
                 static_cast<uint8_t>(data.function_id),
                 static_cast<uint8_t>(expected_type),
                 static_cast<uint8_t>(data.data_type));
        return false;
    }
    if (data.value < minimum || data.value > maximum) {
        ESP_LOGW(kTag, "DP 0x%02X value out of range: %lu",
                 static_cast<uint8_t>(data.function_id),
                 static_cast<unsigned long>(data.value));
        return false;
    }
    return true;
}

void McuDp::NotifyStateChanged(McuFunctionId function_id) {
    StateChangedCallback callback;
    McuDpState snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = state_changed_callback_;
        snapshot = state_;
    }

    if (callback) {
        callback(function_id, snapshot);
    }
}

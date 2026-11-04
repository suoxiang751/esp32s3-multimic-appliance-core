#include "mcp.h"

#include <stdexcept>
#include <string>

#include <esp_log.h>

#include "mcp_server.h"
#include "mcu_dp.h"
#include "application.h"

namespace {

constexpr char kTag[] = "WaterDispenserMcp";

ReturnValue SetWaterDispenserPower(bool on) {
    if (!McuDp::GetInstance().SetPower(on)) {
        throw std::runtime_error(
            on ? "打开饮水机指令发送失败" : "关闭饮水机指令发送失败");
    }

    // This text is returned to the cloud through the MCP tool result. The
    // cloud then generates and sends the corresponding TTS audio.
    return std::string(on ? "正在打开饮水机" : "正在关闭饮水机");
}

ReturnValue SetWaterDispenserWaterControl(bool start) {
    if (!McuDp::GetInstance().SetWaterControl(start)) {
        throw std::runtime_error(
            start ? "开始出水指令发送失败" : "停止出水指令发送失败");
    }

    return std::string(start ? "正在开始出水" : "正在停止出水");
}

ReturnValue SetWaterDispenserTemperature(int temperature_celsius) {
    if (!McuDp::GetInstance().SetTargetTemperature(
            static_cast<uint32_t>(temperature_celsius))) {
        throw std::runtime_error("设置饮水机温度指令发送失败");
    }

    return std::string("正在将水温设置为") +
           std::to_string(temperature_celsius) + "摄氏度";
}

ReturnValue SetWaterDispenserVolume(int volume_ml) {
    if (!McuDp::GetInstance().SetTargetWaterVolume(
            static_cast<uint32_t>(volume_ml))) {
        throw std::runtime_error("设置饮水机出水量指令发送失败");
    }

    return std::string("正在将出水量设置为") + std::to_string(volume_ml) +
           "毫升";
}

McuBeverageMode ParseBeverageMode(const std::string& mode) {
    if (mode == "normal_water" || mode == "常温水") {
        return McuBeverageMode::kNormalWater;
    }
    if (mode == "green_tea" || mode == "绿茶") {
        return McuBeverageMode::kGreenTea;
    }
    if (mode == "milk" || mode == "冲奶") {
        return McuBeverageMode::kMilk;
    }
    if (mode == "coffee" || mode == "咖啡") {
        return McuBeverageMode::kCoffee;
    }
    if (mode == "black_tea" || mode == "红茶") {
        return McuBeverageMode::kBlackTea;
    }
    if (mode == "lemon_water" || mode == "柠檬水") {
        return McuBeverageMode::kLemonWater;
    }
    if (mode == "honey" || mode == "蜂蜜") {
        return McuBeverageMode::kHoney;
    }
    if (mode == "sesame_paste" || mode == "芝麻糊") {
        return McuBeverageMode::kSesamePaste;
    }
    if (mode == "custom" || mode == "自定义") {
        return McuBeverageMode::kCustom;
    }

    throw std::runtime_error("不支持的饮品模式：" + mode);
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
            return "未知";
    }
}

bool GetBeverageModeTemperature(McuBeverageMode mode,
                                uint32_t& temperature_celsius) {
    switch (mode) {
        case McuBeverageMode::kNormalWater:
            temperature_celsius = 25;
            return true;
        case McuBeverageMode::kGreenTea:
            temperature_celsius = 90;
            return true;
        case McuBeverageMode::kMilk:
            temperature_celsius = 45;
            return true;
        case McuBeverageMode::kCoffee:
            temperature_celsius = 95;
            return true;
        case McuBeverageMode::kBlackTea:
            temperature_celsius = 100;
            return true;
        case McuBeverageMode::kLemonWater:
            temperature_celsius = 60;
            return true;
        case McuBeverageMode::kHoney:
            temperature_celsius = 55;
            return true;
        case McuBeverageMode::kSesamePaste:
            temperature_celsius = 85;
            return true;
        case McuBeverageMode::kCustom:
        default:
            return false;
    }
}

const char* WorkStateChineseName(McuWorkState state) {
    switch (state) {
        case McuWorkState::kStandby:
            return "待机";
        case McuWorkState::kDispensing:
            return "出水中";
        case McuWorkState::kHeating:
            return "加热中";
        case McuWorkState::kAutoCleaning:
            return "自动清洗中";
        case McuWorkState::kFault:
            return "故障";
        default:
            return "未知";
    }
}

const char* FaultCodeChineseName(McuFaultCode fault) {
    switch (fault) {
        case McuFaultCode::kNone:
            return "正常";
        case McuFaultCode::kE1WaterShortage:
            return "E1缺水";
        case McuFaultCode::kE3FreezeProtection:
            return "E3防冻保护";
        case McuFaultCode::kE4OverTemperature:
            return "E4高温保护";
        case McuFaultCode::kE5HotSensorDisconnected:
            return "E5热水温感断开";
        case McuFaultCode::kE6HotSensorOffset:
            return "E6热水温感偏移";
        case McuFaultCode::kE7NormalSensorDisconnected:
            return "E7常温温感断开";
        case McuFaultCode::kE8NormalSensorOffset:
            return "E8常温温感偏移";
        case McuFaultCode::kL3SensorGroupAlarm:
            return "L3温感集体报警";
        case McuFaultCode::kF7CleanReminder:
            return "F7清洗提醒";
        default:
            return "未知";
    }
}

void AddBooleanStatus(cJSON* json, const char* name,
                      const McuDpState& state, McuFunctionId function_id,
                      bool value) {
    if (state.HasValue(function_id)) {
        cJSON_AddBoolToObject(json, name, value);
    } else {
        cJSON_AddNullToObject(json, name);
    }
}

void AddNumberStatus(cJSON* json, const char* name,
                     const McuDpState& state, McuFunctionId function_id,
                     uint32_t value) {
    if (state.HasValue(function_id)) {
        cJSON_AddNumberToObject(json, name, value);
    } else {
        cJSON_AddNullToObject(json, name);
    }
}

void AddStringStatus(cJSON* json, const char* name,
                     const McuDpState& state, McuFunctionId function_id,
                     const char* value) {
    if (state.HasValue(function_id)) {
        cJSON_AddStringToObject(json, name, value);
    } else {
        cJSON_AddNullToObject(json, name);
    }
}

ReturnValue GetWaterDispenserStatus() {
    auto& mcu_dp = McuDp::GetInstance();
    const McuDpState state = mcu_dp.GetState();
    cJSON* status = cJSON_CreateObject();
    if (status == nullptr) {
        throw std::runtime_error("创建饮水机状态结果失败");
    }

    cJSON_AddBoolToObject(status, "mcu_online", mcu_dp.IsMcuOnline());
    AddBooleanStatus(status, "power_on", state, McuFunctionId::kPower,
                     state.power);
    AddStringStatus(status, "work_state", state, McuFunctionId::kWorkState,
                    WorkStateChineseName(state.work_state));
    AddNumberStatus(status, "target_temperature_celsius", state,
                    McuFunctionId::kTargetTemperature,
                    state.target_temperature);
    AddNumberStatus(status, "target_volume_ml", state,
                    McuFunctionId::kTargetWaterVolume,
                    state.target_water_volume);
    AddNumberStatus(status, "current_temperature_celsius", state,
                    McuFunctionId::kCurrentTemperature,
                    state.current_temperature);
    AddNumberStatus(status, "current_volume_ml", state,
                    McuFunctionId::kCurrentWaterVolume,
                    state.current_water_volume);
    AddBooleanStatus(status, "dispensing", state,
                     McuFunctionId::kWaterControl, state.water_running);
    AddBooleanStatus(status, "child_lock", state, McuFunctionId::kChildLock,
                     state.child_lock);
    AddNumberStatus(status, "tds_ppm", state, McuFunctionId::kTdsValue,
                    state.tds);
    AddBooleanStatus(status, "auto_cleaning", state,
                     McuFunctionId::kAutoCleanControl,
                     state.auto_clean == McuAutoCleanState::kStarted);
    AddBooleanStatus(status, "clean_required", state,
                     McuFunctionId::kCleanReminder, state.clean_required);
    AddNumberStatus(status, "remaining_heating_minutes", state,
                    McuFunctionId::kRemainingHeatingTime,
                    state.remaining_heating_minutes);
    AddStringStatus(status, "fault", state, McuFunctionId::kFaultCode,
                    FaultCodeChineseName(state.fault_code));
    AddStringStatus(status, "beverage_mode", state,
                    McuFunctionId::kBeverageMode,
                    BeverageModeChineseName(state.beverage_mode));
    return status;
}

ReturnValue SetWaterDispenserBeverageMode(const std::string& mode_name) {
    const McuBeverageMode mode = ParseBeverageMode(mode_name);
    auto& mcu_dp = McuDp::GetInstance();
    if (!mcu_dp.SetBeverageMode(mode)) {
        throw std::runtime_error("设置饮水机饮品模式指令发送失败");
    }

    uint32_t temperature_celsius = 0;
    if (!GetBeverageModeTemperature(mode, temperature_celsius)) {
        return std::string("正在将饮水机切换到") +
               BeverageModeChineseName(mode) + "模式";
    }
    if (!mcu_dp.SetTargetTemperature(temperature_celsius)) {
        throw std::runtime_error(
            "饮品模式已下发，但对应温度设置指令发送失败");
    }

    return std::string("正在将饮水机切换到") +
           BeverageModeChineseName(mode) + "模式，水温设置为" +
           std::to_string(temperature_celsius) + "摄氏度";
}

ReturnValue SetConversationModeByName(const std::string& mode_name) {
    ListeningMode mode;
    if (mode_name == "auto_stop" || mode_name == "连续对话") {
        mode = kListeningModeAutoStop;
    } else if (mode_name == "realtime" || mode_name == "实时对话") {
        mode = kListeningModeRealtime;
    } else {
        throw std::runtime_error("不支持的对话模式：" + mode_name);
    }

    // Module-local feature: only switches the local conversation mode,
    // nothing is sent to the MCU.
    Application::GetInstance().SetConversationMode(mode);
    return std::string("已切换到") +
           (mode == kListeningModeRealtime ? "实时对话" : "连续对话") + "模式";
}

ReturnValue GetConversationModeStatus() {
    const ListeningMode mode = Application::GetInstance().GetConversationMode();
    cJSON* status = cJSON_CreateObject();
    if (status == nullptr) {
        throw std::runtime_error("创建对话模式状态结果失败");
    }
    cJSON_AddStringToObject(
        status, "mode",
        mode == kListeningModeRealtime ? "realtime" : "auto_stop");
    cJSON_AddStringToObject(
        status, "mode_name",
        mode == kListeningModeRealtime ? "实时对话" : "连续对话");
    return status;
}
}  // namespace

void PotekWaterDispenserMcpInitialize() {
    static bool initialized = false;
    if (initialized) {
        ESP_LOGW(kTag, "MCP tools are already initialized");
        return;
    }

    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddTool(
        "self.water_dispenser.turn_on",
        "Turn on the WaterDispenser water dispenser. After the command succeeds, "
        "speak the returned Chinese confirmation to the user verbatim.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return SetWaterDispenserPower(true);
        });

    mcp_server.AddTool(
        "self.water_dispenser.turn_off",
        "Turn off the WaterDispenser water dispenser. After the command succeeds, "
        "speak the returned Chinese confirmation to the user verbatim.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return SetWaterDispenserPower(false);
        });

    mcp_server.AddTool(
        "self.water_dispenser.get_status",
        "Get the latest WaterDispenser water-dispenser status reported by the MCU. "
        "Always call this read-only tool for questions about device status; "
        "never answer from conversation memory. It returns online and power "
        "state, target and current temperature, target and current volume, "
        "dispensing and work state, beverage mode, child lock, TDS, cleaning, "
        "heating time, and faults. A null field has not yet been reported. "
        "This tool never changes settings or starts dispensing.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return GetWaterDispenserStatus();
        });

    mcp_server.AddTool(
        "self.water_dispenser.set_temperature",
        "Set only the target water temperature of the WaterDispenser water dispenser. "
        "temperature_celsius must be 25 to 100. Always call set_volume after "
        "this tool, using the requested volume or the device default. Never "
        "start dispensing automatically; ask the user whether to dispense "
        "after all parameters are set.",
        PropertyList({
            Property("temperature_celsius", kPropertyTypeInteger, 25, 100)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            return SetWaterDispenserTemperature(
                properties["temperature_celsius"].value<int>());
        });

    mcp_server.AddTool(
        "self.water_dispenser.set_volume",
        "Set only the target water volume of the WaterDispenser water dispenser. "
        "volume_ml must be 50 to 1000. This tool never starts dispensing. "
        "After all requested parameters are set, ask the user whether to "
        "dispense; do not claim that dispensing has started.",
        PropertyList({
            Property("volume_ml", kPropertyTypeInteger, 50, 1000)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            return SetWaterDispenserVolume(
                properties["volume_ml"].value<int>());
        });

    mcp_server.AddTool(
        "self.water_dispenser.control_water",
        "Start or stop dispensing water from the WaterDispenser water dispenser without "
        "changing temperature or volume. Use start=true only after asking "
        "whether to dispense and receiving the user's explicit confirmation. "
        "This confirmation is required even if the user's initial request "
        "mentions dispensing. Never start based only on parameters, beverage "
        "mode, or inferred intent. A direct stop request does not require "
        "confirmation; call this tool immediately with start=false.",
        PropertyList({
            Property("start", kPropertyTypeBoolean)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            return SetWaterDispenserWaterControl(
                properties["start"].value<bool>());
        });

    mcp_server.AddTool(
        "self.water_dispenser.set_beverage_mode",
        "Set the beverage mode of the WaterDispenser water dispenser. Preset modes "
        "automatically send their matching target temperature, so do not "
        "call set_temperature again unless the user requests an override. "
        "This tool does not set volume or start dispensing. The required "
        "mode must be one of: normal_water (常温水), green_tea (绿茶), "
        "milk (冲奶), coffee (咖啡), black_tea (红茶), "
        "lemon_water (柠檬水), honey (蜂蜜), "
        "sesame_paste (芝麻糊), or custom (自定义). After the command "
        "succeeds, speak the returned Chinese confirmation to the user "
        "verbatim.",
        PropertyList({
            Property("mode", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            return SetWaterDispenserBeverageMode(
                properties["mode"].value<std::string>());
        });

    mcp_server.AddTool(
        "self.conversation.set_mode",
        "Switch the local conversation mode of the AI module (module-local "
        "feature; nothing is sent to the MCU). mode must be \"auto_stop\" "
        "(连续对话模式, kListeningModeAutoStop) or \"realtime\" (实时对话模式, "
        "kListeningModeRealtime). Call this tool when the user asks to switch "
        "between continuous conversation and real-time conversation. After "
        "the command succeeds, speak the returned Chinese confirmation to "
        "the user verbatim.",
        PropertyList({
            Property("mode", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            return SetConversationModeByName(
                properties["mode"].value<std::string>());
        });
    mcp_server.AddTool(
        "self.conversation.get_mode",
        "Get the current local conversation mode of the AI module "
        "(module-local feature; nothing is read from the MCU). Returns "
        "\"auto_stop\" (连续对话模式, kListeningModeAutoStop) or \"realtime\" "
        "(实时对话模式, kListeningModeRealtime). Always call this read-only "
        "tool when the user asks about the current conversation mode; never "
        "answer from conversation memory.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return GetConversationModeStatus();
        });
    initialized = true;
    ESP_LOGI(kTag, "Water-dispenser MCP tools registered");
}
